#include "entt/core/fwd.hpp"
#include <newbase/res/async_load.hpp>

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>


using namespace nb;
using namespace nb::res::load;


// internal data

struct task_state
{
    bool    done {false};
    bool    cancelled {false};
    float   progress {0.f};
    std::uint64_t   total_count{0};
    std::uint64_t   preloaded_count{0};
    std::uint64_t   loaded_count{0};
    std::uint64_t   failed_count{0};
    task_results    results {};

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
    return task_status{
        state.done,
        state.cancelled,
        state.progress,
        state.total_count,
        state.preloaded_count,
        state.loaded_count,
        state.failed_count
    };
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
            it->second->cancelled = true;
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

    auto &task_state = *(it->second);

    if(!task_state.done)
        return {};

    // store final task status in results
    task_state.results = {
        task_state.done,
        task_state.cancelled,
        task_state.progress,
        task_state.total_count,
        task_state.preloaded_count,
        task_state.loaded_count,
        task_state.failed_count
    };

    // take over task result data and pointers
    auto ret {std::move(task_state.results)};

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
    bool work_initialized;
    typeof(task_state::loaded_count) work_loaded_count;
    typeof(task_state::preloaded_count) work_preloaded_count;
    typeof(task_state::failed_count) work_failed_count;
    {
        std::lock_guard<std::mutex> lock(_d->tasks_mutex);
        if (current_job.state->cancelled)
        {
            // this task has been canceled, remove from internal task list
            // also return true so that the job doesn't return to queue
            _d->tasks.erase(current_job.handle);
            return true;
        }
        // we can continue loading this
        // let's initialize our runtime counters from the current counters
        work_initialized =  current_job.state->initialized;
        work_loaded_count = current_job.state->loaded_count;
        work_preloaded_count = current_job.state->preloaded_count;
        work_failed_count = current_job.state->failed_count;
    }

    // now, we can do our actual fucking job
    // from here on, we can only touch local buffered variables
    // and the parts of task_state not used in the thread-safe API

    // check if we need to initialize
    if(!work_initialized)
    {
        // we need to take the root handles and add to our preload list
        // TODO
        work_initialized = true;
    }

    // update state
    // TODO

    // TODO recalc final state progress
    float progress = 0.5f;
    bool task_done =  progress == 1.0f; // BUG use counters, not a fuckking float

    // commit evolved task state
    {
        std::lock_guard<std::mutex> lock(_d->tasks_mutex);

        // update counters and other shared task state here
        current_job.state->initialized = work_initialized;
        current_job.state->preloaded_count = work_preloaded_count;
        current_job.state->loaded_count = work_loaded_count;
        current_job.state->failed_count = work_failed_count;
        current_job.state->done = task_done;

        if (current_job.state->cancelled)
        {
            // this task has been canceled, remove from internal task list
            // also return true so that the job doesn't return to queue
            _d->tasks.erase(current_job.handle);
            return true;
        }
        task_done = current_job.state->done || current_job.state->cancelled;
    }
    return task_done;
}
