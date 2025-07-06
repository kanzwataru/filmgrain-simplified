[[vk::image_format("rgba8")]]
RWTexture2D<float4> out_texture : register(u0, space1);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	int w, h;
	out_texture.GetDimensions(w, h);

	float2 uv = float2(id.xy) / float2(w, h);

	out_texture[id.xy] = float4(uv, 0.5, 1.0);
}
