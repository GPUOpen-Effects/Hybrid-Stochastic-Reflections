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

/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h // 
/////////////////////////////////////////////////////

#if 0
Texture2D<float4> g_gbuffer_full_depth;
SamplerState g_wrap_linear_sampler;
Texture2D<float4> g_decal_albedo;
Texture2D<float4> g_gbuffer_full_normal;
Texture2D<float4> g_gbuffer_full_roughness;
RWTexture2D<float4> g_rw_full_lit_scene;
SamplerComparisonState g_cmp_sampler; 
#endif

// Declarations needed for shading and shadow filtering
#define brdfTexture g_textures[GDT_TEXTURES_BRDF_LUT_SLOT]
#define diffuseCube g_ctextures[GDT_CTEXTURES_ATMOSPHERE_DIF_SLOT]
#define specularCube g_ctextures[GDT_CTEXTURES_ATMOSPHERE_LUT_SLOT]
#define myPerFrame g_frame_info.perFrame
#define ID_shadowMap_declared
#define SAMPLE_SHADOW_MAP(shadowIndex, UV, Z, IJ)                                                                                                                                  \
    (g_textures[NonUniformResourceIndex(GDT_TEXTURES_SHADOW_MAP_BEGIN_SLOT + shadowIndex)].SampleCmpLevelZero(g_cmp_sampler, (UV).xy, (Z), IJ.xy).r)

// Useful functions
#include "functions.hlsl"
// Shadow filtering for shading new fragments
#include "shadowFiltering.h"
// Code for shading new fragments
#include "RTShading.h"

float FFX_SSSR_LoadDepth(int2 pixel_coordinate) { return g_gbuffer_full_depth.Load(int3(pixel_coordinate, 0)); }

[numthreads(8, 8, 1)] void main(uint3 dispatch_thread_id
                                  : SV_DispatchThreadID) {

    uint2  screen_size                     = uint2(g_frame_info.base_width, g_frame_info.base_height);
    float2 uv                              = float2(dispatch_thread_id.xy + 0.5) / float2(screen_size);
    float  z                               = FFX_SSSR_LoadDepth(dispatch_thread_id.xy);
    if (FFX_DNSR_Reflections_IsBackground(z))
        return;
    float3 screen_uv_space_ray_origin      = float3(uv, z);
    float3 view_space_point                = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 view_space_ray_direction        = normalize(view_space_point);
    float3 world_space_point               = mul(float4(view_space_point, 1), g_inv_view).xyz;
    float3 world_space_direction           = normalize(mul(float4(view_space_ray_direction, 0), g_inv_view).xyz);
    if (world_space_point.x < 2.0 && world_space_point.x > 0.9 && all(abs(world_space_point.xyz) < 100.0f))
    {
        float2 uv = (world_space_point.zy + float2(0.5 - g_frame_info.simulation_time * 0.1f, -0.2)) * 1.0f * float2(1.0f, -1.0f);
        uint2 size;
        g_decal_albedo.GetDimensions(size.x, size.y);
        float ratio = float(size.x) / float(size.y);
        uv = uv * float2(1.0, ratio);
        if (uv.y < 0.0 || uv.y > 1.0) return;
        // uv. = saturate(uv.y);
        if (frac(uv.x * 0.5) > 0.5) return;
        float4 albedo = g_decal_albedo.SampleLevel(g_wrap_linear_sampler, uv, abs(view_space_point.z) * 0.1f);
        float alpha = albedo.a;//max(albedo.x, max(albedo.y, albedo.z));

        // float3 central_normal       = normalize(g_gbuffer_full_normal.Load(int3(dispatch_thread_id.xy, 0)).xyz * 2.0f - 1.0f);
        // float4 central_roughness    = g_gbuffer_full_roughness.Load(int3(dispatch_thread_id.xy, 0));
        // ShadingInfo local_basis;
        // local_basis.WorldPos  = world_space_point;
        // local_basis.Normal    = central_normal;
        // local_basis.View      = -world_space_direction;
        // local_basis.Albedo    = float3(0.0f, 0.0f, 0.0f);
        // local_basis.Roughness = 1.0f;
        // local_basis.Metalness = 0.0f;
        // float3 radiance       = doPbrLighting(local_basis, myPerFrame) + float3(2.3, 4.0, 1.4);

        g_rw_full_lit_scene[dispatch_thread_id.xy] = lerp(g_rw_full_lit_scene[dispatch_thread_id.xy], float4(albedo.xyz * 10.0, 1.0f), alpha);
    }
    
}