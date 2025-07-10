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

#define FILTER_WIDTH 3

uint render_channel(float value, int2 noise_coord, float4 layer_weights)
{
	float4 noise_sample = noise_texture[noise_coord];

	float noised_value = step(noise_sample.r, value * layer_weights.r);
	noised_value = max(noised_value, step(noise_sample.g, value * layer_weights.g));
	noised_value = max(noised_value, step(noise_sample.b, value * layer_weights.b));
	noised_value = max(noised_value, step(noise_sample.a, value * layer_weights.a));

	return noised_value > 0.0 ? 1 : 0;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	int w, h;
	out_texture.GetDimensions(w, h);

	int noise_w, noise_h;
	noise_texture.GetDimensions(noise_w, noise_h);
	int2 noise_size = int2(noise_w, noise_h);

	if(id.x < w && id.y < h)
	{
		float3 rgb = in_raw_texture.Load(int3(id.xy, 0)).rgb;

		if(u.grayscale) {
			rgb = dot(rgb, float3(0.299, 0.587, 0.114)).rrr;
		}

		uint3 grain_count = 0;

		for(int y = 0; y < u.noise_tile_size; ++y) {
			for(int x = 0; x < u.noise_tile_size; ++x) {
				grain_count.r += render_channel(rgb.r, (id.xy * u.noise_tile_size + int2(x, y) + u.noise_offsets_r) % noise_size, u.layer_weights);
				grain_count.g += render_channel(rgb.g, (id.xy * u.noise_tile_size + int2(x, y) + u.noise_offsets_g) % noise_size, u.layer_weights);
				grain_count.b += render_channel(rgb.b, (id.xy * u.noise_tile_size + int2(x, y) + u.noise_offsets_b) % noise_size, u.layer_weights);
			}
		}

		float3 reconstructed_value = float3(grain_count) / float(u.noise_tile_size * u.noise_tile_size); // TODO: Should probably account for layer weight!
		float3 out_rgb = pow(reconstructed_value, 2.2);

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
}
