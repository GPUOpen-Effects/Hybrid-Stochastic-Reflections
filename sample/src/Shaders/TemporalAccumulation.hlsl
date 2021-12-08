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
Texture2D<float4> g_radiance_mip; // 8x8 average radiance 
Texture2D<min16float3> g_radiance_1; // Radiance target 1 - history radiance 
Texture2D<min16float3> g_radiance_reprojected; // Radiance target 2 - reprojected results 
Texture2D<float4> g_extracted_roughness; // Current extracted GBuffer/roughness in reflection target resolution 
Texture2D<min16float> g_radiance_variance_1; // Variance target 1 - history 
Texture2D<min16float> g_radiance_num_samples_0; // Sample counter target 0 - current 
RWTexture2D<min16float3> g_rw_radiance_0; // Radiance target 0 - intersection results 
RWTexture2D<min16float> g_rw_radiance_variance_0; // Variance target 0 - current variance/ray length 
RWByteAddressBuffer g_rw_denoise_tile_list; 
RWTexture2D<float4> g_rw_debug; // Debug target 
SamplerState g_linear_sampler; 
#endif


//////////////////////////////////
///// Load/Store Interface ///////
//////////////////////////////////

// Input
min16float3 FFX_DNSR_Reflections_SampleAverageRadiance(float2 uv) { return g_radiance_mip.SampleLevel(g_linear_sampler, uv, 0.0); }
min16float3 FFX_DNSR_Reflections_LoadRadiance(int2 pixel_coordinate) { return g_radiance_1[pixel_coordinate].xyz; }
min16float3 FFX_DNSR_Reflections_LoadRadianceReprojected(int2 pixel_coordinate) { return g_radiance_reprojected.Load(int3(pixel_coordinate, 0)).xyz; }
min16float  FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) { return g_extracted_roughness.Load(int3(pixel_coordinate, 0)).x; }
min16float  FFX_DNSR_Reflections_LoadVariance(int2 pixel_coordinate) { return g_radiance_variance_1[pixel_coordinate].x; }
min16float  FFX_DNSR_Reflections_LoadNumSamples(int2 pixel_coordinate) { return g_radiance_num_samples_0[pixel_coordinate].x; }

// Output
void FFX_DNSR_Reflections_StoreTemporalAccumulation(int2 pixel_coordinate, min16float3 radiance, min16float variance) {
    g_rw_radiance_0[pixel_coordinate]          = radiance.xyz;
    g_rw_radiance_variance_0[pixel_coordinate] = variance;
}
//////////////////////////////////
//////////////////////////////////
//////////////////////////////////

#include "ffx_denoiser_reflections_resolve_temporal.h"

[numthreads(8, 8, 1)]
void main(int2 group_thread_id : SV_GroupThreadID,
          uint group_index     : SV_GroupIndex,
          uint    group_id     : SV_GroupID) {
    uint   packed_coords           = g_rw_denoise_tile_list.Load(4 * group_id);
    int2   dispatch_thread_id      = int2(packed_coords & 0xffffu, (packed_coords >> 16) & 0xffffu) + group_thread_id;
    int2   dispatch_group_id       = dispatch_thread_id / 8;
    uint2  g_buffer_dimensions     = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    float2 g_inv_buffer_dimensions = (1.0).xx / float2(g_buffer_dimensions);
    uint2 remapped_group_thread_id    = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    uint2 remapped_dispatch_thread_id = dispatch_group_id * 8 + remapped_group_thread_id;
    FFX_DNSR_Reflections_ResolveTemporal(remapped_dispatch_thread_id, remapped_group_thread_id, g_buffer_dimensions, g_inv_buffer_dimensions, g_frame_info.history_clip_weight);

#ifdef HSR_DEBUG
    float2 uv = float2(dispatch_thread_id + 0.5) / g_buffer_dimensions;
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_VARIANCE) {
        g_rw_debug[dispatch_thread_id] = g_radiance_variance_0.Load(int3(dispatch_thread_id, 0));
        return;
    }
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_NUM_SAMPLES) {
        min16float  roughness     = FFX_DNSR_Reflections_LoadRoughness(dispatch_thread_id);
        min16float  num_samples   = g_radiance_num_samples_0.Load(int3(dispatch_thread_id, 0));
        min16float s_max_samples  = max(8.0, g_frame_info.max_history_samples * roughness);
        g_rw_debug[dispatch_thread_id] = num_samples.xxxx / s_max_samples;
        return;
    }
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_AVG_RADIANCE) {
        g_rw_debug[dispatch_thread_id] = g_radiance_mip.SampleLevel(g_linear_sampler, uv, 0.0);
        return;
    }
#endif
}