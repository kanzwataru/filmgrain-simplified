#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
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
#include "../resources/resources.gen.h"

#define THREADCOUNT 8

#define MAX(a_, b_) ((a_) > (b_) ? (a_) : (b_))

static uint32_t calc_threadgroup(uint32_t count)
{
	return count / THREADCOUNT + ((count % THREADCOUNT) > 0 ? 1 : 0);
}

int main(int argc, char **argv)
{
	SDL_Init(SDL_INIT_VIDEO);

	// 0. Load data
	if(argc != 3) {
		fprintf(stderr, "filmgrain-simplified [in-image-path] [out-image-path]\n");
		fprintf(stderr, "\nNOTE: The input image should be 16-bit for best results.\n");
		return 1;
	}

	const char *in_path = argv[1];
	const char *out_path = argv[2];

	int w, h, c;
	void *in_texture_data = stbi_load_16(in_path, &w, &h, &c, 4);
	if(!in_texture_data) {
		fprintf(stderr, "Couldn't load image\n");
		return 1;
	}

	int noise_w, noise_h, noise_c;
	void *noise_texture_data = stbi_load_from_memory(LDR_RGBA_0_png_data, sizeof(LDR_RGBA_0_png_data), &noise_w, &noise_h, &noise_c, 4);
	if(!noise_texture_data) {
		fprintf(stderr, "Couldn't load noise texture\n");
		return 1;
	}

	// 1. Create GPU resources
	SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);

	SDL_GPUTransferBuffer *transfer_buffer = SDL_CreateGPUTransferBuffer(device, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD | SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
		.size = MAX(w, noise_w) * MAX(h, noise_h) * 4 * sizeof(uint16_t),
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
		.num_readonly_storage_textures = 2,
	});

	SDL_GPUTexture *in_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo) {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ,
		.width = w,
		.height = h,
		.layer_count_or_depth = 1,
		.num_levels = 1,
		.sample_count = SDL_GPU_SAMPLECOUNT_1,
	});

	SDL_GPUTexture *noise_texture = SDL_CreateGPUTexture(device, &(SDL_GPUTextureCreateInfo) {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ,
		.width = noise_w,
		.height = noise_h,
		.layer_count_or_depth = 1,
		.num_levels = 1,
		.sample_count = SDL_GPU_SAMPLECOUNT_1,
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

	// 2. Make the GPU do the stuff
	SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(device);

	// * Pass: Upload textures
	{
		// Upload the input texture
		SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmdbuf);

		void *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);
		SDL_assert_always(data);
		memcpy(data, in_texture_data, w * h * 4 * sizeof(uint16_t));
		SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

		SDL_UploadToGPUTexture(copy_pass,
			&(SDL_GPUTextureTransferInfo) {
				.transfer_buffer = transfer_buffer,
			},
			&(SDL_GPUTextureRegion) {
				.texture = in_texture,
				.mip_level = 0,
				.layer = 0,
				.w = w,
				.h = h,
				.d = 1,
			},
			false
		);

		SDL_EndGPUCopyPass(copy_pass);
	}

	{
		// Upload the noise texture
		SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmdbuf);

		void *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, true);
		SDL_assert_always(data);
		memcpy(data, noise_texture_data, noise_w * noise_h * 4);
		SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

		SDL_UploadToGPUTexture(copy_pass,
			&(SDL_GPUTextureTransferInfo) {
				.transfer_buffer = transfer_buffer,
			},
			&(SDL_GPUTextureRegion) {
				.texture = noise_texture,
				.mip_level = 0,
				.layer = 0,
				.w = noise_w,
				.h = noise_h,
				.d = 1,
			},
			false
		);

		SDL_EndGPUCopyPass(copy_pass);
	}

	// * Pass: Dispatch compute
	{
		SDL_GPUComputePass *compute_pass = SDL_BeginGPUComputePass(
			cmdbuf,
			&(SDL_GPUStorageTextureReadWriteBinding) { .texture = out_texture },
			1,
			NULL,
			0
		);

		SDL_BindGPUComputePipeline(compute_pass, pipeline);
		SDL_BindGPUComputeStorageTextures(compute_pass, 0, &in_texture, 1);
		SDL_BindGPUComputeStorageTextures(compute_pass, 1, &noise_texture, 1);
		SDL_DispatchGPUCompute(compute_pass, calc_threadgroup(w), calc_threadgroup(h), 1);

		SDL_EndGPUComputePass(compute_pass);
	}

	// * Pass: Readback the texture from the GPU
	{
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
	}

	// * Submit Command Buffer & Wait
	SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
	SDL_WaitForGPUFences(device, false, &fence, 1);
	SDL_ReleaseGPUFence(device, fence);

	// 3. Write out image
	void *data = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
	SDL_assert_always(data);

	if(!stbi_write_tga(out_path, w, h, 4, data)) {
		fprintf(stderr, "Failed to write output texture\n");
		return 1;
	}

	SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

	// Not releasing resources on purpose, since this is a one-shot command-line program that exits immediately!
	// ...

	return 0;
}
