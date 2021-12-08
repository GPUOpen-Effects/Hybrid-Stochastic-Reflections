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
Texture2D<min16float3> g_radiance_0; // Radiance target 0 - intersection results 
RWTexture2D<min16float> g_rw_radiance_variance_0; // Variance target 0 - current variance/ray length 
Texture2D<float4> g_gbuffer_depth; // Current GBuffer/depth in reflection target resolution 
Texture2D<float4> g_extracted_roughness_history; // Previous extracted GBuffer/roughness in reflection target resolution 
Texture2D<float4> g_gbuffer_normal; // Current GBuffer/normal in reflection target resolution 
Texture2D<float4> g_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
Texture2D<float4> g_radiance_mip_prev; // 8x8 average radiance history 
Texture2D<min16float3> g_radiance_1; // Radiance target 1 - history radiance 
Texture2D<min16float> g_radiance_variance_1; // Variance target 1 - history 
Texture2D<min16float> g_radiance_num_samples_1; // Sample count target 1 - history 
Texture2D<float4> g_gbuffer_normal_history; // Previous GBuffer/normal in reflection target resolution 
Texture2D<float4> g_gbuffer_depth_history; // Previous GBuffer/depth in reflection target resolution 
Texture2D<float4> g_extracted_roughness_history; // Previous extracted GBuffer/roughness in reflection target resolution 
RWTexture2D<min16float3> g_rw_radiance_reprojected; // Radiance target 2 - reprojected results 
RWTexture2D<min16float> g_rw_radiance_variance_0; // Variance target 0 - current variance/ray length 
RWTexture2D<min16float> g_rw_radiance_num_samples_0; // Sample counter target 0 - current 
RWTexture2D<float4> g_rw_radiance_avg; // 8x8 average radiance 
RWByteAddressBuffer g_rw_denoise_tile_list; 
SamplerState g_linear_sampler; 
#endif

//////////////////////////////////
///// Load/Store Interface ///////
//////////////////////////////////

// Current buffers
min16float3 FFX_DNSR_Reflections_LoadRadiance(int2 pixel_coordinate) { return (min16float3)g_radiance_0[pixel_coordinate].xyz; }
min16float FFX_DNSR_Reflections_LoadRayLength(int2 pixel_coordinate) { return (min16float)g_rw_radiance_variance_0[pixel_coordinate].x; }
float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) { return g_gbuffer_depth.Load(int3(pixel_coordinate, 0)).x; }
min16float FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) { return g_extracted_roughness.Load(int3(pixel_coordinate, 0)).x; }
min16float3 FFX_DNSR_Reflections_LoadWorldSpaceNormal(int2 pixel_coordinate) { return normalize(2 * (min16float3)g_gbuffer_normal.Load(int3(pixel_coordinate, 0)).xyz - 1); }
float2 FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate) { return g_motion_vector.Load(int3(pixel_coordinate, 0)).xy * float2(0.5, -0.5); }

// History buffers
min16float3 FFX_DNSR_Reflections_SamplePreviousAverageRadiance(float2 uv) { return g_radiance_mip_prev.SampleLevel(g_linear_sampler, uv, 0.0).xyz; }
min16float3 FFX_DNSR_Reflections_SampleRadianceHistory(float2 uv) { return (min16float3)g_radiance_1.SampleLevel(g_linear_sampler, uv, 0).xyz; }
min16float FFX_DNSR_Reflections_SampleVarianceHistory(float2 uv) { return (min16float)g_radiance_variance_1.SampleLevel(g_linear_sampler, uv, 0.0).x; }
min16float FFX_DNSR_Reflections_SampleNumSamplesHistory(float2 uv) { return (min16float)g_radiance_num_samples_1.SampleLevel(g_linear_sampler, uv, 0.0).x; }
min16float3 FFX_DNSR_Reflections_SampleWorldSpaceNormalHistory(float2 uv) { return normalize(2 * (min16float3)g_gbuffer_normal_history.SampleLevel(g_linear_sampler, uv, 0.0).xyz - 1); }
float FFX_DNSR_Reflections_SampleDepthHistory(float2 uv) { return g_gbuffer_depth_history.SampleLevel(g_linear_sampler, uv, 0.0).x; }
min16float FFX_DNSR_Reflections_SampleRoughnessHistory(float2 uv) { return g_extracted_roughness_history.SampleLevel(g_linear_sampler, uv, 0).x; }

min16float FFX_DNSR_Reflections_LoadRoughnessHistory(int2 pixel_coordinate) { return g_extracted_roughness_history.Load(int3(pixel_coordinate, 0)).x; }
min16float3 FFX_DNSR_Reflections_LoadRadianceHistory(int2 pixel_coordinate) { return (min16float3)g_radiance_1.Load(int3(pixel_coordinate, 0)).xyz; }
float FFX_DNSR_Reflections_LoadDepthHistory(int2 pixel_coordinate) { return g_gbuffer_depth_history.Load(int3(pixel_coordinate, 0)).x; }
min16float3 FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(int2 pixel_coordinate) { return normalize(2 * (min16float3)g_gbuffer_normal_history.Load(int3(pixel_coordinate, 0)).xyz - 1); }

float FFX_DNSR_Reflections_GetRandom(int2 dispatch_thread_id) {
    return SampleRandomVector2DBaked(dispatch_thread_id).x;
}

// Output
void FFX_DNSR_Reflections_StoreRadianceReprojected(int2 pixel_coordinate, min16float3 value) {
    g_rw_radiance_reprojected[pixel_coordinate] = value.xyz;
}
void FFX_DNSR_Reflections_StoreVariance(int2 pixel_coordinate, min16float value) {
    g_rw_radiance_variance_0[pixel_coordinate] = value;
}
void FFX_DNSR_Reflections_StoreNumSamples(int2 pixel_coordinate, min16float value) {
    g_rw_radiance_num_samples_0[pixel_coordinate] = value;
}
void FFX_DNSR_Reflections_StoreAverageRadiance(int2 pixel_coordinate, min16float3 value) {
    g_rw_radiance_avg[pixel_coordinate] = value.xyzz;
}

//////////////////////////////////
//////////////////////////////////
//////////////////////////////////

#include "ffx_denoiser_reflections_reproject.h"

[numthreads(8, 8, 1)]
void main(int2 group_thread_id      : SV_GroupThreadID,
                uint group_index    : SV_GroupIndex,
                uint    group_id    : SV_GroupID) {
    uint  packed_coords      = g_rw_denoise_tile_list.Load(4 * group_id);
    int2  dispatch_thread_id = int2(packed_coords & 0xffffu, (packed_coords >> 16) & 0xffffu) + group_thread_id;
    int2  dispatch_group_id  = dispatch_thread_id / 8;
    uint2 g_buffer_dimensions = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    float2 g_inv_buffer_dimensions = (1.0).xx / float2(g_buffer_dimensions);
    uint2 remapped_group_thread_id    = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    uint2 remapped_dispatch_thread_id = dispatch_group_id * 8 + remapped_group_thread_id;
    
    FFX_DNSR_Reflections_Reproject(remapped_dispatch_thread_id, remapped_group_thread_id, g_buffer_dimensions, g_temporal_stability_factor, g_frame_info.max_history_samples);

#ifdef HSR_DEBUG
    float2 uv = float2(dispatch_thread_id + 0.5) / g_buffer_dimensions;
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_REPROJECTION) {
            g_rw_textures[GDT_RW_TEXTURES_DEBUG_SLOT][dispatch_thread_id] = g_radiance_reprojected[dispatch_thread_id].xyzz;
        return;
    }
#endif

}