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

#include "../shader/render_filmgrain.gen.h"
#include "../resources/resources.gen.h"

#define THREADCOUNT 8

#define MAX(a_, b_) ((a_) > (b_) ? (a_) : (b_))
#define countof(x_) (sizeof(x_) / sizeof((x_)[0]))

static uint32_t calc_threadgroup(uint32_t count)
{
	return count / THREADCOUNT + ((count % THREADCOUNT) > 0 ? 1 : 0);
}

struct GPUUniforms {
	// NOTE: Keep this in-sync with same struct in 'render_filmgrain.hlsl' (and mind the alignment!)
	int32_t noise_offsets_r[2];
	int32_t noise_offsets_g[2];

	int32_t noise_offsets_b[2];
	int32_t noise_tile_size;
	int32_t grayscale;

	float layer_weights[4];

	float base_color[3];
	float use_base_color;
};

int main(int argc, char **argv)
{
	SDL_Init(SDL_INIT_VIDEO);

	// 0. Load data
	struct GPUUniforms uniforms = {
		.noise_tile_size = 4,
		.noise_offsets_r = { 0, 0 },
		.noise_offsets_g = { 6, 3 },
		.noise_offsets_b = { 1, 7 },
		.layer_weights = { 1.0f, 0.9f, 0.75f, 0.5f },
		.grayscale = 0,
		.base_color = { 0.005f, 0.009f, 0.014f },
		.use_base_color = 0.0f,
	};

	struct Argument {
		const char *flag_name;
		enum { INT_VALUE, FLOAT_VALUE } type;
		int count;
		void *ptr;
		const char *description;
	};

	struct Argument arguments[] = {
		{ "-noise_tile_size", .type = INT_VALUE, .count = 1, .ptr = &uniforms.noise_tile_size,
		  .description = "How many subpixels of noise to apply. For example 3 would be a 3x3 tile per pixel. The larger the number the finer the grain." },

		{ "-noise_offsets_r", .type = INT_VALUE, .count = 2, .ptr = &uniforms.noise_offsets_r[0],
		  .description = "How many pixels to shift the noise texture for the Red channel" },

		{ "-noise_offsets_g", .type = INT_VALUE, .count = 2, .ptr = &uniforms.noise_offsets_g[0], 
		  .description = "How many pixels to shift the noise texture for the Green channel"},

		{ "-noise_offsets_b", .type = INT_VALUE, .count = 2, .ptr = &uniforms.noise_offsets_b[0],
		  .description = "How many pixels to shift the noise texture for the Blue channel" },

		{ "-layer_weights", .type = FLOAT_VALUE, .count = 4, .ptr = &uniforms.layer_weights,
		  .description = "Four layers of film grain are applied, each one of them has the input value multiplied by this to simulate occlusion." },

		{ "-grayscale", .type = INT_VALUE, .count = 1, .ptr = &uniforms.grayscale,
		  .description = "Set this to 1 to treat the input image as grayscale (works on colour images as well). If the input is actually grayscale you should set this to avoid colourful film grain." },

		{ "-base_color", .type = FLOAT_VALUE, .count = 3, .ptr = &uniforms.base_color,
		  .description = "Simulating the emulsion base layer color, this sets the black point of the image. It is only applied if -use_base_color is enabled." },

		{ "-use_base_color", .type = FLOAT_VALUE, .count = 1, .ptr = &uniforms.use_base_color,
		  .description = "How much to blend in the base color (0.0 to 1.0)." },
	};

	int args_top = 1;

	if(argc < 3 || (argc == 2 && 0 == strcmp(argv[1], "--help"))) {
		fprintf(stderr, "filmgrain-simplified [in-image-path] [out-image-path] <flags>\n\n");
		
		fprintf(stderr, "in-image-path: The path to the input image. Can be 16-bit image.\n");
		fprintf(stderr, "out-image-path: The path to the output image.\n\n");

		for(int i = 0; i < countof(arguments); ++i) {
			//fprintf(stderr, "%s\n\t%s\n", arguments[i].flag_name, arguments[i].description);
			fprintf(stderr, "%s ", arguments[i].flag_name);
			if(arguments[i].type == INT_VALUE) {
				for(int j = 0; j < arguments[i].count; ++j) {
					fprintf(stderr, "%d ", ((int *)arguments[i].ptr)[j]);
				}
				fprintf(stderr, "\n");
			}
			else if(arguments[i].type == FLOAT_VALUE) {
				for(int j = 0; j < arguments[i].count; ++j) {
					fprintf(stderr, "%f ", ((float *)arguments[i].ptr)[j]);
				}
				fprintf(stderr, "\n");
			}
			fprintf(stderr, "\t%s\n\n", arguments[i].description);
		}

		return 1;
	}

	const char *in_path = argv[args_top++];
	const char *out_path = argv[args_top++];

	while(args_top < argc) {
		bool parsed = false;
		for(int i = 0; i < countof(arguments); ++i) {
			if(0 == strcmp(arguments[i].flag_name, argv[args_top])) {
				if(args_top + arguments[i].count >= argc) {
					fprintf(stderr, "%s takes %d numbers\n", arguments[i].flag_name, arguments[i].count);
					return 1;
				}

				++args_top;
				for(int j = 0; j < arguments[i].count; ++j) {
					if(arguments[i].type == INT_VALUE) {
						((int*)arguments[i].ptr)[j] = atoi(argv[args_top++]);
					}
					else if(arguments[i].type == FLOAT_VALUE) {
						((float*)arguments[i].ptr)[j] = atof(argv[args_top++]);
					}
				}

				parsed = true;
				break;
			}
		}

		if(!parsed) {
			fprintf(stderr, "Unknown argument: %s\n", argv[args_top]);
			return 1;
		}
	}

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
		.code = &render_filmgrain_shader_source[0],
		.code_size = sizeof(render_filmgrain_shader_source),
		.entrypoint = "CSMain",
		.threadcount_x = 8,
		.threadcount_y = 8,
		.threadcount_z = 1,
		.num_readwrite_storage_textures = 1,
		.num_readonly_storage_textures = 2,
		.num_uniform_buffers = 1,
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
		SDL_PushGPUComputeUniformData(cmdbuf, 0, &uniforms, sizeof(uniforms));
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
