#include "newbase/res/resource.hpp"
#include <mutex>

namespace nb
{

    bool resource::step_preload()
    {
        // Attempt to claim preloading responsibility
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);

            resource_state current = state();

            // Already past preloading stage
            if (current >= resource_state::PRELOADED && current != resource_state::FAILED) {
                return true;
            }
            if (current == resource_state::FAILED) {
                // there could be special handlng for failed, but alas
                return true;
            }

            if (m_state == resource_state::CREATED) {
                m_state.store(resource_state::PRELOADING, std::memory_order_release);
            } else if (m_state == resource_state::PRELOADING) {
                // Another thread is preloading right now -> YIELD!
                return false;
            } else if (m_state >= resource_state::PRELOADED) {
                return true;
            }
        }

        // Perform actual work OUTSIDE the lock
        bool success = do_preload();

        // Transition state and notify waiters
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.store(success ? resource_state::PRELOADED : resource_state::FAILED, std::memory_order_release);
        }
        m_cv.notify_all();

        return success;
    }

    bool resource::step_load()
    {
        // Step 1: Ensure resource is preloaded first
        if (!step_preload()) {
            return false; // Yield if preloading isn't ready
        }

        // Attempt to claim full loading responsibility
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);

            resource_state current = state();
            if (current == resource_state::LOADED || current == resource_state::FAILED) {
                return true;
            }

            if (m_state == resource_state::PRELOADED) {
                m_state.store(resource_state::LOADING, std::memory_order_release);
            } else if (m_state == resource_state::LOADING) {
                // Another thread is actively loading -> YIELD!
                return false;
            } else if (m_state == resource_state::LOADED) {
                return true; // already loading
            }
        }

        // Perform actual payload loading OUTSIDE the lock
        bool success = do_load();

        // Final state transition and notify waiters
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.store(success ? resource_state::LOADED : resource_state::FAILED, std::memory_order_release);
        }
        m_cv.notify_all();

        return success;
    }

    void resource::wait_until_loaded_or_reset()
    {
        if (is_loaded() || is_failed()) {
            return;
        }

        std::unique_lock<std::mutex> lock(m_state_mutex);
        m_cv.wait(lock, [this]() {
            resource_state s = m_state.load(std::memory_order_relaxed);
            return s == resource_state::LOADED || s == resource_state::FAILED;
        });
    }

    void resource::force_load_sync()
    {
        // loop ensures that if the resource is reset while we are waiting,
        // we try to load it again.
        while (!is_loaded() && !is_failed())
        {
            // Try to load it ourselves. If someone else is loading it,
            // or it has been reset, this returns false.
            if (!step_load())
            {
                // Sleep until the active thread finishes, fails, or resets.
                wait_until_loaded_or_reset();
            }
        }
    }

    bool resource::reset()
    {
        {
            std::unique_lock<std::mutex> lock(m_state_mutex);

            // if currently mid-step on a worker thread, wait for it to finish
            m_cv.wait(lock, [this]() {
                resource_state s = m_state.load(std::memory_order_relaxed);
                return s != resource_state::PRELOADING && s != resource_state::LOADING;
            });

            // if already in 'created' state, nothing to do.
            if (m_state.load(std::memory_order_relaxed) == resource_state::CREATED) {
                return true;
            }

            // deletage clearing to subclass
            do_unload();

            // commit state transition
            m_state.store(resource_state::CREATED, std::memory_order_release);
        }

        // we're done, notify any threads that might be waiting on transitions
        m_cv.notify_all();

        return true;
    }

} // namespace nb
