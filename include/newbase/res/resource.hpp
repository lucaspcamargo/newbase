#pragma once

#include <newbase/utility/mixins.hpp>
#include "entt/core/fwd.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>


//
//  Or base `resource` class
//
//  It has an atomic mechanism for controlling state transitions.
//  This lets resources be safely loaded from both the main thread and the async worker.
//  If a load attempt is made, but the load is already ongoing in another thread:
//   - If on the main thread, it blocks on the cv and waits for the load completion;
//   - If on a worker thread, it must yield (and work on other actionable resources).
//     The load will eventually be completed (or fail) somewhere else.
//  There are two standard load levels, PRELOADED amd LOADED.
//  A PRELOADED resource must be able to list its dependencies, but does not need to be fully ready.
//  A LOADED resource must be fully ready for usage, specific system interactions non-withstandding.
//  A FAILED resource could not read or parse its contents correctly, but must still remain
//   internally consistent.
//

// NOTE
// If we need hot-reloading at some later point, we do have a reset() operation.
// that has to put the resource back to the CREATED state and release resources.
// Resetting and reloading at a specific point on the main thread is a good starting point.
// We could add some sort of barrier on the async worker thread to prevent race conditions.
// On the other hand, we might not want to reload stuff on the main thread at all.
//
// One idea: dispatch a "special" loading operation in the background that does not touch
// the main resource cache. Once that is done we "swap" the data of the reloaded
// resources into the real ones, in a well-defined sync point in the main thread.
//
// Consider the header-only lib `filewatch` for the storage backend notifications.


namespace nb {

/**
 * Resource state enumeration.
 */
enum class resource_state
{
    /// a sentinel for the state of an invalid resource
    INVALID,

    /// resource object is created, but no loading has taken place
    CREATED,

    /// resource is ongoing a transition from created to preloading
    PRELOADING,

    /// resource object has obtained headers and metadata,
    /// and knows its the list of dependencies
    PRELOADED,

    /// resource is ongoing a transition from created to preloading
    LOADING,

    /// resource object is fully loaded and usable
    LOADED,

    /// resource failed to load
    FAILED
};


/**
 * @brief Base resource class. All resource types must inherit from this.
 * This class is pinned in memory, and cannot be copied nor moved.
 * The synchronization primitives that enabled background loading
 * require this.
 *
 * The general state machine transitions and behavior are implemented
 * in the base ::nb::resource class. Inheritors must override the virtual
 * functions for preloading and loading (do_*). This should allow them to function
 * correctly within the resource management framework.
 */
class resource : public std::enable_shared_from_this<resource>, private pinned {
public:

    virtual ~resource() = default;

    /// Getter for resource id
    entt::id_type id()      const { return _id; }

    /// Getter for resource type id
    entt::id_type type_id() const { return _type_id; }

    /// Convenience: resolve the entt meta type for this resource
    entt::meta_type meta_type() const { return entt::resolve(_type_id); }

    /// A resource dependency. A pair of (resource type id, resource id).
    /// We need to specify the resource type because loading requires
    /// knowing the desired resource type beforehand.
    using dependency_t = std::pair<entt::id_type, entt::id_type>;

    /// Returns a list of resource dependencies for loading.
    /// For usage when resource state is PRELOADED or LOADED.
    /// The resource system may use this information to structure loading
    /// of multiple resources in a single loading job.
    virtual const std::vector<dependency_t>* dependencies() const { return nullptr; } // TODO C++20: return a span

    /// Writes the data for a subresource into the given destination vector
    /// @returns Whether the subresource was found and if there's data for it
    virtual bool get_subresource_data(entt::id_type sub, std::vector<uint8_t>&) { return false; }

    /// Lock-free state query
    [[nodiscard]] resource_state state() const noexcept
    {
        return m_state.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_preloaded() const noexcept
    {
        const auto s = state();
        return s == resource_state::PRELOADED ||
               s == resource_state::LOADING ||
               s == resource_state::LOADED;
    }

    [[nodiscard]] bool is_loaded() const noexcept
    {
        return state() == resource_state::LOADED;
    }

    [[nodiscard]] bool is_failed() const noexcept
    {
        return state() == resource_state::FAILED;
    }


    // --- Cooperative Stepping (for async loader thread) ---

    /// Attempts to execute preloading step.
    /// @returns true if preloading is complete (or was already complete),
    ///          false if yielded because another thread is preloading.
    bool step_preload();

    /// Attempts to execute full loading step.
    /// @returns true if loading is complete (or was already complete),
    ///          false if yielded because another thread is loading.
    bool step_load();


    // --- Synchronous Execution & Waiting (for main thread loading) ---

    /// Blocks the caller thread until state reaches 'loaded' or 'failed'.
    void wait_until_loaded();

    /// Blocks the caller thread until state reaches 'preloaded', 'loaded', or 'failed'.
    void wait_until_preloaded();

    /// Forces immediate execution on caller thread if not already being processed,
    /// or blocks waiting for the worker to finish if already in progress.
    void force_load_sync();

    /// Forces immediate execution on caller thread if not already being processed,
    /// or blocks waiting for the worker to finish if already in progress.
    void force_preload_sync();

    // Releases a loaded resources' internal data, and puts it back into the CREATED state.
    // For usage on the main thread.
    // If a loader is currently operating on this resource, will block and wait
    // for completion before unloading data.
    bool reset();

protected:

    /// constructor to be used by specializations, sets up id and type_id
    explicit resource(entt::id_type id = 0, entt::id_type type_id = 0)
        : _id(id), _type_id(type_id) {}

    /// Subclasses override to parse headers, metadata, dependency trees, etc.
    virtual bool do_preload() { return true; }

    /// Subclasses override to perform heavy deserialization, decoding, and GPU uploads.
    virtual bool do_load() { return true; }

    /// Subclasses must use this to release loaded resources when reset() is invoked.
    /// TODO consider making this pure virtual, thus mandatory to be possible to reset
    ///      worth it IF and WHEN we are actually doing such a thing
    virtual void do_unload() { }

    /// Force-sets this resource state
    /// For usage with anonymous resources that are loaded in-line (e.g. rtexture::load_from)
    void force_state(resource_state state);


private:
    entt::id_type _id      {0};
    entt::id_type _type_id {0};
    std::atomic<resource_state> m_state{resource_state::CREATED};
    mutable std::mutex m_state_mutex;
    std::condition_variable m_cv;
};

} // namespace nb
