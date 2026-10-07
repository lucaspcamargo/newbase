#pragma once

#include <newbase/res/mesh.hpp>
#include <newbase/sys/render_gpu/copy_scope.hpp>
#include <newbase/log.hpp>
#include <SDL3/SDL_gpu.h>


namespace nb::gpu
{
    /// Our gpu buffer manager
    /// Right now it's just for meshes
    class buffer_manager
    {
    public:

        void init(SDL_GPUDevice *dev)
        {
            // nothing to do tbh
        }

        void teardown(SDL_GPUDevice *dev)
        {
            cleanup(dev);
        }

        void cleanup(SDL_GPUDevice *dev)
        {
            if(!m_delete.size())
                return;

            typeof(m_delete) local_delete {};

            {
                std::scoped_lock lock(m_delete_mtx);
                std::swap(local_delete, m_delete);
            }

            for(auto &buf_ptr : local_delete)
            {
                SDL_ReleaseGPUBuffer(dev, buf_ptr);
            }
        }

        bool prepare(SDL_GPUDevice *dev, SDL_GPUCommandBuffer *cmd, rmesh *mesh, SDL_GPUCopyPass *curr_cpy = nullptr)
        {
            if(mesh->uploaded)
                return true; // already up or no data

            if(mesh->rptr)
                _mesh_release(*mesh, this);

            if(!mesh->has_data())
            {
                log::error("[render_gpu] buffer_manager: prepare: mesh has no data!");
                mesh->uploaded = true;
                return false;
            }

            SDL_GPUBufferCreateInfo info
            {
                .usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX,
                .size = (uint32_t) mesh->data_size(),
                .props = 0
            };
            auto gbuf = SDL_CreateGPUBuffer(dev, &info);
            if(!gbuf)
            {
                log::error("[render_gpu] buffer_manager: prepare: cannot create buffer: %s", SDL_GetError());
                return false;
            }

            SDL_GPUTransferBufferCreateInfo tinfo
            {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = info.size,
                .props = 0
            };
            auto tbuf = SDL_CreateGPUTransferBuffer(dev, &tinfo);
            if(!tbuf)
            {
                log::error("[render_gpu] buffer_manager: prepare: cannot create transfer buffer: %s", SDL_GetError());
                SDL_ReleaseGPUBuffer(dev, gbuf);
                return false;
            }

            auto dst = SDL_MapGPUTransferBuffer(dev, tbuf, false);
            if(!dst)
            {
                log::error("[render_gpu] buffer_manager: prepare: cannot map transfer buffer: %s", SDL_GetError());
                SDL_ReleaseGPUBuffer(dev, gbuf);
                SDL_ReleaseGPUTransferBuffer(dev, tbuf);
                return false;
            }
            memcpy(dst, mesh->data(), mesh->data_size());
            SDL_UnmapGPUTransferBuffer(dev, tbuf);

            {
                copy_scope cpy {cmd, curr_cpy};
                SDL_GPUTransferBufferLocation loc_src = {
                    .transfer_buffer = tbuf,
                    .offset          = 0
                };
                SDL_GPUBufferRegion region_dst = {
                    .buffer          = gbuf,
                    .offset          = 0,
                    .size            = info.size
                };
                SDL_UploadToGPUBuffer(
                    cpy,
                    &loc_src,
                    &region_dst,
                    true
                );
            }

            SDL_ReleaseGPUTransferBuffer(dev, tbuf);

            mesh->on_delete_uptr = this;
            mesh->on_delete = _mesh_release;
            mesh->rptr = gbuf;
            mesh->uploaded = true;

            log::critical("UPLOADED :)");

            return true;
        }

        template<class It>
        bool prepare_multiple(SDL_GPUDevice * dev, SDL_GPUCommandBuffer *cmd, It begin, It end, SDL_GPUCopyPass *cpy = nullptr)
        {
            bool ret = true;
            copy_scope cpy_scope(cmd, cpy);
            for(auto it = begin; it!= end; it++)
                ret = ret && prepare(dev, cmd, &**it, cpy_scope);
            return ret;
        }

        bool bind(SDL_GPURenderPass *pass, rmesh *mesh)
        {
            if(!mesh || !mesh->uploaded)
                return false;

            SDL_GPUBuffer *buf = (SDL_GPUBuffer*) mesh->rptr;

            SDL_GPUBufferBinding vbo_binding = { .buffer = buf, .offset = 0 };
            SDL_BindGPUVertexBuffers(pass, 0, &vbo_binding, 1);

            SDL_GPUBufferBinding ibo_binding = { .buffer = buf, .offset = mesh->get_data_index_offset() };
            SDL_BindGPUIndexBuffer(pass, &ibo_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

            return true;
        }

    private:
        static void _mesh_release(rmesh& mesh, void *uptr)
        {
            if(!mesh.rptr)
                return;
            if(!uptr)
            {
                log::error("[render_gpu] buffer_manager: _mesh_release: no user data!");
                return;
            }

            SDL_GPUBuffer *curr = static_cast<SDL_GPUBuffer*>(mesh.rptr);
            buffer_manager *mgr = static_cast<buffer_manager*>(uptr);

            {
                // store block for deletion
                std::scoped_lock lock(mgr->m_delete_mtx);
                mgr->m_delete.push_back(curr);
            }
            mesh.rptr = nullptr;
            mesh.on_delete = nullptr;
            mesh.on_delete_uptr = nullptr;
        }

        std::vector<SDL_GPUBuffer*> m_delete {}; // GPU buffer deferred delete list
        std::mutex m_delete_mtx {};

    };

}
