#include <newbase/sys/render_gpu/buffers.hpp>
#include <newbase/render/vertex.hpp>

#include <SDL3/SDL_gpu.h>


using namespace nb;
using namespace nb::render;
using namespace nb::gpu;


/*

 * Example code for uploaading data from a mesh resource

 // Create unified buffer for vertices + indices
 SDL_GPUBufferCreateInfo buf_info{};
 buf_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
 buf_info.size  = static_cast<uint32_t>(mesh.m_data.size());

 SDL_GPUBuffer* gpu_buf = SDL_CreateGPUBuffer(device, &buf_info);

 // Direct memcpy to SDL_GPU Transfer Buffer
 SDL_GPUTransferBufferCreateInfo xfer_info{};
 xfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
 xfer_info.size  = buf_info.size;

 SDL_GPUTransferBuffer* xfer_buf = SDL_CreateGPUTransferBuffer(device, &xfer_info);

 uint8_t* map = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, xfer_buf, false));
 std::memcpy(map, mesh.m_data.data(), mesh.m_data.size());
 SDL_UnmapGPUTransferBuffer(device, xfer_buf);

 // Execute transfer copy pass
 SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
 SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

 SDL_GPUTransferBufferLocation src{ .transfer_buffer = xfer_buf, .offset = 0 };
 SDL_GPUBufferRegion dst{ .buffer = gpu_buf, .offset = 0, .size = buf_info.size };

 SDL_UploadToGPUBuffer(copy_pass, &src, &dst, false);

 SDL_EndGPUCopyPass(copy_pass);
 SDL_SubmitGPUCommandBuffer(cmd);
 SDL_ReleaseGPUTransferBuffer(device, xfer_buf);

 */
