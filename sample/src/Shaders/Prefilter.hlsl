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
Texture2D<float4> g_extracted_roughness; // Current extracted GBuffer/roughness in reflection target resolution 
Texture2D<float4> g_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
Texture2D<min16float3> g_radiance_0; // Radiance target 0 - intersection results 
Texture2D<min16float> g_radiance_variance_0; // Variance target 0 - current variance/ray length 
Texture2D<float4> g_gbuffer_normal; // Current GBuffer/normal in reflection target resolution 
Texture2D<float4> g_gbuffer_depth_history; // Previous GBuffer/depth in reflection target resolution 
RWTexture2D<min16float3> g_rw_radiance_1; // Radiance target 1 - history radiance 
RWTexture2D<min16float> g_rw_radiance_variance_1; // Variance target 1 - history 
RWByteAddressBuffer g_rw_denoise_tile_list; 
SamplerState g_linear_sampler;
#endif

#define IN_RADIANCE g_radiance_0

//////////////////////////////////
///// Load/Store Interface ///////
//////////////////////////////////

// Input
// Get 1/8 resolution uv
float2      FFX_DNSR_Reflections_GetUV8(int2 xy) { return float2(xy) * float2(g_frame_info.x_to_u_factor, g_frame_info.y_to_v_factor); }
min16float3      FFX_DNSR_Reflections_SampleAverageRadiance(float2 uv) { return g_radiance_mip.SampleLevel(g_linear_sampler, uv, 0.0); }
min16float       FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) { return g_extracted_roughness.Load(int3(pixel_coordinate, 0)).x; }
float2      FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate) { return g_motion_vector.Load(int3(pixel_coordinate, 0)).xy * float2(0.5, -0.5); }
min16float3 FFX_DNSR_Reflections_LoadRadianceFP16(int2 pixel_coordinate) { return min16float3(IN_RADIANCE[pixel_coordinate].xyz); }
min16float  FFX_DNSR_Reflections_LoadVarianceFP16(int2 pixel_coordinate) { return g_radiance_variance_0.Load(int3(pixel_coordinate, 0)).x; }
min16float3 FFX_DNSR_Reflections_LoadWorldSpaceNormalFP16(int2 pixel_coordinate) { return (min16float3)normalize(2 * g_gbuffer_normal.Load(int3(pixel_coordinate, 0)).xyz - 1); }
float       FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) { return g_gbuffer_depth.Load(int3(pixel_coordinate, 0)).x; }
void        FFX_DNSR_Reflections_LoadNeighborhood(int2 dispatch_thread_id, out min16float3 radiance, out min16float variance, out min16float3 normal, out float depth,
                                                int2 screen_size) {
    float2 uv            = (dispatch_thread_id.xy + (0.5).xx) / float2(screen_size.xy);
    float2 motion_vector = FFX_DNSR_Reflections_LoadMotionVector(dispatch_thread_id);
    radiance             = FFX_DNSR_Reflections_LoadRadianceFP16(dispatch_thread_id);
    variance             = FFX_DNSR_Reflections_LoadVarianceFP16(dispatch_thread_id);
    normal               = FFX_DNSR_Reflections_LoadWorldSpaceNormalFP16(dispatch_thread_id);
    depth                = FFX_DNSR_Reflections_GetLinearDepth(uv, FFX_DNSR_Reflections_LoadDepth(dispatch_thread_id));
}

// Output
void FFX_DNSR_Reflections_StorePrefilteredReflections(int2 pixel_coordinate, min16float3 radiance, min16float variance) {
    g_rw_radiance_1[pixel_coordinate]          = radiance.xyz;
    g_rw_radiance_variance_1[pixel_coordinate] = variance;
}

//////////////////////////////////
//////////////////////////////////
//////////////////////////////////

#include "ffx_denoiser_reflections_prefilter.h"

[numthreads(8, 8, 1)]
void main(int2 group_thread_id : SV_GroupThreadID,
          uint group_index     : SV_GroupIndex,
          uint    group_id     : SV_GroupID) {
    uint  packed_coords               = g_rw_denoise_tile_list.Load(4 * group_id);
    int2  dispatch_thread_id          = int2(packed_coords & 0xffffu, (packed_coords >> 16) & 0xffffu) + group_thread_id;
    int2  dispatch_group_id           = dispatch_thread_id / 8;
    uint2 screen_dimensions           = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    uint2 remapped_group_thread_id    = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    uint2 remapped_dispatch_thread_id = dispatch_group_id * 8 + remapped_group_thread_id;
    FFX_DNSR_Reflections_Prefilter((int2)remapped_dispatch_thread_id, (int2)remapped_group_thread_id, screen_dimensions);
}