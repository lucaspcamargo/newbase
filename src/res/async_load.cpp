#include "newbase/res/resource.hpp"
#include <newbase/res/async_load.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>

#include <entt/core/fwd.hpp>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace nb;
using namespace nb::res::load;

#ifdef __EMSCRIPTEN__
#ifndef __EMSCRIPTEN_PTHREADS__
#error EMSCRIPTEN build incrrectly configured for thread usage.
#endif
#endif


// internal data

struct task_state
{
    // needs task mutex sync to get and set data
    task_status  status {};

    // only touched by worker thread, until task completion
    // then must not be touched again, will be moved by complete()
    // final_status is unused until complete() is invoked
    task_results results {};

    // the below state is only touched from the loader thread, so it can operate
    // on it without locking
    bool initialized {false};
    std::vector<std::shared_ptr<resource>> needs_preload;
    std::vector<std::shared_ptr<resource>> needs_load;
    std::vector<entt::id_type>             not_found;
};


struct worker::job
{
    task_handle handle;
    task_descriptor desc;
    std::shared_ptr<task_state> state;  // note that this is shared state with thread-safe API!
};


struct worker::worker_p
{
    std::atomic<task_handle> next_handle{1};
    std::atomic<bool> stop_requested{false};

    std::thread thread;

    mutable std::mutex queue_mutex;
    std::condition_variable cv;
    std::deque<job> job_queue;

    mutable std::mutex tasks_mutex;
    std::unordered_map<task_handle, std::shared_ptr<task_state>> tasks;
};


worker::worker()
{
    _d = std::make_unique<worker::worker_p>();
}


worker::~worker()
{
    stop();
}


void worker::run()
{
    if (!_d->thread.joinable()) {
        _d->stop_requested = false;
        _d->thread = std::thread(&worker::worker_loop, this);
    }
}


void worker::stop()
{
    _d->stop_requested = true;
    _d->cv.notify_all();

    if (_d->thread.joinable()) {
        _d->thread.join();
    }
}


task_handle worker::start(const task_descriptor& desc)
{
    if(desc.entries.empty())
        return TASK_INVALID;

    task_handle handle = _d->next_handle.fetch_add(1, std::memory_order_relaxed);

    auto state = std::make_shared<task_state>();

    {
        std::lock_guard<std::mutex> lock(_d->tasks_mutex);
        _d->tasks[handle] = state;
    }

    {
        std::lock_guard<std::mutex> lock(_d->queue_mutex);
        _d->job_queue.push_back(job{handle, desc, state});
    }

    _d->cv.notify_one();
    return handle;
}


task_status worker::status(task_handle handle) const
{
    std::lock_guard<std::mutex> lock(_d->tasks_mutex);

    auto it = _d->tasks.find(handle);
    if (it == _d->tasks.end())
        return {};

    const task_state& state = *it->second;
    return state.status;
}


bool worker::cancel(task_handle handle)
{
    if (handle == TASK_INVALID) {
        return false;
    }

    bool task_exists = false;

    {
        std::lock_guard<std::mutex> lock(_d->tasks_mutex);
        auto it = _d->tasks.find(handle);
        if (it != _d->tasks.end())
        {
            it->second->status.cancelled = true;
            task_exists = true;
        }
    }

    return task_exists;
}

task_results worker::complete(task_handle handle)
{
    std::lock_guard<std::mutex> lock(_d->tasks_mutex);

    auto it = _d->tasks.find(handle);
    if (it == _d->tasks.end())
        return {};

    auto &state = *(it->second);

    if(!state.status.done)
        return {};

    log::info("[res] async_worker: job completed, collected: %u", it->first);

    // store final task status in results
    // move resource pointers
    task_results ret = {
        .final_status = state.status,
        .root = std::move(state.results.root),
        .deps = std::move(state.results.deps)
    };

    // we can now safely remove it from the task pool
    _d->tasks.erase(it);

    return ret;
}

void worker::worker_loop()
{
    while (!_d->stop_requested)
    {
        job current_job;

        {
            std::unique_lock<std::mutex> lock(_d->queue_mutex);
            _d->cv.wait(lock, [this] {
                return !_d->job_queue.empty() || _d->stop_requested;
            });

            if (_d->stop_requested && _d->job_queue.empty()) {
                break;
            }

            current_job = std::move(_d->job_queue.front());
            _d->job_queue.pop_front();
        }

        auto completed = step_job(current_job);

        while(!completed && !_d->stop_requested)
        {
            // we step the same job repeatedly for as long as the queue is not empty

            completed = step_job(current_job);

            if(completed)
                break;

            // if queue is non-empty, and we haven't yet completed, put the
            // task back in the queue for round-robin
            {
                std::lock_guard<std::mutex> queue_lock(_d->queue_mutex);
                if (!_d->job_queue.empty()) {
                    _d->job_queue.push_back(std::move(current_job));
                    break; // Break inner loop -> fetches next task from front of queue
                }
            }
        }
    }
}

/// Returns true if job is completed
bool worker::step_job(const job& current_job)
{
    // buffer shared task state if not cancelled
    // if cancelled, return early
    task_status work_status;
    {
        std::lock_guard<std::mutex> lock(_d->tasks_mutex);
        if (current_job.state->status.cancelled)
        {
            // this task has been canceled, remove from internal task list
            // also return true so that the job doesn't return to queue
            _d->tasks.erase(current_job.handle);
            return true;
        }
        // we can continue loading this
        // let's initialize our runtime counters from the current counters
        work_status = current_job.state->status;
    }

    // now, we can do our actual fucking job
    // from here on, we can only touch local buffered variables
    // and the parts of task_state not used in the thread-safe API

    // check if we need to initialize
    if(!current_job.state->initialized)
    {
        // we need to take the root handles and add to our preload list
        log::info("[res] async_worker: job init: %u", current_job.handle);
        current_job.state->initialized = true;
        for(const auto &entry: current_job.desc.entries)
        {
            auto res = rman().create(entry.first, entry.second);
            auto state = res->state();
            std::vector<resource*> gather_deps;
            switch(state)
            {
                case resource_state::LOADED:
                    current_job.state->results.root.push_back(res);
                    work_status.loaded_count++;
                    gather_deps.push_back(res.get());
                    log::info("[res] async_worker: root already loaded: 0x%08x", res->id());
                    break;

                case resource_state::LOADING:
                    [[fallthrough]];
                case resource_state::PRELOADED:
                    current_job.state->needs_load.push_back(res);
                    gather_deps.push_back(res.get());
                    log::info("[res] async_worker: root already preloaded: 0x%08x", res->id());
                    break;

                case resource_state::PRELOADING:
                    [[fallthrough]];
                case resource_state::CREATED:
                    log::info("[res] async_worker: root: 0x%08x", res->id());
                    current_job.state->needs_preload.push_back(res);
                    break;

                case resource_state::INVALID:
                    [[fallthrough]];
                case resource_state::FAILED:
                    log::info("[res] async_worker: root already failed/invalid: 0x%08x", res->id());
                    current_job.state->results.root.push_back(res);
                    work_status.failed_count++;

                default:
                    log::error("[res] async_worker: root in unknown state: 0x%08x", res->id());
                    current_job.state->results.root.push_back(res);
                    work_status.failed_count++;
            }

            for(auto res_ptr: gather_deps)
            {
                if(auto deps = res_ptr->dependencies())
                {
                    for(const auto &dep: *deps)
                    {
                        log::info("[res] async_worker: root dep: 0x%08x", dep.second);
                        current_job.state->needs_preload.emplace_back(rman().create(dep.first, dep.second));
                    }
                }
            }
        }
    }

    // update state
    // resources are loaded in the opposite order they are discovered in preload
    // preload adds resources to load at the end of the load list
    // load consumes entries in the end of the load list
    // TODO what about cyclic dependencies? could be possible...

    // do all necessary loading here
    auto l_it = current_job.state->needs_load.begin();
    while(l_it != current_job.state->needs_load.end() && current_job.state->needs_preload.empty()) // TEST defer all loading after preloading
    {
        auto &res = *l_it;
        log::info("[res] async_worker: loading: 0x%08x", res->id());
        if(res->step_load())
        {
            // managed state transition
            // resource is either LOADED or FAILED
            auto entry_it = std::find(current_job.desc.entries.begin(),
                                      current_job.desc.entries.end(),
                                      task_descriptor::entry{res->type_id(), res->id()}
                                      );
            bool is_root = entry_it != current_job.desc.entries.end();

            if(res->is_loaded())
            {
                log::info("[res] async_worker: loaded: 0x%08x", res->id());
                (is_root? current_job.state->results.root : current_job.state->results.deps).push_back(res);
                work_status.loaded_count++;
            }
            else if(res->is_failed())
            {
                log::info("[res] async_worker: failed: 0x%08x", res->id());
                (is_root? current_job.state->results.root : current_job.state->results.deps).push_back(res);
                work_status.failed_count++;
            }
            else
            {
                log::error("[res] async_worker: resource 0x%08x (0x%08x): neither loaded nor failed at end of load transition. dropping reference", res->id(), res->type_id());
            }

            auto curr_state = res->state();
            l_it = current_job.state->needs_load.erase(l_it);
        }
        else
        {
            // must yield
            log::info("[res] async_worker: loading: yield: 0x%08x", res->id());
            ++l_it;
        }
    }

    std::vector<std::shared_ptr<resource>> new_preloads;
    auto p_it = current_job.state->needs_preload.begin();
    while(p_it != current_job.state->needs_preload.end())
    {
        auto &res = *p_it;
        log::info("[res] async_worker: preloading: 0x%08x", res->id());
        if(res->step_preload())
        {
            // managed state transition
            // resource is at least preloaded
            // move to needs_load
            log::info("[res] async_worker: preloaded: 0x%08x", res->id());
            current_job.state->needs_load.push_back(res);
            auto curr_state = res->state();
            // gather dependencies
            if(auto deps = res->dependencies())
            {
                for(const auto &dep: *deps)
                {
                    new_preloads.emplace_back(rman().create(dep.first, dep.second));
                    log::info("[res] async_worker: new dep: 0x%08x", res->id());
                }
            }
            p_it = current_job.state->needs_preload.erase(p_it);
        }
        else
        {
            // must yield
            log::info("[res] async_worker: preloading: yield: 0x%08x", res->id());
            ++p_it;
        }
    }
    for(auto &np: new_preloads)
        current_job.state->needs_preload.emplace_back(std::move(np));
    // no need: new_preloads.clear();

    // recalc final state progress
    auto to_load = current_job.state->needs_preload.size() +
                   current_job.state->needs_load.size();
    auto loaded =  current_job.state->results.root.size() +
                   current_job.state->results.deps.size();
    auto total_discovered = to_load + loaded;
    work_status.progress = loaded / (float) total_discovered;
    work_status.done = loaded == total_discovered;

    // commit evolved task state
    {
        std::lock_guard<std::mutex> lock(_d->tasks_mutex);

        // preserve in-between cancellation state
        bool was_cancelled_interim = current_job.state->status.cancelled;
        work_status.cancelled = was_cancelled_interim || work_status.cancelled;

        // update status
        current_job.state->status = work_status;

        if (work_status.cancelled)
        {
            // this task has been canceled, remove from internal task list
            // also return true so that the job doesn't return to queue
            _d->tasks.erase(current_job.handle);
            return true;
        }
    }

    if(work_status.done)
        log::info("[res] async_worker: job done: %u", current_job.handle);

    return work_status.done;
}
