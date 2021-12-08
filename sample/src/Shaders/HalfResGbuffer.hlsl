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
Texture2D<float4> g_gbuffer_full_depth; // Current GBuffer/depth in render resolution 
Texture2D<float4> g_gbuffer_full_roughness; // Current GBuffer/specular_roughness in render resolution 
Texture2D<float4> g_gbuffer_full_normal; // Current GBuffer/albedo in render resolution 
Texture2D<float4> g_full_motion_vector; // Current GBuffer/motion_vectors in render resolution 
RWTexture2D<float4> g_rw_gbuffer_normal; // Current GBuffer/normal in reflection target resolution 
RWTexture2D<float4> g_rw_gbuffer_roughness; // Current GBuffer/specular_roughness in reflection target resolution 
RWTexture2D<float4> g_rw_gbuffer_depth; // Current GBuffer/depth in reflection target resolution 
RWTexture2D<float4> g_rw_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
#endif

void PickSample(
    int2 dispatch_thread_id,
    out float4 specular_roughness,
    out float3 normal,
    out float4 albedo,
    out float  depth,            
    out float2 motion_vector      
) {
    float min_depth = 1.0f;
    int2 samples[] = {
        int2(0, 0),
        int2(1, 0),
        int2(1, 1),
        int2(0, 1),
    };
    for (int i = 0; i < 4; i++) {
        int2 load_coord = dispatch_thread_id.xy * 2 + samples[(i /*+ g_frame_index*/) & 3];
        float  central_depth           = g_gbuffer_full_depth.Load(int3(load_coord, 0)).x;
        if (!FFX_DNSR_Reflections_IsBackground(central_depth) &&
                // Closest 
                min_depth > central_depth
            ) {
            float4 central_roughness       = g_gbuffer_full_roughness.Load(int3(load_coord, 0));
            // if (FFX_DNSR_Reflections_IsGlossyReflection(central_roughness.w)) {
                float3 central_normal          = g_gbuffer_full_normal.Load(int3(load_coord, 0)).xyz * 2.0f - 1.0f;
                float4 central_albedo          = g_gbuffer_full_albedo.Load(int3(load_coord, 0)).xyzw;
                if (!all(central_normal == -1.0f)) {
                    float2  central_motion_vector  = g_full_motion_vector.Load(int3(load_coord, 0)).xy;
                    specular_roughness = central_roughness;
                    normal             = central_normal;
                    albedo             = central_albedo;
                    depth              = central_depth;
                    motion_vector      = central_motion_vector;
                    min_depth          = central_depth;
                    // return;
                }
            // }
        }
    }
}

[numthreads(8, 8, 1)]
void main(int2 group_id : SV_GroupID, uint group_index : SV_GroupIndex) {
    int2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    int2 dispatch_thread_id = group_id * 8 + group_thread_id;

    uint2 screen_size = uint2(g_frame_info.base_width, g_frame_info.base_height);
    uint2 reflection_size = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);

    float2 uv                      = (float2(dispatch_thread_id) + (0.5f).xx) / float2(screen_size.xy);

    float4 specular_roughness = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float3 normal             = float3(-1.0f, -1.0f, -1.0f);
    float4 albedo             = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  depth              = 1.0f;
    float2 motion_vector      = (0.0f).xx;
    PickSample(dispatch_thread_id, specular_roughness, normal, albedo, depth, motion_vector);
    g_rw_gbuffer_albedo[dispatch_thread_id.xy] = albedo;
    g_rw_gbuffer_normal[dispatch_thread_id.xy] = (normal / 2.0f + (0.5f).xxx).xyzz;
    g_rw_gbuffer_roughness[dispatch_thread_id.xy] = specular_roughness;
    g_rw_gbuffer_depth[dispatch_thread_id.xy] = g_textures[GDT_TEXTURES_HIZ_SLOT].Load(int3(dispatch_thread_id.xy, 1));
    g_rw_motion_vector[dispatch_thread_id.xy] = motion_vector.xyyy;
}
