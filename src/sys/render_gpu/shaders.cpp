#include "SDL3/SDL_gpu.h"
#include <newbase/sys/render_gpu/shaders.hpp>
#include <newbase/log.hpp>

using namespace nb;
using namespace nb::gpu;


bool shader_manager::prepare(SDL_GPUDevice * dev, rshader *shader)
{
    if(shader->rptr)
        return true;

    SDL_GPUShaderStage stage = shader->meta.stage == render::shader_stage::FRAGMENT? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShaderCreateInfo info
    {
        shader->data.size(),
        shader->data.data(),
        "main",
        SDL_GPU_SHADERFORMAT_SPIRV,
        stage,
        shader->meta.num_samplers,
        shader->meta.num_storage_textures,
        shader->meta.num_storage_buffers,
        shader->meta.num_uniform_buffers,
        0
    };
    auto gpu_sh = SDL_CreateGPUShader(dev, &info);
    if(!gpu_sh)
    {
        log::warn("[render_gpu] shader: creation failed: 0x%08x", shader->id());
        return false;
    }

    shader->rptr = gpu_sh;
    shader->on_destroyed = _shader_destroy;
    shader->on_destroyed_uptr = this;
    return true;

}

void shader_manager::cleanup(SDL_GPUDevice * dev, gpu::pipeline_cache &pipelines)
{
    typeof(m_delete) local_delete {};

    {
        std::scoped_lock lock(m_delete_mtx);
        std::swap(local_delete, m_delete);
    }

    for(auto &shader_ptr : local_delete)
    {
        // we need to evict the pipelines using this shader from the PSO cache
        pipelines.evict_by_shader(dev, shader_ptr);
        SDL_ReleaseGPUShader(dev, shader_ptr);
    }
}

void shader_manager::teardown(SDL_GPUDevice * dev, gpu::pipeline_cache &pipelines)
{
    cleanup(dev, pipelines);
}

void shader_manager::_shader_destroy(rshader& shader, void *uptr)
{
    if(!shader.rptr)
        return;

    if(!uptr)
    {
        log::error("[render_gpu] texture_manager: _texture_destroy: no user data!");
        return;
    }

    shader_manager *mgr = static_cast<shader_manager*>(uptr);
    {
        // store block for deletion
        std::scoped_lock lock(mgr->m_delete_mtx);
        mgr->m_delete.push_back(static_cast<SDL_GPUShader*>(shader.rptr));
    }
    shader.rptr = nullptr;
    shader.on_destroyed = nullptr;
    shader.on_destroyed_uptr = nullptr;
}
