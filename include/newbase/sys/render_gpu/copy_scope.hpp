#pragma once

#include <newbase/utility/mixins.hpp>
#include <SDL3/SDL_gpu.h>

namespace nb::gpu
{
    /**
     * An utility class for managing copy passes via RAII.
     * Takes an optional existing copy pass.
     * If it does not exist, creates a new one, and releases it on destruction.
     */
    class copy_scope : private pinned
    {
    public:

        /**
         * Obtains a reference to an existing copy pass, or creates
         * a managed copy pass if it doesn't exist
         */
        explicit copy_scope(SDL_GPUCommandBuffer *cmd, SDL_GPUCopyPass *cpy = nullptr) :
            m_cmd(cmd), m_ptr(cpy), m_owned(false)
        {
            if(!m_ptr && m_cmd)
            {
                m_ptr = SDL_BeginGPUCopyPass(m_cmd);
                m_owned = true;
            }
        }

        ~copy_scope()
        {
            if(m_owned)
                SDL_EndGPUCopyPass(m_ptr);
        }

        /**
         * Implicit conversion operator
         */
        operator SDL_GPUCopyPass*() const
        {
            return m_ptr;
        }

        /**
         * Gets the current copy pass explicitly
         */
        SDL_GPUCopyPass* get() const
        {
            return this->operator SDL_GPUCopyPass*();
        }

    private:
        SDL_GPUCommandBuffer *m_cmd;
        SDL_GPUCopyPass *m_ptr;
        bool m_owned;
    };
}
