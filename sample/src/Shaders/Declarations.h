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

#ifndef DECLARATIONS_H
#define DECLARATIONS_H

struct Material_Info {
    float albedo_factor_x;
    float albedo_factor_y;
    float albedo_factor_z;
    float albedo_factor_w;

    // A.R.M. Packed texture - Ambient occlusion | Roughness | Metalness
    float arm_factor_x;
    float arm_factor_y;
    float arm_factor_z;
    int   arm_tex_id;

    float emission_factor_x;
    float emission_factor_y;
    float emission_factor_z;
    int   emission_tex_id;

    int   normal_tex_id;
    int   albedo_tex_id;
    float alpha_cutoff;
    int   is_opaque;
};

struct Instance_Info {
    int surface_id_table_offset;
    int num_opaque_surfaces;
    int node_id;
    int num_surfaces;
};

#define SURFACE_INFO_INDEX_TYPE_U32 0
#define SURFACE_INFO_INDEX_TYPE_U16 1

struct Surface_Info {
    int material_id;
    int index_offset; // Offset for the first index
    int index_type;   // 0 - u32, 1 - u16
    int position_attribute_offset;

    int texcoord0_attribute_offset;
    int texcoord1_attribute_offset;
    int normal_attribute_offset;
    int tangent_attribute_offset;

    int num_indices;
    int num_vertices;
    int weight_attribute_offset;
    int joints_attribute_offset;
};

#ifndef __HLSL_VERSION

#    ifndef CUSTOM_VECTOR_MATH

struct float4x4 {
    float m[16];
    float4x4() = default;
    float4x4(Vectormath::Matrix4 mat) { memcpy(m, &mat, 64); }
};
struct float4 {
    float m[4];
    float4() = default;
    float4(Vectormath::Vector4 v) { memcpy(m, &v, 16); }
};
struct float3 {
    float m[3];
    float3() = default;
    float3(Vectormath::Vector3 v) { memcpy(m, &v, 12); }
};
struct float2 {
    float m[2];
};
struct int4 {
    int m[4];
};
struct int3 {
    int m[3];
};
struct int2 {
    int m[2];
};
struct uint2 {
    int m[2];
};
using uint = uint32_t;

#    endif

#endif

#define MAX_LIGHT_INSTANCES 80
#define MAX_SHADOW_INSTANCES 32

struct Light {
    float4x4 mLightViewProj;
    float4x4 mLightView;

    float3 direction;
    float  range;

    float3 color;
    float  intensity;

    float3 position;
    float  innerConeCos;

    float outerConeCos;
    int   type;
    float depthBias;
    int   shadowMapIndex;
};

struct LightInstance {
    float4x4 mLightViewProj;
    float3   direction;
    float3   position;
    int      shadowMapIndex;
    float    depthBias;
};

static const int LightType_Directional = 0;
static const int LightType_Point       = 1;
static const int LightType_Spot        = 2;

struct PerFrame {
    float4x4 u_mCameraCurrViewProj;
    float4x4 u_mCameraPrevViewProj;
    float4x4 u_mCameraCurrViewProjInverse;
    float4   u_CameraPos;

    float  u_iblFactor;
    float  u_EmissiveFactor;
    float2 u_invScreenResolution;

    float4 u_WireframeOptions;

    int3 u_padding;
    int  u_lightCount;

    Light u_lights[MAX_LIGHT_INSTANCES];
};

struct FrameInfo {
    float4x4 inv_view_proj;
    float4x4 proj;
    float4x4 inv_proj;
    float4x4 view;
    float4x4 inv_view;
    float4x4 prev_view_proj;
    float4x4 prev_view;

    uint frame_index;
    uint max_traversal_intersections;
    uint min_traversal_occupancy;
    uint most_detailed_mip;

    float temporal_stability_factor;
    float ssr_confidence_threshold;
    float depth_buffer_thickness;
    float roughness_threshold;

    uint  samples_per_quad;
    uint  temporal_variance_guided_tracing_enabled;
    uint  hsr_mask;
    float simulation_time;

    // We use 1/8 resolution textures and we need a way to convert from screen space xy
    // to 1/8 space UV that takes the cut-off pixels bias into account
    float x_to_u_factor;
    uint  max_history_samples;
    float y_to_v_factor;
    float history_clip_weight;

    uint base_width;
    uint base_height;
    uint reflection_width;
    uint reflection_height;

    float hybrid_miss_weight;
    float max_raytraced_distance;
    float hybrid_spawn_rate;
    float reflections_backfacing_threshold;

    float depth_similarity_sigma;
    uint  reflections_upscale_mode;
    uint  random_samples_per_pixel;
    float vrt_variance_threshold;

    float ssr_thickness_length_factor;
    float fsr_roughness_threshold;
    float ray_length_exp_factor;
    float reflection_factor;

    float rt_roughness_threshold;
    uint  pad0;
    uint  pad1;
    uint  pad2;

    PerFrame perFrame;
};

#define HSR_UPSCALE_MODE_POINT 0
#define HSR_UPSCALE_MODE_BILINEAR 1
#define HSR_UPSCALE_MODE_FSR 2
#define HSR_UPSCALE_MODE_FSR_EDGE 3

#ifdef __HLSL_VERSION

// Convenience definitions
#    define g_frame_info g_frame_info_cb[0]
#    define g_inv_view_proj g_frame_info.inv_view_proj
#    define g_proj g_frame_info.proj
#    define g_inv_proj g_frame_info.inv_proj
#    define g_view g_frame_info.view
#    define g_inv_view g_frame_info.inv_view
#    define g_prev_view_proj g_frame_info.prev_view_proj
#    define g_prev_view g_frame_info.prev_view
#    define g_frame_index g_frame_info.frame_index
#    define g_max_traversal_intersections g_frame_info.max_traversal_intersections
#    define g_min_traversal_occupancy g_frame_info.min_traversal_occupancy
#    define g_most_detailed_mip g_frame_info.most_detailed_mip
#    define g_temporal_stability_factor g_frame_info.temporal_stability_factor
#    define g_depth_buffer_thickness g_frame_info.depth_buffer_thickness
#    define g_roughness_threshold g_frame_info.roughness_threshold
#    define g_samples_per_quad g_frame_info.samples_per_quad
#    define g_temporal_variance_guided_tracing_enabled g_frame_info.temporal_variance_guided_tracing_enabled
#    define g_hsr_mask g_frame_info.hsr_mask
#    define g_hsr_variance_factor g_frame_info.hsr_variance_factor
#    define g_hsr_variance_power g_frame_info.hsr_variance_power
#    define g_hsr_variance_bias g_frame_info.hsr_variance_bias
#    define g_depth_similarity_sigma g_frame_info.depth_similarity_sigma
#    define g_camera_pos_world_space g_frame_info.perFrame.u_CameraPos

#    define myPerFrame g_frame_info.perFrame

#endif

#define RAY_COUNTER_SW_OFFSET 0
#define RAY_COUNTER_SW_HISTORY_OFFSET 4
#define RAY_COUNTER_DENOISE_OFFSET 8
#define RAY_COUNTER_DENOISE_HISTORY_OFFSET 12
#define RAY_COUNTER_HW_OFFSET 16
#define RAY_COUNTER_HW_HISTORY_OFFSET 20

#define INDIRECT_ARGS_SW_OFFSET 0
#define INDIRECT_ARGS_DENOISE_OFFSET 12
#define INDIRECT_ARGS_APPLY_OFFSET 24
#define INDIRECT_ARGS_HW_OFFSET 36

#include "Descriptors.h"

// Use hitcounter feedback
#define HSR_FLAGS_USE_HIT_COUNTER (1 << 0)
// Traverse in screen space
#define HSR_FLAGS_USE_SCREEN_SPACE (1 << 1)
// Traverse using HW ray tracing
#define HSR_FLAGS_USE_RAY_TRACING (1 << 2)
// Iterate BVH to search for the opaque fragment
#define HSR_FLAGS_RESOLVE_TRANSPARENT (1 << 3)
// Grab radiance from screen space shaded image for ray traced intersections, when possible
#define HSR_FLAGS_SHADING_USE_SCREEN (1 << 5)
// defines HSR_SHADING_USE_SCREEN

// Extra flags for debugging
#define HSR_FLAGS_FLAG_0 (1 << 9)
#define HSR_FLAGS_FLAG_1 (1 << 10)
#define HSR_FLAGS_FLAG_2 (1 << 11)
#define HSR_FLAGS_FLAG_3 (1 << 12)

// Visualization tweaking
#define HSR_FLAGS_SHOW_DEBUG_TARGET (1 << 13)
#define HSR_FLAGS_SHOW_INTERSECTION (1 << 14)
#define HSR_FLAGS_SHOW_REFLECTION_TARGET (1 << 15)
#define HSR_FLAGS_APPLY_REFLECTIONS (1 << 16)
#define HSR_FLAGS_INTERSECTION_ACCUMULATE (1 << 17)

#define HSR_FLAGS_VISUALIZE_WAVES (1 << 18)
#define HSR_FLAGS_VISUALIZE_AVG_RADIANCE (1 << 19)
#define HSR_FLAGS_VISUALIZE_VARIANCE (1 << 20)
#define HSR_FLAGS_VISUALIZE_NUM_SAMPLES (1 << 21)
#define HSR_FLAGS_VISUALIZE_RAY_LENGTH (1 << 23)
#define HSR_FLAGS_VISUALIZE_REPROJECTION (1 << 25)
#define HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY (1 << 26)
#define HSR_FLAGS_VISUALIZE_HIT_COUNTER (1 << 27)
#define HSR_FLAGS_VISUALIZE_PRIMARY_RAYS (1 << 28)

#endif
