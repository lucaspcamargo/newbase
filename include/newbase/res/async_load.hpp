#pragma once

#include <newbase/res/resource.hpp>
#include <entt/core/fwd.hpp>
#include <cstdint>
#include <utility>
#include <vector>

/// FIRST PASS on async resource loading mechanism.
/// Will need to introduce some thread safety in resource manager caches.
/// Basically a worker thread with work queues and handles.

namespace nb::res::load
{

/// A handle type for a loadng job.
/// Resource loading steps are serialized.
/// Multiple load jobs can safely run at the same time,
/// and will be stepped in a round-robin fashion.
using task_handle = uint64_t;

/// Sentinel value for an invalid task.
constexpr task_handle TASK_INVALID = 0;


/// A descriptor for a resource loading job.
/// Multiple resources can be the "root" of the loader graph,
/// And resources are loaded for both.
struct task_descriptor
{
    /// A resource to load in this task.
    /// A pair containing the resource id and desired resource type.
    using entry = std::pair<entt::id_type, entt::id_type>;

    /// List of entries to load for this task
    std::vector<entry> entries {};
};


struct task_status
{
    /// Whether the task has been completed.
    bool done  {false};

    /// This loading task has been cancelled.
    bool cancelled {false};

    /// The current loading progress of the task.
    float progress {0.f};

    /// Count of all resources discovered for this task so far.
    /// Includes the root resources plus the dependecy tree.
    uint64_t total_count {0};

    /// Count of all resources preloaded
    uint64_t preloaded_count {0};

    /// Count of all resources fully loaded
    uint64_t loaded_count {0};

    /// Count of all resources that were listed but could not be found.
    uint64_t failed_count {0};
};


struct task_results
{
    // The final report of load task execution, for convenience
    task_status final_status {};

    // A list of shared pointers to the main entries of the loading task
    std::vector<std::shared_ptr<resource>> root;

    // A list of shred pointers to the dependencies of the main entries
    std::vector<std::shared_ptr<resource>> deps;
};


/// The async loader worker thread.
/// Right now we will design for a single worker thread,
/// as it should be mostly IO-bound.
/// POitRoAE
class worker final
{
public:

    worker();
    ~worker();

    /// Starts the worker thread.
    void run();

    /// Stops the worker thread.
    void stop();

    /// Starts a loading task.
    /// The descriptor is copied to an internal representation.
    /// Thread-safe.
    /// @param desc The task descriptor to use.
    /// @returns The handle for the newly-created task, or TASK_INVALID
    ///          if the descriptor is invalid.
    task_handle start(const task_descriptor &desc);

    /// Obtains current status for a loading task.
    /// Thread-safe.
    /// @param handle The handle for the task to get status for.
    /// @returns A status report structure for the currently loading task.
    task_status status(task_handle handle) const;

    /// Cancels an ongoing loading operation
    /// This will release the hold on all resource shared pointers of the task.
    /// This can also be done on completed tasks
    /// @param handle The handle for the task to cancel.
    /// @returns Whether the task is valid and was successfully cancelled.
    bool cancel(task_handle handle);

    /// Obtains the results of a loading task and unregisters it.
    /// If the task is not yet completed, this will return empty results,
    /// and do nothing.
    /// Thread-safe.
    /// @param handle The handle for the task to get results for.
    /// @returns A results structure with references to all loaded resources.
    task_results complete(task_handle handle);

private:
    struct worker_p;
    struct job;
    void worker_loop();
    bool step_job(const job& current_job);

    std::unique_ptr<worker_p> _d;
};




/// RAII wrapper for a loading task.
/// Automatically cancels the task on destruction unless detached or completed.
class task_ref : public nocopy
{
public:
    /// Default constructor creates an empty/invalid task reference.
    task_ref() = default;

    /// Constructs and immediately starts a loading task on the provided worker.
    task_ref(worker& w, const task_descriptor& desc)
    : m_worker(&w)
    , m_handle(m_worker->start(desc))
    {
    }

    /// Destructor cancels the loading task if still active.
    ~task_ref()
    {
        reset();
    }

    // Move semantics (transfer ownership of the active task handle)
    task_ref(task_ref&& other) noexcept
    : m_worker(std::exchange(other.m_worker, nullptr))
    , m_handle(std::exchange(other.m_handle, TASK_INVALID))
    {
    }

    task_ref& operator=(task_ref&& other) noexcept
    {
        if (this != &other) {
            reset(); // Cancel current task if active before taking ownership
            m_worker = std::exchange(other.m_worker, nullptr);
            m_handle = std::exchange(other.m_handle, TASK_INVALID);
        }
        return *this;
    }

    /// Checks if this handle points to a valid active task.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_worker != nullptr && m_handle != TASK_INVALID;
    }

    /// Explicit boolean conversion (same as valid()).
    explicit operator bool() const noexcept
    {
        return valid();
    }

    /// Obtains the current status of the task.
    [[nodiscard]] task_status status() const
    {
        if (!valid()) {
            return {};
        }
        return m_worker->status(m_handle);
    }

    /// Convenience check for completion without consuming the task.
    [[nodiscard]] bool is_done() const
    {
        return status().done;
    }

    /// Obtains results and unregisters the task.
    /// If successful, releases ownership so the destructor won't attempt to cancel.
    task_results complete()
    {
        if (!valid()) {
            return {};
        }

        task_results res = m_worker->complete(m_handle);

        // If complete succeeded (returned a completed task's results), release the handle
        if (res.final_status.done) {
            m_handle = TASK_INVALID;
            m_worker = nullptr;
        }

        return res;
    }

    /// Explicitly cancels the task and invalidates this reference.
    /// @returns whether the cancel was successful.
    bool cancel()
    {
        if (!valid()) {
            return false;
        }

        bool success = m_worker->cancel(m_handle);
        m_handle = TASK_INVALID;
        m_worker = nullptr;
        return success;
    }

    /// Detaches the task from this RAII wrapper.
    /// The worker will continue processing the task, but this wrapper will no longer auto-cancel it.
    /// @returns The underlying task_handle.
    task_handle detach() noexcept
    {
        m_worker = nullptr;
        return std::exchange(m_handle, TASK_INVALID);
    }

    /// Resets the wrapper, cancelling any ongoing task.
    void reset()
    {
        if (valid()) {
            m_worker->cancel(m_handle);
            m_handle = TASK_INVALID;
            m_worker = nullptr;
        }
    }

    /// Gets the raw task handle.
    [[nodiscard]] task_handle handle() const noexcept
    {
        return m_handle;
    }

private:
    worker* m_worker{nullptr};
    task_handle m_handle{TASK_INVALID};
};

}

