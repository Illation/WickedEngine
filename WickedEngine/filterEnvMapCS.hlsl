#include "globals.hlsli"
#include "ShaderInterop_Utility.h"


TEXTURECUBEARRAY(input, float4, TEXSLOT_UNIQUE0);
RWTEXTURE2DARRAY(output, float4, 0);

[numthreads(GENERATEMIPCHAIN_2D_BLOCK_SIZE, GENERATEMIPCHAIN_2D_BLOCK_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x < filterResolution.x && DTid.y < filterResolution.y)
	{
		float2 uv = (DTid.xy + 0.5f) * filterResolution_rcp.xy;
		float3 N = UV_to_CubeMap(uv, DTid.z);

		float3x3 tangentSpace = GetTangentSpace(N);

		float4 col = 0;

		for (uint i = 0; i < filterRayCount; ++i)
		{
			float2 hamm = hammersley2d(i, filterRayCount);
			float3 hemisphere = hemispherepoint_cos(hamm.x, hamm.y);
			float3 cone = mul(hemisphere, tangentSpace);
			cone = lerp(N, cone, filterRoughness);

			col += input.SampleLevel(sampler_linear_clamp, float4(cone, filterArrayIndex), 0);
		}
		col /= (float)filterRayCount;

		output[uint3(DTid.xy, DTid.z + filterArrayIndex * 6)] = col;
	}
}
