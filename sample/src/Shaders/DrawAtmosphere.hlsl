/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

#include "Declarations.h"

HLSL_INIT_GLOBAL_BINDING_TABLE(1)

#include "Common.hlsl"

#include "Atmosphere.h"

/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h // 
/////////////////////////////////////////////////////

#if 0
RWTexture2D<float4> g_rw_full_lit_scene; // Current render resolution color target 
Texture2D<float4> g_gbuffer_full_depth; // Current GBuffer/depth in render resolution 
TextureCube<float4> g_atmosphere_lut; // Environment specular LUT 
SamplerState g_linear_sampler;
#endif

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint2 size;
    g_rw_full_lit_scene.GetDimensions(size.x, size.y);
    if (any(tid.xy >= size))
        return;
    float depth = g_gbuffer_full_depth.Load(int3(tid.xy, 0)).x;
    if (depth < 1.0f - 1.0e-4f)
         return;
    float2 uv = (float2(tid.xy) + float2(0.5f, 0.5f)) / float2(size.xy);
    float2 xy = uv * 2.0f - float2(1.0f, 1.0f);
    xy.y = -xy.y;
    float4 clip = float4(xy.xy, 1.0f, 1.0f);
    float4 pixelDir = mul(g_frame_info.perFrame.u_mCameraCurrViewProjInverse, clip);
    float3 dir = normalize(pixelDir.xyz);
    float3 transmittance;
    float3 environment_lookup = g_atmosphere_lut.SampleLevel(g_linear_sampler, dir, 0).xyz;
    g_rw_full_lit_scene[tid.xy] = environment_lookup.xyzz;
}