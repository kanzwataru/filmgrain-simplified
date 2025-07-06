[[vk::image_format("rgba8")]]
RWTexture2D<float4> out_texture : register(u0, space1);

Texture2D<float4> in_raw_texture : register(t0, space0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	int w, h;
	out_texture.GetDimensions(w, h);

	if(id.x < w && id.y < h)
	{
		out_texture[id.xy] = in_raw_texture.Load(int3(id.xy, 0));
	}
}
