[[vk::image_format("rgba8")]]
RWTexture2D<float4> out_texture : register(u0, space1);

Texture2D<float4> in_raw_texture : register(t0, space0);
Texture2D<float4> noise_texture : register(t1, space0);

/*
struct Parameters {
	int noise_tile_size;
};

cbuffer ParametersConstantBuffer : register(b0, space2) { Parameters params : packoffset(c0); }
*/

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

		uint grain_count = 0;

		for(int y = 0; y < FILTER_WIDTH; ++y) {
			for(int x = 0; x < FILTER_WIDTH; ++x) {
				grain_count += render_channel(rgb.r, (id.xy * FILTER_WIDTH + int2(x, y)) % noise_size, float4(1.0, 0.9, 0.75, 0.5));
			}
		}

		float reconstructed_value = float(grain_count) / float(FILTER_WIDTH * FILTER_WIDTH);

		float3 out_rgb = reconstructed_value.rrr;

		out_texture[id.xy] = float4(out_rgb, 1.0);
	}
}
