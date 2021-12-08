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

#include "ffx_denoiser_reflections_common.h"

/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h // 
/////////////////////////////////////////////////////

#if 0
RWTexture2D<float4> g_rw_debug;
Texture2D<min16float3> g_radiance_0;
Texture2D<float4> g_gbuffer_full_roughness;
Texture2D<float4> g_gbuffer_normal;
Texture2D<float4> g_gbuffer_depth;
#endif

struct PushConstants {
  uint clear;
  uint pad0;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc : DX12_PUSH_CONSTANTS;

float3 EvalGGX(float3 N, float3 V, float3 L, float roughness, float3 specularColor, out float pdf) {
  float3 F0  = specularColor;
  float3 H   = normalize(V + L);
  float  NoL = saturate(dot(N, L));
  float  NoH = saturate(dot(N, H));
  float  NoV = saturate(dot(N, V));
  float  VoH = saturate(dot(V, H));
  float  LoH = saturate(dot(L, H));
  pdf   = (NoH * NoV) / VoH;
  pdf   = max(pdf, 1.0e-3f);
  float  alpha = roughness * roughness;
  float3 F     = F0 + ((1.0f).xxx - F0) * pow(1.0f - NoV, 5.0f);
  float K = 0.5 * alpha;
  float G = (NoL * NoV) / ((NoL * (1.0 - K) + K) * (NoV * (1.0 - K) + K));
  if (roughness < 0.02f) return 1.0f;
  return
  // Omit the fresnel factor
  // F *
  G;
}

[numthreads(8, 8, 1)]
void main(int2 group_id : SV_GroupID, uint group_index : SV_GroupIndex) {
    int2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    int2 dispatch_thread_id = group_id * 8 + group_thread_id;

    uint2 screen_size = uint2(g_frame_info.base_width, g_frame_info.base_height);
    uint2 reflection_size = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);

    int2 xy = dispatch_thread_id.xy;
    float3 radiance = g_radiance_0[xy].xyz;

    if (pc.clear) {
        g_rw_debug[xy].xyzw = (0.0f).xxxx;
    }
    if (g_hsr_mask & HSR_FLAGS_INTERSECTION_ACCUMULATE) {
        float4 specularRoughness    = g_gbuffer_full_roughness.Load(int3(dispatch_thread_id, 0));
        float3 specularColor        = specularRoughness.xyz;
        float roughness             = specularRoughness.w;
        uint2 screen_size           = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
        float2 uv                   = float2(dispatch_thread_id + 0.5) / float2(screen_size);
        float3 world_space_normal   = normalize(g_gbuffer_normal.Load(int3(dispatch_thread_id, 0)) * 2.0f - (1.0f).xxx);
        float z                     = g_gbuffer_depth.Load(int3(dispatch_thread_id, 0));

        if (FFX_DNSR_Reflections_IsBackground(z)) {
            g_rw_debug[xy].xyzw = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }

        float3 screen_uv_space_ray_origin      = float3(uv, z);
        float3 view_space_ray                  = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
        float3 view_space_surface_normal       = mul(float4(normalize(world_space_normal), 0), g_view).xyz;
        float3 view_space_ray_direction        = normalize(view_space_ray);
        float3 view_space_reflected_direction  = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, dispatch_thread_id);
        screen_uv_space_ray_origin             = ProjectPosition(view_space_ray, g_proj);
        float3 screen_space_ray_direction      = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, g_proj);
        float3 world_space_reflected_direction = mul(float4(view_space_reflected_direction, 0), g_inv_view).xyz;
        float3 world_space_ray_direction       = mul(float4(view_space_ray_direction, 0), g_inv_view).xyz;

        if (dot(world_space_normal, world_space_reflected_direction) < 0.0f)
          return;

        float pdf;
        float3 brdf = EvalGGX(world_space_normal, -world_space_ray_direction, world_space_reflected_direction, roughness, specularColor, pdf);
        g_rw_debug[xy].xyzw += float4(radiance * 1.0f / pdf, 1.0f / pdf);
    } else {
        g_rw_debug[xy].xyzw = float4(radiance, 1.0f);
    }
}
