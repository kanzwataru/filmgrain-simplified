[[vk::image_format("rgba8")]]
RWTexture2D<float4> out_texture : register(u0, space1);

Texture2D<float4> in_raw_texture : register(t0, space0);
Texture2D<float4> noise_texture : register(t1, space0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	int w, h;
	out_texture.GetDimensions(w, h);

	int noise_w, noise_h;
	noise_texture.GetDimensions(noise_w, noise_h);

	if(id.x < w && id.y < h)
	{
		float3 rgb = in_raw_texture.Load(int3(id.xy, 0)).rgb;
		float4 noise_sample = noise_texture[id.xy % int2(noise_w, noise_h)];
		
		float3 out_rgb = step(noise_sample.r, rgb.r).rrr;

		out_texture[id.xy] = float4(out_rgb, 1.0);
	}
}
