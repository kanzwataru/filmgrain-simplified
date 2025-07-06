#include <SDL3/SDL_gpu.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../extern/stb_image.h"
#include "../extern/stb_image_write.h"

#include "../shader/hello.gen.h"

#define THREADCOUNT 8

static uint32_t calc_threadgroup(uint32_t count)
{
	return count / THREADCOUNT + ((count % THREADCOUNT) > 0 ? 1 : 0);
}

int main(int argc, char **argv)
{
	SDL_Init(SDL_INIT_VIDEO);

	int w = 1024;
	int h = 1024;

	SDL_Window *window = SDL_CreateWindow("filmgrain-simplified (dummy window)", 128, 128, SDL_WINDOW_VULKAN);
	SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);

	SDL_ClaimWindowForGPUDevice(device, window);

	SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(device, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD | SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = w * h * 4,
	});

	SDL_GPUComputePipeline *pipeline = SDL_CreateGPUComputePipeline(device, &(SDL_GPUComputePipelineCreateInfo){
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.code = &hello_shader_source[0],
		.code_size = sizeof(hello_shader_source),
		.entrypoint = "CSMain",
		.threadcount_x = 8,
		.threadcount_y = 8,
		.threadcount_z = 1,
		.num_readwrite_storage_textures = 1,
	});

	SDL_GPUTexture *out_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo) {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE,
		.width = w,
		.height = h,
		.layer_count_or_depth = 1,
		.num_levels = 1,
		.sample_count = SDL_GPU_SAMPLECOUNT_1,
	});

	SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUComputePass *compute_pass = SDL_BeginGPUComputePass(
		cmdbuf,
		&(SDL_GPUStorageTextureReadWriteBinding) { .texture = out_texture },
		1,
		NULL,
		0
	);

	SDL_BindGPUComputePipeline(compute_pass, pipeline);
	SDL_DispatchGPUCompute(compute_pass, calc_threadgroup(w), calc_threadgroup(h), 1);

	SDL_EndGPUComputePass(compute_pass);

	SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmdbuf);

	SDL_DownloadFromGPUTexture(copy_pass,
		&(SDL_GPUTextureRegion) {
			.texture = out_texture,
			.w = w,
			.h = h,
			.d = 1,
		},
		&(SDL_GPUTextureTransferInfo) {
			.transfer_buffer = transfer_buffer
		}
	);
	
	SDL_EndGPUCopyPass(copy_pass);

	SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
	SDL_WaitForGPUFences(device, false, &fence, 1);
	SDL_ReleaseGPUFence(device, fence);

	void *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
	SDL_assert_always(data);

	if(!stbi_write_tga("out.tga", w, h, 4, data)) {
		fprintf(stderr, "Failed to write output texture\n");
		return 1;
	}

	SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

	return 0;
}
