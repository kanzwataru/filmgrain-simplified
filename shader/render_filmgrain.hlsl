/*
 * This is the shader that renders the image using film grain simulation.
 *
 * Note that even though it is a compute shader, no compute-specific functionality was used.
 * Presumably this could be made faster using shared memory tricks, but it was kept simple for demonstration purposes.
 * This also means you could take this code and put it in a WebGL fragment shader if you'd like.
*/
[[vk::image_format("rgba8")]]
RWTexture2D<float4> out_texture : register(u0, space1);

Texture2D<float4> in_raw_texture : register(t0, space0);
Texture2D<float4> noise_texture : register(t1, space0);

struct Uniforms {
	int2 noise_offsets_r;
	int2 noise_offsets_g;

	int2 noise_offsets_b;
	int noise_tile_size;
	int grayscale;

	float4 layer_weights;

	float3 base_color;
	float use_base_color;
};

cbuffer UniformsConstantBuffer : register(b0, space2) { Uniforms u : packoffset(c0); }

// This function renders one channel as film grain.
// * This is not called once per pixel, but multiple times for each "sub-pixel" of the noise tile (see below)
// * This takes in the brightness value of the pixel.
// -> It returns 1 or 0, depending on whether or not a grain is visible in this sub-pixel.
uint render_channel(float value, int2 noise_coord, float4 layer_weights)
{
	// Sample the noise texture
	float4 noise_sample = noise_texture[noise_coord];

	// Apply the noise
	// * Most important thing to note: we "step" the noise by the value, giving us a binary value!
	// * Basically, this is conceptually the same as placing a certain number of grains,
	//   except that the positions are pre-calculated in the form of a noise texture.
	float noised_value = step(noise_sample.r, value * layer_weights.r);
	noised_value = max(noised_value, step(noise_sample.g, value * layer_weights.g));
	noised_value = max(noised_value, step(noise_sample.b, value * layer_weights.b));
	noised_value = max(noised_value, step(noise_sample.a, value * layer_weights.a));

	// Return whether we have any grains that were hit by light, in this coordinate of the film medium.
	return noised_value > 0.0 ? 1 : 0;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	// 1. Get dimensions
	int w, h;
	out_texture.GetDimensions(w, h);

	int noise_w, noise_h;
	noise_texture.GetDimensions(noise_w, noise_h);
	int2 noise_size = int2(noise_w, noise_h);

	// 2. Early-return if we're out of bounds (this can happen if the image resolution is not a multiple of threadgroup size)
	if(id.x >= w && id.y >= h)
	{
		return;
	}

	// 3. Load in image, convert to grayscale if needed
	float3 rgb = in_raw_texture.Load(int3(id.xy, 0)).rgb;

	if(u.grayscale) {
		rgb = dot(rgb, float3(0.299, 0.587, 0.114)).rrr;
	}

	// 4. Re-render the image using the film grain model

	// * This variable accumulates how many film grains are "hit by light" in this pixel.
	// * Basically we split each pixel up into sub-pixel tiles, and count how many film grains should be present.
	uint3 grain_count = 0;

	for(int y = 0; y < u.noise_tile_size; ++y) {
		for(int x = 0; x < u.noise_tile_size; ++x) {
			// Here we increment the grain count, for each channel individually
			// * The same noise texture is used per-channel, with different offsets. This helps have a more cohesive look.
			// * The grain size is dependent on the number of subdivisions per pixel, as well as the resolution of the original image.
			// 	 This could be improved to make these three things be independent. If the noise texture was an SDF texture for example.
			grain_count.r += render_channel(rgb.r, (id.xy * u.noise_tile_size + int2(x, y) + u.noise_offsets_r) % noise_size, u.layer_weights);
			grain_count.g += render_channel(rgb.g, (id.xy * u.noise_tile_size + int2(x, y) + u.noise_offsets_g) % noise_size, u.layer_weights);
			grain_count.b += render_channel(rgb.b, (id.xy * u.noise_tile_size + int2(x, y) + u.noise_offsets_b) % noise_size, u.layer_weights);
		}
	}

	// * Here we average the sub-pixel grain tile, to get a value from 0.0 to 1.0, which will be used as the output luminance of the image.
	// * Note we apply a (simplified) sRGB display transform here for the output.
	//   The input image color space wasn't accounted for, and the noise distribution is also not controlled for (which also gives it a curve),
	//   so this is a bit sloppy and would need improvement for use in a real product.
	float3 reconstructed_value = float3(grain_count) / float(u.noise_tile_size * u.noise_tile_size); // TODO: Should probably account for layer weight and count!
	float3 out_rgb = pow(reconstructed_value, 2.2);

	// 5. Write out the pixel value

	// * Output only one channel if we're grayscale, otherwise do film base color emulation
	if(u.grayscale) {
		out_rgb = out_rgb.rrr;
	}
	else {
		// NOTE: The black point manipulation for the base color is applied in gamma display space on purpose.
		float3 base_color = lerp(float3(0.0, 0.0, 0.0), u.base_color, u.use_base_color);
		out_rgb = lerp(base_color, float3(1.0, 1.0, 1.0), out_rgb);
	}

	out_texture[id.xy] = float4(out_rgb, 1.0);
}
