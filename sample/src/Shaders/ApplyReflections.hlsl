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
Texture2D<float4> g_gbuffer_depth;
Texture2D<float4> g_gbuffer_normal;
Texture2D<float4> g_gbuffer_normal_history;
Texture2D<float4> g_motion_vector;
Texture2D<min16float3> g_radiance_0;
Texture2D<float4> g_brdf_lut;
SamplerState g_linear_sampler;
Texture2D<float4> g_gbuffer_full_roughness;
Texture2D<float4> g_gbuffer_full_depth;
RWTexture2D<float4> g_rw_full_lit_scene;
Texture2D<float4> g_debug;
#endif

struct PushConstants {
    uint4 easu_const0;
    uint4 easu_const1;
    uint4 easu_const2;
    uint4 easu_const3;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc : DX12_PUSH_CONSTANTS;


//////////////////////////////////
///// Load/Store Interface ///////
//////////////////////////////////

float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) { return g_gbuffer_depth.Load(int3(pixel_coordinate, 0)).x; }
float3 FFX_DNSR_Reflections_LoadWorldSpaceNormal(int2 pixel_coordinate) { return 2 * g_gbuffer_normal.Load(int3(pixel_coordinate, 0)).xyz - 1; }
float3 FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(int2 pixel_coordinate) { return 2 * g_gbuffer_normal_history.Load(int3(pixel_coordinate, 0)).xyz - 1; }
float2 FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate) { return g_motion_vector.Load(int3(pixel_coordinate, 0)).xy * float2(0.5, -0.5); }

//////////////////////////////////
//////////////////////////////////
//////////////////////////////////

#ifndef IN_RADIANCE

#    define IN_RADIANCE g_radiance_0

#endif

#define DIELECTRIC_SPECULAR 0.04f

// Important bits from the PBR shader
float3 getIBLContribution(float perceptualRoughness, float3 specularColor, float3 specularLight, float3 n, float3 v) {
    float  NdotV           = clamp(dot(n, v), 0.0, 1.0);
    float  fs              = pow(1.0f - NdotV, 5.0f);
    float3 kS              = specularColor + ((1.0f).xxx - specularColor) * fs;
    float2 brdfSamplePoint = clamp(float2(NdotV, perceptualRoughness), float2(0.0, 0.0), float2(1.0, 1.0));
    // retrieve a scale and bias to F0. See [1], Figure 3
    float2 brdf = g_brdf_lut.SampleLevel(g_linear_sampler, brdfSamplePoint, 0.0f).rg;

    float3 specular = specularLight * (kS * brdf.x + brdf.y);
    return specular;
}

#define A_GPU 1
#define A_HLSL 1
;
#define LUMA_POWER 0.1f
#define ILUMA_POWER 10.0f

#include "ffx_a.h"
#define FSR_EASU_F 1

AF4 FsrEasuRF(AF2 p) {
    AF4 res = pow(IN_RADIANCE.GatherRed(g_linear_sampler, p, int2(0, 0)), LUMA_POWER);
    return res;
}
AF4 FsrEasuGF(AF2 p) {
    AF4 res = pow(IN_RADIANCE.GatherGreen(g_linear_sampler, p, int2(0, 0)), LUMA_POWER);
    return res;
}
AF4 FsrEasuBF(AF2 p) {
    AF4 res = pow(IN_RADIANCE.GatherBlue(g_linear_sampler, p, int2(0, 0)), LUMA_POWER);
    return res;
}
#include "ffx_fsr1.h"

float3 upscale(float2 uv, int2 coord) {
    AU4 Const0 = pc.easu_const0;
    AU4 Const1 = pc.easu_const1;
    AU4 Const2 = pc.easu_const2;
    AU4 Const3 = pc.easu_const3;
    float3 rgb;
    FsrEasuF(/* out */ rgb, /* in */ coord, /* in */ Const0, /* in */ Const1, /* in */ Const2, /* in */ Const3);
    return pow(rgb, ILUMA_POWER);
}

groupshared uint g_ffx_dnsr_shared_0[16][16];
groupshared uint g_ffx_dnsr_shared_1[16][16];

#include "ffx_denoiser_reflections_common.h"

struct FFX_DNSR_Reflections_NeighborhoodSample {
    min16float3 normal;
};

FFX_DNSR_Reflections_NeighborhoodSample FFX_DNSR_Reflections_LoadFromGroupSharedMemory(int2 idx) {
    uint2 packed_normal = uint2(g_ffx_dnsr_shared_0[idx.y][idx.x], g_ffx_dnsr_shared_1[idx.y][idx.x]);
    min16float4 unpacked_normal = FFX_DNSR_Reflections_UnpackFloat16_4(packed_normal);
    FFX_DNSR_Reflections_NeighborhoodSample sample;
    sample.normal    = normalize(unpacked_normal.xyz);
    return sample;
}


void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 group_thread_id, min16float3 normal) {
    g_ffx_dnsr_shared_0[group_thread_id.y][group_thread_id.x] = FFX_DNSR_Reflections_PackFloat16(normal.xy);
    g_ffx_dnsr_shared_1[group_thread_id.y][group_thread_id.x] = FFX_DNSR_Reflections_PackFloat16(normal.zz);
}

void FFX_DNSR_Reflections_LoadNeighborhood(
    int2 pixel_coordinate,
    int2 screen_size,
    out min16float3 normal) {
    float2 uv = (float2(pixel_coordinate) + (0.5f).xx) / screen_size;
    normal = normalize(g_gbuffer_normal.SampleLevel(g_linear_sampler, uv, 0.0f).xyz * 2.0f - 1.0f);
}

void FFX_DNSR_Reflections_InitializeGroupSharedMemory(int2 dispatch_thread_id, int2 group_thread_id, int2 screen_size) {
    // Load 16x16 region into shared memory using 4 8x8 blocks.
    int2 offset[4] = { int2(0, 0), int2(8, 0), int2(0, 8), int2(8, 8) };

    // Intermediate storage registers to cache the result of all loads
    min16float3 normal[4];

    // Start in the upper left corner of the 16x16 region.
    dispatch_thread_id -= 4; 

    // First store all loads in registers
    for(int i = 0; i < 4; ++i)
    {
        FFX_DNSR_Reflections_LoadNeighborhood(dispatch_thread_id + offset[i], screen_size, normal[i]);
    }
    
    // Then move all registers to groupshared memory
    for(int j = 0; j < 4; ++j)
    {
        FFX_DNSR_Reflections_StoreInGroupSharedMemory(group_thread_id + offset[j], normal[j]);
    }
}

void FFX_DNSR_Reflections_ApplyReflections(int2 dispatch_thread_id, int2  screen_size, float3 radiance) {
    float4 central_roughness    = g_gbuffer_full_roughness.Load(int3(dispatch_thread_id, 0));
    float  central_depth        = g_gbuffer_full_depth.Load(int3(dispatch_thread_id, 0)).x;
    if (FFX_DNSR_Reflections_IsBackground(central_depth))
        return;
    float2 uv                   = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
    float  central_linear_depth = FFX_DNSR_Reflections_GetLinearDepth(uv, central_depth);

#ifdef HSR_DEBUG
    if (g_hsr_mask & HSR_FLAGS_SHOW_INTERSECTION) {
        float2 uv                             = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
        float4 radiance_weight                = g_debug.SampleLevel(g_linear_sampler, uv, 0.0f);
        g_rw_full_lit_scene[dispatch_thread_id.xy] = (radiance_weight.xyz / radiance_weight.w).xyzz;
        return;
    }

    if (g_hsr_mask & HSR_FLAGS_SHOW_REFLECTION_TARGET) {
        // Show just the reflection view
        g_rw_full_lit_scene[dispatch_thread_id.xy] = float4(radiance, 0);
        return;
    }

    if (g_hsr_mask & HSR_FLAGS_SHOW_DEBUG_TARGET) {
        float2 uv                             = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
        float4 debug_value                    = float4(g_debug.SampleLevel(g_linear_sampler, uv, 0.0f).xyz, 1.0f);
        if (any(isinf(debug_value)) || any(isnan(debug_value)))
            g_rw_full_lit_scene[dispatch_thread_id.xy] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        else
            g_rw_full_lit_scene[dispatch_thread_id.xy] = debug_value;
        return;
    }
#endif

    if (g_hsr_mask & HSR_FLAGS_APPLY_REFLECTIONS) {
        float3 central_normal             = normalize(g_gbuffer_full_normal.Load(int3(dispatch_thread_id, 0)).xyz * 2.0f - 1.0f);
        float4 specularRoughness          = central_roughness;
        float3 specularColor              = specularRoughness.xyz;
        float  perceptualRoughness        = sqrt(specularRoughness.w); // specularRoughness.w contains alphaRoughness
        float3 screen_uv_space_ray_origin = float3(uv, central_depth);
        float3 view_space_ray             = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
        float3 view_space_ray_direction   = normalize(view_space_ray);
        float3 world_space_ray_direction  = mul(float4(view_space_ray_direction, 0), g_inv_view).xyz;
        radiance                          = getIBLContribution(perceptualRoughness, specularColor, radiance, central_normal, -world_space_ray_direction);
        g_rw_full_lit_scene[dispatch_thread_id.xy] += float4(radiance * g_frame_info.reflection_factor, 1); // Show the reflections applied to the scene
    } else {
        // Show just the scene
    }
}

[numthreads(8, 8, 1)] void main(int2 group_id : SV_GroupID,
                                uint group_index : SV_GroupIndex) {
    int2 group_thread_id    = FFX_DNSR_Reflections_RemapLane8x8(group_index); // Remap lanes to ensure four neighboring lanes are arranged in a quad pattern
    int2 dispatch_thread_id = group_id * 8 + group_thread_id;
    uint2  screen_size      = uint2(g_frame_info.base_width, g_frame_info.base_height);
    uint2  reflection_size  = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    
    // Here go different upscaling modes
#ifdef UPSCALE
    float3 radiance = (0.0f).xxx;
    if (g_frame_info.reflections_upscale_mode == 0) { // POINT
        float2 uv   = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
        radiance    = IN_RADIANCE.Load(int3(uv * reflection_size, 0)).xyz;
    } else if (g_frame_info.reflections_upscale_mode == 1) { // BILINEAR
        float2 uv   = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
        radiance    = IN_RADIANCE.SampleLevel(g_linear_sampler, uv, 0.0f).xyz;
    } else if (g_frame_info.reflections_upscale_mode == 2) { // FSR
        float2 uv   = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
        radiance    = upscale(uv, dispatch_thread_id);
    } else if (g_frame_info.reflections_upscale_mode == 3) { // FSR + Edge Shift
        FFX_DNSR_Reflections_InitializeGroupSharedMemory(dispatch_thread_id, group_thread_id, screen_size);
        GroupMemoryBarrierWithGroupSync();

        group_thread_id += 4; // Center threads in groupshared memory

        int2  coord_acc       = group_thread_id;
        float weight_acc      = 0.0f;
        float3 central_normal = normalize(g_gbuffer_full_normal.Load(int3(dispatch_thread_id, 0)).xyz * 2.0f - 1.0f);
        // Initialize weight
        {
            float3 normal         = FFX_DNSR_Reflections_LoadFromGroupSharedMemory(group_thread_id).normal;
            weight_acc            = 1.0f
                                    * exp(-(1.0f - dot(normal, central_normal)));
        }
        const int radius = 2;
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                int2   gcoord = group_thread_id + int2(x, y);
                float3 normal = FFX_DNSR_Reflections_LoadFromGroupSharedMemory(gcoord).normal;
                float weight  = 1.0f
                                * exp(-(1.0f - dot(normal, central_normal)));
                if (weight > weight_acc + 1.0e-3f) {
                    coord_acc  = gcoord;
                    weight_acc = weight;
                }
            }
        }
        int2 gid          = group_id * 8;
        int2 sample_coord = gid + coord_acc - int2(4, 4);
        if (g_gbuffer_full_roughness.Load(int3(dispatch_thread_id, 0)).w <= g_frame_info.fsr_roughness_threshold)
            radiance = upscale((float2(sample_coord) + (0.5f).xx) / float2(screen_size), sample_coord);
        else {
            float2 uv   = (float2(sample_coord) + (0.5f).xx) / float2(screen_size);
            radiance    = IN_RADIANCE.SampleLevel(g_linear_sampler, uv, 0.0f).xyz;
        }
    }

    FFX_DNSR_Reflections_ApplyReflections(dispatch_thread_id, screen_size, radiance);
#else
    float2 uv = float2(dispatch_thread_id + (0.5f).xx) / screen_size.xy;
    float3 radiance  = IN_RADIANCE.Load(int3(uv * reflection_size, 0)).xyz;
    FFX_DNSR_Reflections_ApplyReflections(dispatch_thread_id, screen_size, radiance);
#endif
    
}