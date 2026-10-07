#pragma once

// Quick and dirty test cube

#include "newbase/render/shader.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <newbase/sys/render_gpu/pipelines.hpp>
#include <newbase/render/vertex.hpp>
#include <newbase/utility/glm.hpp>

using namespace nb;

// 24 Vertices (4 per face to maintain distinct normals & UV coordinates)
static const render::vertex3d CUBE_VERTICES[24] = {
    // Front Face (+Z)
    {{-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},

    // Back Face (-Z)
    {{ 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},

    // Top Face (+Y)
    {{-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},

    // Bottom Face (-Y)
    {{-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},

    // Right Face (+X)
    {{ 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{ 0.5f,  0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

    // Left Face (-X)
    {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}}
};

// 36 Indices (Counter-Clockwise winding)
static const uint16_t CUBE_INDICES[36] = {
    0,  1,  2,   2,  3,  0, // Front
    4,  5,  6,   6,  7,  4, // Back
    8,  9, 10,  10, 11,  8, // Top
    12, 13, 14,  14, 15, 12, // Bottom
    20, 21, 22,  22, 23, 20,  // Left
    16, 17, 18,  18, 19, 16, // Right
};

inline SDL_GPUBuffer* UploadStaticGPUBuffer(
    SDL_GPUDevice* device,
    SDL_GPUBufferUsageFlags usage,
    const void* data,
    Uint32 size_in_bytes)
{
    // 1. Create the destination GPU VRAM buffer
    SDL_GPUBufferCreateInfo buffer_info = {
        .usage = usage,
        .size = size_in_bytes
    };
    SDL_GPUBuffer* gpu_buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!gpu_buffer) {
        SDL_Log("Failed to create GPU buffer: %s", SDL_GetError());
        return nullptr;
    }

    // 2. Create a temporary upload staging buffer
    SDL_GPUTransferBufferCreateInfo transfer_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = size_in_bytes
    };
    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer_buffer) {
        SDL_Log("Failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUBuffer(device, gpu_buffer);
        return nullptr;
    }

    // 3. Map memory & copy CPU data into staging area
    void* target_ptr = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    SDL_memcpy(target_ptr, data, size_in_bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

    // 4. Record the upload copy pass
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation source = {
        .transfer_buffer = transfer_buffer,
        .offset = 0
    };
    SDL_GPUBufferRegion destination = {
        .buffer = gpu_buffer,
        .offset = 0,
        .size = size_in_bytes
    };

    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    // 5. Submit command buffer synchronously for initialization setup
    SDL_SubmitGPUCommandBuffer(cmd);

    // 6. Staging buffer can be released immediately once submitted
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

    return gpu_buffer;
}

inline nb::gpu::pipeline_desc create_cube_pipeline_desc(
    SDL_GPUShader* vert_shader,
    SDL_GPUShader* frag_shader,
    const render::shader_reflection &vert_refl)
{
    nb::gpu::pipeline_desc desc{};

    desc.vert = vert_shader;
    desc.frag = frag_shader;

    desc.setup_vertex_input(render::vertex_type_descriptor::descriptor_table()
                                .at(render::vertex_type::POS_NORM_UV_COL_3D), vert_refl);

    desc.cull_mode = SDL_GPU_CULLMODE_NONE;
    desc.blend = nb::render::blendmode::NONE; // Opaque pass
    desc.depth_test = true;               // Enable depth testing
    desc.depth_write = true;              // Enable depth writes
    desc.depth_compare_op = SDL_GPU_COMPAREOP_LESS;
    desc.depth_clip = true;

    return desc;
}

struct TransformData {
    glm::mat4 mvp;   // Model-View-Projection Matrix
    glm::mat4 model; // World Matrix for normal transformation
};

inline void UpdateCubeTransforms(
    TransformData& outTransforms,
    glm::vec3 cameraPos,
    float viewportWidth,
    float viewportHeight,
    float rotationDegrees = 0.0f)
{
    // 1. Model Matrix (Position, Rotation, Scale in World Space)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(rotationDegrees), glm::vec3(0.5f, 1.0f, 0.0f));

    // 2. View Matrix (Camera LookAt)
    glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f); // Origin
    glm::vec3 upVector     = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, upVector);

    // 3. Projection Matrix
    float aspectRatio = viewportWidth / viewportHeight;
    float fovYRadians = glm::radians(45.0f);
    float nearPlane   = 0.1f;
    float farPlane    = 100.0f;

    glm::mat4 proj = glm::perspective(fovYRadians, aspectRatio, nearPlane, farPlane);

    // 4. Assign to Uniform Struct
    outTransforms.model = model;
    outTransforms.mvp   = proj * view * model;

    // FLIP Y because SDL_GPU NDC has Y down?
    //outTransforms.mvp[1][1] *= -1.0f;
}
