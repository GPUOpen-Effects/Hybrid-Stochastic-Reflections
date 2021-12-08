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

#ifdef HSR_DEBUG
static int2 g_debug_pixel_coord;
#endif

#include "Declarations.h"

HLSL_INIT_GLOBAL_BINDING_TABLE(1)

#include "Common.hlsl"

#define FFX_REFLECTIONS_SKY_DISTANCE 20.0f
#define FFX_HITCOUNTER_SW_HIT_FLAG (1 << 0)
#define FFX_HITCOUNTER_SW_MISS_FLAG (1 << 16)


/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h // 
/////////////////////////////////////////////////////

#if 0
Texture2D<float4> g_brdf_lut; // BRDF Look up table for image based lightning
TextureCube<float4> g_atmosphere_lut; // Environment specular LUT 
TextureCube<float4> g_atmosphere_dif; // Environment Diffuse LUT 
SamplerState g_linear_sampler; 
Texture2D<float4> g_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
Texture2D<float4> g_gbuffer_normal_history; // Previous GBuffer/normal in reflection target resolution 
Texture2D<float4> g_hiz; // Current HIZ pyramid in reflection target resolution 
Texture2D<float4> g_gbuffer_depth; // Current GBuffer/depth in reflection target resolution 
RWByteAddressBuffer g_rw_geometry; // Vertex/Index buffers for all the geometry in the TLAS 
RWByteAddressBuffer g_rw_ray_counter; 
RWByteAddressBuffer g_rw_indirect_args; 
RWByteAddressBuffer g_rw_metrics; 
RWByteAddressBuffer g_rw_downsample_counter; 
RWByteAddressBuffer g_rw_ray_gbuffer_list; // Array of RayGBuffer for deferred shading of ray traced results 
RWTexture2D<min16float3> g_rw_radiance_0; // Radiance target 0 - intersection results 
RWTexture2D<min16float> g_rw_radiance_variance_0; // Variance target 0 - current variance/ray length 
RWByteAddressBuffer g_rw_hw_ray_list; 
RWByteAddressBuffer g_rw_ray_list; 
Texture2D<float4> g_extracted_roughness; // Current extracted GBuffer/roughness in reflection target resolution 
Texture2D<float4> g_lit_scene_history; // Previous render resolution color target 
RWTexture2D<float4> g_rw_debug; // Debug target 
SamplerComparisonState g_cmp_sampler; 
#endif

groupshared uint g_group_shared_counter;

// Declarations needed for shading and shadow filtering
#define brdfTexture g_brdf_lut
#define diffuseCube g_atmosphere_dif
#define specularCube g_atmosphere_lut
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

struct PushConstants {
    uint masks;
    uint depth_mip_bias;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc : DX12_PUSH_CONSTANTS;

float2 FFX_DNSR_Reflections_SampleMotionVector(float2 uv) { return g_motion_vector.SampleLevel(g_linear_sampler, uv, 0.0).xy * float2(0.5, -0.5); }

uint GetBin(float3 origin, float3 direction) {
    uint x = direction.x > 0.0 ? 1 : 0;
    uint y = direction.y > 0.0 ? 1 : 0;
    uint z = direction.z > 0.0 ? 1 : 0;
    return x | (y << 1) | (z << 2);
}

float3 FFX_SSSR_LoadWorldSpaceNormal(int2 pixel_coordinate) { return normalize(2 * g_gbuffer_normal.Load(int3(pixel_coordinate, 0)).xyz - 1); }
float3 FFX_SSSR_SampleNormal(float2 uv) { return normalize(2 * g_gbuffer_normal.SampleLevel(g_linear_sampler, uv, 0.0).xyz - 1); }
float FFX_SSSR_LoadDepth(int2 pixel_coordinate, int mip) { return g_hiz.Load(int3(pixel_coordinate, mip + pc.depth_mip_bias)); }
float FFX_DNSR_Reflections_SampleDepth(float2 uv) { return g_gbuffer_depth.SampleLevel(g_linear_sampler, uv, 0.0).x; }
float3 FFX_SSSR_ScreenSpaceToViewSpace(float3 screen_space_position) { return InvProjectPosition(screen_space_position, g_inv_proj); }
float3 ScreenSpaceToWorldSpace(float3 screen_space_position) { return InvProjectPosition(screen_space_position, g_inv_view_proj); }
float3 SampleEnvironmentMap(float3 direction) { return g_atmosphere_lut.SampleLevel(g_linear_sampler, direction, 0).xyz; }
bool IsMirrorReflection(float roughness) { return roughness < 0.0001; }
void FFX_Fetch_Face_Indices_U32(out uint3 face3, in uint offset, in uint triangle_id) { face3 = g_rw_geometry.Load<uint3>(offset * 4 + 12 * triangle_id); }
void FFX_Fetch_Face_Indices_U16(out uint3 face3, in uint offset, in uint triangle_id) {
    uint word_id_0  = triangle_id * 3 + 0;
    uint dword_id_0 = word_id_0 / 2;
    uint shift_0    = 16 * (word_id_0 & 1);

    uint word_id_1  = triangle_id * 3 + 1;
    uint dword_id_1 = word_id_1 / 2;
    uint shift_1    = 16 * (word_id_1 & 1);

    uint word_id_2  = triangle_id * 3 + 2;
    uint dword_id_2 = word_id_2 / 2;
    uint shift_2    = 16 * (word_id_2 & 1);

    uint u0 = g_rw_geometry.Load<uint>(offset * 4 + dword_id_0 * 4);
    u0      = (u0 >> shift_0) & 0xffffu;
    uint u1 = g_rw_geometry.Load<uint>(offset * 4 + dword_id_1 * 4);
    u1      = (u1 >> shift_1) & 0xffffu;
    uint u2 = g_rw_geometry.Load<uint>(offset * 4 + dword_id_2 * 4);
    u2      = (u2 >> shift_0) & 0xffffu;
    face3   = uint3(u0, u1, u2);
}
float2 FFX_Fetch_float2(in int offset, in int vertex_id) { return g_rw_geometry.Load<float2>(offset * 4 + sizeof(float2) * vertex_id); }
float3 FFX_Fetch_float3(in int offset, in int vertex_id) { return g_rw_geometry.Load<float3>(offset * 4 + sizeof(float3) * vertex_id); }
float4 FFX_Fetch_float4(in int offset, in int vertex_id) { return g_rw_geometry.Load<float4>(offset * 4 + sizeof(float4) * vertex_id); }
void FFX_Fetch_Local_Basis(in Surface_Info sinfo, in uint3 face3, in float2 bary, out float2 uv, out float3 normal, out float4 tangent) {
    float3 normal0 = FFX_Fetch_float3(sinfo.normal_attribute_offset, face3.x);
    float3 normal1 = FFX_Fetch_float3(sinfo.normal_attribute_offset, face3.y);
    float3 normal2 = FFX_Fetch_float3(sinfo.normal_attribute_offset, face3.z);
    normal         = normal1 * bary.x + normal2 * bary.y + normal0 * (1.0 - bary.x - bary.y);
    // normal         = normalize(normal);

    if (sinfo.tangent_attribute_offset >= 0) {
        float4 tangent0 = FFX_Fetch_float4(sinfo.tangent_attribute_offset, face3.x);
        float4 tangent1 = FFX_Fetch_float4(sinfo.tangent_attribute_offset, face3.y);
        float4 tangent2 = FFX_Fetch_float4(sinfo.tangent_attribute_offset, face3.z);
        tangent         = tangent1 * bary.x + tangent2 * bary.y + tangent0 * (1.0 - bary.x - bary.y);
        // tangent.xyz     = normalize(tangent.xyz);
    }
    if (sinfo.texcoord0_attribute_offset >= 0) {
        float2 uv0 = FFX_Fetch_float2(sinfo.texcoord0_attribute_offset, face3.x);
        float2 uv1 = FFX_Fetch_float2(sinfo.texcoord0_attribute_offset, face3.y);
        float2 uv2 = FFX_Fetch_float2(sinfo.texcoord0_attribute_offset, face3.z);
        uv         = uv1 * bary.x + uv2 * bary.y + uv0 * (1.0 - bary.x - bary.y);
    }
}
void FFX_Fetch_Local_Basis(in Surface_Info sinfo, in uint3 face3, in float2 bary, out float2 uv, out float3 normal) {
    float3 normal0 = FFX_Fetch_float3(sinfo.normal_attribute_offset, face3.x);
    float3 normal1 = FFX_Fetch_float3(sinfo.normal_attribute_offset, face3.y);
    float3 normal2 = FFX_Fetch_float3(sinfo.normal_attribute_offset, face3.z);
    normal         = normal1 * bary.x + normal2 * bary.y + normal0 * (1.0 - bary.x - bary.y);
    // normal         = normalize(normal);

    if (sinfo.texcoord0_attribute_offset >= 0) {
        float2 uv0 = FFX_Fetch_float2(sinfo.texcoord0_attribute_offset, face3.x);
        float2 uv1 = FFX_Fetch_float2(sinfo.texcoord0_attribute_offset, face3.y);
        float2 uv2 = FFX_Fetch_float2(sinfo.texcoord0_attribute_offset, face3.z);
        uv         = uv1 * bary.x + uv2 * bary.y + uv0 * (1.0 - bary.x - bary.y);
    }
}

#ifdef USE_SSR
#    include "ffx_sssr.h"
#else
float2 FFX_SSSR_GetMipResolution(float2 screen_dimensions, int mip_level) { return screen_dimensions * pow(0.5, mip_level); }
#endif

float3 rotate(float4x4 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }
float3 rotate(float3x4 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }
float3 rotate(float3x3 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }

[numthreads(1, 1, 1)]
void PrepareIndirect(uint group_index : SV_GroupIndex,
                    uint group_id     : SV_GroupID) {
    uint cnt = g_rw_ray_counter.Load(RAY_COUNTER_HW_OFFSET);
    g_rw_ray_counter.Store(RAY_COUNTER_HW_OFFSET, 0);
    g_rw_ray_counter.Store(RAY_COUNTER_HW_HISTORY_OFFSET, cnt);
    g_rw_indirect_args.Store(INDIRECT_ARGS_HW_OFFSET + 0, (cnt + 31) / 32);
    g_rw_indirect_args.Store(INDIRECT_ARGS_HW_OFFSET + 4, 1);
    g_rw_indirect_args.Store(INDIRECT_ARGS_HW_OFFSET + 8, 1);
    // Feedback for metrics visualization
    {
        g_rw_metrics.Store(8, cnt);
    }
}

[numthreads(6, 1, 1)]
void ClearDownsampleCounter(int2 dispatch_thread_id : SV_DispatchThreadID) {
    g_rw_downsample_counter.Store(4 * dispatch_thread_id.x, 0);
}

// By Morgan McGuire @morgan3d, http://graphicscodex.com
// Reuse permitted under the BSD license.
// https://www.shadertoy.com/view/4dsSzr
float3 heatmapGradient(float t) {
    return clamp((pow(t, 1.5) * 0.8 + 0.2) * float3(smoothstep(0.0, 0.35, t) + t * 0.5, smoothstep(0.5, 1.0, t), max(1.0 - t * 1.7, t * 7.0 - 6.0)), 0.0, 1.0);
}

struct RayGbuffer {
    float2 uv;
    float3 normal;
    uint   material_id;
    float  world_ray_length;
    // Skip shading and do env probe
    bool skip;
};

struct PackedRayGbuffer {
    uint pack0;
    uint pack1;
    uint pack2;
};

bool FFX_Reflections_GbufferIsSkip(PackedRayGbuffer gbuffer) { return (gbuffer.pack1 & (1u << 31u)) != 0; }

float FFX_Reflections_GbufferGetRayLength(PackedRayGbuffer gbuffer) { return asfloat(gbuffer.pack2); }

PackedRayGbuffer FFX_Reflections_PackGbuffer(RayGbuffer gbuffer) {
    uint   pack0 = PackFloat16(gbuffer.uv);
    uint   pack1;
    float2 nuv  = OctahedronUV(gbuffer.normal);
    uint   iuvx = uint(255.0 * nuv.x);
    uint   iuvy = uint(255.0 * nuv.y);
    pack1       =                                  //
        ((gbuffer.material_id & 0x7fffu) << 16u) | //
        (iuvx << 0u) |                             //
        (iuvy << 8u);
    if (gbuffer.skip) pack1 |= (1u << 31u);
    uint             pack2 = asuint(gbuffer.world_ray_length); // PackFloat16(float2());
    PackedRayGbuffer pack  = {pack0, pack1, pack2};
    return pack;
}

RayGbuffer FFX_Reflections_UnpackGbuffer(PackedRayGbuffer gbuffer) {
    RayGbuffer ogbuffer;
    ogbuffer.uv               = UnpackFloat16(gbuffer.pack0);
    uint   iuvx               = (gbuffer.pack1 >> 0u) & 0xffu;
    uint   iuvy               = (gbuffer.pack1 >> 8u) & 0xffu;
    float2 nuv                = float2(float(iuvx) / 255.0, float(iuvy) / 255.0);
    ogbuffer.normal           = UVtoOctahedron(nuv);
    ogbuffer.material_id      = (gbuffer.pack1 >> 16u) & 0x7fffu;
    ogbuffer.skip             = FFX_Reflections_GbufferIsSkip(gbuffer);
    ogbuffer.world_ray_length = FFX_Reflections_GbufferGetRayLength(gbuffer);
    return ogbuffer;
}

void FFX_Reflections_SkipRayGBuffer(uint ray_index) {
    uint pack1 = (1u << 31u);
    g_rw_ray_gbuffer_list.Store<uint>(ray_index * 12 + 4, pack1);
}

void FFX_Reflections_StoreRayGBuffer(uint ray_index, in PackedRayGbuffer gbuffer) {
    g_rw_ray_gbuffer_list.Store<uint>(ray_index * 12 + 0, gbuffer.pack0);
    g_rw_ray_gbuffer_list.Store<uint>(ray_index * 12 + 4, gbuffer.pack1);
    g_rw_ray_gbuffer_list.Store<uint>(ray_index * 12 + 8, gbuffer.pack2);
}

PackedRayGbuffer FFX_Reflections_LoadRayGBuffer(uint ray_index) {
    uint             pack0 = g_rw_ray_gbuffer_list.Load<uint>(ray_index * 12 + 0);
    uint             pack1 = g_rw_ray_gbuffer_list.Load<uint>(ray_index * 12 + 4);
    uint             pack2 = g_rw_ray_gbuffer_list.Load<uint>(ray_index * 12 + 8);
    PackedRayGbuffer pack  = {pack0, pack1, pack2};
    return pack;
}

void FFX_Reflections_WriteRadiance(uint packed_coords, float4 radiance) {
    int2 coords;
    bool copy_horizontal;
    bool copy_vertical;
    bool copy_diagonal;
    UnpackRayCoords(packed_coords, coords, copy_horizontal, copy_vertical, copy_diagonal);
    g_rw_radiance_0[coords]          = radiance.xyz;
    g_rw_radiance_variance_0[coords] = radiance.w;
    uint2 copy_target    = coords ^ 0b1; // Flip last bit to find the mirrored coords along the x and y axis within a quad.
    {

        if (copy_horizontal) {
            uint2 copy_coords         = uint2(copy_target.x, coords.y);
            g_rw_radiance_0[copy_coords]          = radiance.xyz;
            g_rw_radiance_variance_0[copy_coords] = radiance.w;
        }
        if (copy_vertical) {
            uint2 copy_coords         = uint2(coords.x, copy_target.y);
            g_rw_radiance_0[copy_coords]          = radiance.xyz;
            g_rw_radiance_variance_0[copy_coords] = radiance.w;
        }
        if (copy_diagonal) {
            uint2 copy_coords         = copy_target;
            g_rw_radiance_0[copy_coords]          = radiance.xyz;
            g_rw_radiance_variance_0[copy_coords] = radiance.w;
        }
    }
}

[numthreads(32, 1, 1)]
void main(uint group_index : SV_GroupIndex,
          uint group_id    : SV_GroupID) {
              
    //////////////////////////////////////////
    ///////////  INITIALIZATION  /////////////
    //////////////////////////////////////////
    uint ray_index = group_id * 32 + group_index;
#ifdef USE_DEFERRED_RAYTRACING
    uint packed_coords = g_rw_hw_ray_list.Load(sizeof(uint) * ray_index);
#else // USE_DEFERRED_RAYTRACING
    uint packed_coords = g_rw_ray_list.Load(sizeof(uint) * ray_index);
#endif // USE_DEFERRED_RAYTRACING
    int2 coords;
    {
        bool copy_horizontal;
        bool copy_vertical;
        bool copy_diagonal;
        UnpackRayCoords(packed_coords, coords, copy_horizontal, copy_vertical, copy_diagonal);
    }
#ifdef HSR_DEBUG
    g_debug_pixel_coord = coords;
    float4 debug_value  = float4(0.0, 0.0, 0.0, 0.0);
#endif // HSR_DEBUG
    float4 specular_roughness = g_gbuffer_roughness.Load(int3(coords, 0));
    float roughness = specular_roughness.w;//g_extracted_roughness.Load(int3(coords, 0));

#ifdef USE_DEFERRED_RAYTRACING
    if (ray_index >= g_rw_ray_counter.Load(RAY_COUNTER_HW_HISTORY_OFFSET) || !FFX_DNSR_Reflections_IsGlossyReflection(roughness)) {
        FFX_Reflections_SkipRayGBuffer(ray_index);
        return;
    }
#else // USE_DEFERRED_RAYTRACING

#endif // USE_DEFERRED_RAYTRACING

    //////////////////////////////////////////
    ///////////  Screen Space  ///////////////
    //////////////////////////////////////////

#ifdef USE_SSR
    uint2  screen_size                     = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    float2 uv                              = float2(coords + 0.5) / float2(screen_size);
    bool   is_mirror                       = IsMirrorReflection(roughness);
    int    most_detailed_mip               = is_mirror ? 0 : g_most_detailed_mip;
    float2 mip_resolution                  = FFX_SSSR_GetMipResolution(screen_size, most_detailed_mip);
    float  z                               = FFX_SSSR_LoadDepth(uv * mip_resolution, most_detailed_mip);
    float3 screen_uv_space_ray_origin      = float3(uv, z);
    float3 view_space_ray                  = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 world_space_normal              = FFX_SSSR_LoadWorldSpaceNormal(coords);
    float3 view_space_surface_normal       = mul(float4(world_space_normal, 0), g_view).xyz;
    float3 view_space_ray_direction        = normalize(view_space_ray);
    float3 view_space_reflected_direction  = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, coords);
    screen_uv_space_ray_origin             = ProjectPosition(view_space_ray, g_proj);
    float3 screen_space_ray_direction      = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, g_proj);
    float3 world_space_reflected_direction = mul(float4(view_space_reflected_direction, 0), g_inv_view).xyz;
    float3 world_space_origin              = mul(float4(view_space_ray, 1), g_inv_view).xyz;
    float  world_ray_length                = 0.0;
    bool   valid_ray                       = !FFX_DNSR_Reflections_IsBackground(z)
                                            && FFX_DNSR_Reflections_IsGlossyReflection(roughness)
                                            && ray_index < g_rw_ray_counter.Load(RAY_COUNTER_SW_HISTORY_OFFSET)
                                            && all(coords < int2(g_frame_info.reflection_width, g_frame_info.reflection_height));
    bool   do_hw       = false;
    uint   hit_counter = 0;
    float3 hit         = float3(0.0, 0.0, 0.0);
    float  confidence  = 0.0;
    float3 world_space_hit = float3(0.0, 0.0, 0.0);
    float3 world_space_ray = float3(0.0, 0.0, 0.0);
    if (valid_ray) {
        bool valid_hit;

        hit = FFX_SSSR_HierarchicalRaymarch(screen_uv_space_ray_origin, screen_space_ray_direction, is_mirror, screen_size, most_detailed_mip, g_min_traversal_occupancy,
                                        g_max_traversal_intersections, valid_hit);



    world_space_hit  = ScreenSpaceToWorldSpace(hit);
    world_space_ray  = world_space_hit - world_space_origin.xyz;
    world_ray_length = length(world_space_ray);
    confidence       = valid_hit ? FFX_SSSR_ValidateHit(hit, uv, world_space_ray, screen_size,
                                                g_depth_buffer_thickness
                                                    // Add thickness for rough surfaces
                                                    + roughness * 10.0
                                                    // Add thickness with distance
                                                    + world_ray_length * g_frame_info.ssr_thickness_length_factor)
                        : 0;
    do_hw            = (g_hsr_mask & HSR_FLAGS_USE_RAY_TRACING) &&
                        // Add some bias with distance to push the confidence
                        (confidence < g_frame_info.ssr_confidence_threshold) && !FFX_DNSR_Reflections_IsBackground(hit.z)
                        // Ray tracing roughness threshold
                        ? true
                        : false;


    // Feedback flags
    // See ClassifyTiles.hlsl
    if (!do_hw)
        hit_counter = FFX_HITCOUNTER_SW_HIT_FLAG;
    else
        hit_counter = FFX_HITCOUNTER_SW_MISS_FLAG;

    uint hit_counter_per_pixel = hit_counter;
    // For variable rate shading we also need to copy counters
    {
        bool copy_horizontal;
        bool copy_vertical;
        bool copy_diagonal;
        UnpackRayCoords(packed_coords, coords, copy_horizontal, copy_vertical, copy_diagonal);

        if (copy_horizontal) hit_counter += hit_counter_per_pixel;
        if (copy_vertical) hit_counter += hit_counter_per_pixel;
        if (copy_diagonal) hit_counter += hit_counter_per_pixel;
    }
    }
    // Feedback information for the next frame
    if (g_hsr_mask & HSR_FLAGS_USE_HIT_COUNTER) {
        // One atomic increment per wave
        uint hit_counter_sum = WaveActiveSum(hit_counter);
        if (WaveIsFirstLane()) InterlockedAdd(g_rw_hit_counter[coords / 8], hit_counter_sum);
    }

    if (valid_ray) {
        if (do_hw) {
#    ifdef HSR_DEBUG
            debug_value = float4(1.0, 0.0, 0.0, 1.0);
#    endif // HSR_DEBUG
        } else {
            if (confidence < 0.9) {
                FFX_Reflections_WriteRadiance(packed_coords,
                                                float4(FFX_GetEnvironmentSample(world_space_origin, world_space_reflected_direction, 0.0), FFX_REFLECTIONS_SKY_DISTANCE));
            } else {
                // Found an intersection with the depth buffer -> We can lookup the color from the lit scene history.
#ifdef HSR_SHADING_USE_SCREEN
                float3 reflection_radiance = g_lit_scene_history.SampleLevel(g_linear_sampler, hit.xy - FFX_DNSR_Reflections_SampleMotionVector(hit.xy), 0.0).xyz;
#else // #ifdef HSR_SHADING_USE_SCREEN  
                // Or shade a new fragment with correct parameters
                float4 albedo_emission = g_gbuffer_full_albedo.SampleLevel(g_linear_sampler, hit.xy, 0.0).xyzw;
                ShadingInfo local_basis;
                local_basis.WorldPos  = world_space_hit;
                local_basis.Normal    = FFX_SSSR_SampleNormal(hit.xy);
                local_basis.View      = -world_space_reflected_direction;
                local_basis.Albedo    = albedo_emission.xyz;
                local_basis.Roughness = sqrt(g_extracted_roughness.SampleLevel(g_linear_sampler, hit.xy, 0.0).x);
                local_basis.Metalness = max(specular_roughness.x, max(specular_roughness.y, specular_roughness.z)) > 0.04 ? 1.0 : 0.0;

                float3 reflection_radiance = doPbrLighting(local_basis, myPerFrame, (1.0 - local_basis.Roughness) * myPerFrame.u_iblFactor);
                // Small hackery, see GLTFPbrPass-PS.hlsl
                reflection_radiance = lerp(reflection_radiance, albedo_emission.xyz * 4.0, albedo_emission.w);
#endif // #ifdef HSR_SHADING_USE_SCREEN
                FFX_Reflections_WriteRadiance(packed_coords, float4(reflection_radiance, world_ray_length));
            }
#    ifdef HSR_DEBUG
            debug_value = float4(0.0, 1.0, 0.0, 1.0);
#    endif // HSR_DEBUG
        }
    }
#    ifdef HSR_DEBUG
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY) debug_value = float4(0.0, 0.0, 0.0, 0.0);
#    endif // HSR_DEBUG

    // Fall back to env probe in case of rough surfaces
    if (do_hw && roughness > g_frame_info.rt_roughness_threshold) {
        do_hw = false;
        FFX_Reflections_WriteRadiance(packed_coords, float4(g_frame_info.perFrame.u_iblFactor * (1.0 - roughness) * FFX_GetEnvironmentSample(world_space_origin, world_space_reflected_direction, 0.0), FFX_REFLECTIONS_SKY_DISTANCE));
    }

    // Store ray coordinate for deferred HW ray tracing pass
    uint deferred_hw_wave_sum  = WaveActiveCountBits(do_hw);
    uint deferred_hw_wave_scan = WavePrefixCountBits(do_hw);
    uint deferred_hw_offset;
    if (WaveIsFirstLane() && deferred_hw_wave_sum) {
        g_rw_ray_counter.InterlockedAdd(RAY_COUNTER_HW_OFFSET, deferred_hw_wave_sum, deferred_hw_offset);
    }
    deferred_hw_offset = WaveReadLaneFirst(deferred_hw_offset);
    if (do_hw) {
        g_rw_hw_ray_list.Store(4 * (deferred_hw_offset + deferred_hw_wave_scan), packed_coords);
    }
    if (!valid_ray) return;
#endif // USE_SSR

    //////////////////////////////////////////
    ///////////  HW Ray Tracing  /////////////
    //////////////////////////////////////////
#ifdef USE_INLINE_RAYTRACING
    float3 world_space_origin;
    float3 world_space_reflected_direction;

    
        uint2  screen_size                    = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
        float2 uv                             = float2(coords + 0.5) / float2(screen_size);
        float  z                              = FFX_SSSR_LoadDepth(coords, 0);
        float3 screen_uv_space_ray_origin     = float3(uv, z);
        float3 view_space_ray                 = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
        float3 world_space_normal             = FFX_SSSR_LoadWorldSpaceNormal(coords);
        float3 view_space_surface_normal      = mul(float4(normalize(world_space_normal), 0), g_view).xyz;
        float3 view_space_ray_direction       = normalize(view_space_ray);
        float3 view_space_reflected_direction = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, coords);
        screen_uv_space_ray_origin            = ProjectPosition(view_space_ray, g_proj);
        float3 screen_space_ray_direction     = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, g_proj);
        world_space_reflected_direction       = normalize(mul(float4(view_space_reflected_direction, 0), g_inv_view).xyz);
        world_space_origin                    = mul(float4(view_space_ray, 1), g_inv_view).xyz;
    
#    ifdef HSR_DEBUG
    if (g_hsr_mask & HSR_FLAGS_USE_SCREEN_SPACE) debug_value = float4(0.0, 0.0, 1.0, 1.0);
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY) debug_value = float4(0.0, 0.0, 0.0, 0.0);
#    endif // HSR_DEBUG
    PackedRayGbuffer packed_gbuffer;
    {
        RayGbuffer default_gbuffer;
        default_gbuffer.normal           = float3(0.0, 0.0, 1.0);
        default_gbuffer.uv               = (0.0).xx;
        default_gbuffer.material_id      = 0;
        default_gbuffer.skip             = true;
        default_gbuffer.world_ray_length = FFX_REFLECTIONS_SKY_DISTANCE;
        packed_gbuffer                   = FFX_Reflections_PackGbuffer(default_gbuffer);
    }
    const float max_t = g_frame_info.max_raytraced_distance * exp(-roughness * g_frame_info.ray_length_exp_factor);

    {
        // Just a single ray, no transparency
        RayQuery<RAY_FLAG_NONE | RAY_FLAG_FORCE_OPAQUE> opaque_query;
        RayDesc                                         ray;
        ray.Origin    = world_space_origin + world_space_normal * 3.0e-3 * length(view_space_ray);
        ray.Direction = world_space_reflected_direction;
        ray.TMin      = 3.0e-3;
        ray.TMax      = max_t;
        opaque_query.TraceRayInline(g_global, 0, 0xff, ray);
        opaque_query.Proceed();
        if (opaque_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            uint          instance_id;
            uint          geometry_id;
            uint          surface_id;
            uint          triangle_id;
            Surface_Info  sinfo;
            Material_Info material;
            Instance_Info iinfo;
            float2        bary;
            uint3         face3;
            float3x3      obj_to_world;

            obj_to_world =
                float3x3(opaque_query.CommittedObjectToWorld3x4()[0].xyz, opaque_query.CommittedObjectToWorld3x4()[1].xyz, opaque_query.CommittedObjectToWorld3x4()[2].xyz);
            bary        = opaque_query.CommittedTriangleBarycentrics();
            instance_id = opaque_query.CommittedInstanceID();
            geometry_id = opaque_query.CommittedGeometryIndex();
            triangle_id = opaque_query.CommittedPrimitiveIndex();

            // bool opaque = true;
            iinfo      = g_rw_instance_info.Load<Instance_Info>(sizeof(Instance_Info) * instance_id);
            surface_id = g_rw_surface_id.Load(4 * (iinfo.surface_id_table_offset + geometry_id /*+ (opaque ? 0 : iinfo.num_opaque_surfaces)*/));
            sinfo      = g_rw_surface_info.Load<Surface_Info>(sizeof(Surface_Info) * surface_id);
            material   = g_rw_material_info.Load<Material_Info>(sizeof(Material_Info) * sinfo.material_id);
            if (sinfo.index_type == SURFACE_INFO_INDEX_TYPE_U16) {
                FFX_Fetch_Face_Indices_U16(/* out */ face3, /* in */ sinfo.index_offset, /* in */ triangle_id);
            } else { // SURFACE_INFO_INDEX_TYPE_U32
                FFX_Fetch_Face_Indices_U32(/* out */ face3, /* in */ sinfo.index_offset, /* in */ triangle_id);
            }
            float3 normal;
            float4 tangent;
            float2 uv;
            // Fetch interpolated local geometry basis
            FFX_Fetch_Local_Basis(/* in */ sinfo, /* in */ face3, /* in */ bary, /* out */ uv, /* out */ normal, /* out */ tangent);
            float4 albedo = (1.0).xxxx;
            if (albedo.w > 0.5) {
                normal        = normalize(rotate(obj_to_world, normal));
                tangent.xyz = rotate(obj_to_world, tangent.xyz);
                if (material.normal_tex_id >= 0 && any(abs(tangent.xyz) > 0.0f)) {
                    tangent.xyz       = normalize(tangent.xyz);
                    float3 binormal   = normalize(cross(normal, tangent.xyz) * tangent.w);
                    float3 normal_rgb = float3(0.0f, 0.0f, 1.0f);
                    normal_rgb        = g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.normal_tex_id)]
                                    .SampleLevel(g_wrap_linear_sampler, uv, 2.0)
                                    .rgb;
                    normal_rgb.z = sqrt(saturate(1.0 - normal_rgb.r * normal_rgb.r - normal_rgb.g * normal_rgb.g));        
                    normal = normalize(normal_rgb.z * normal + (2.0f * normal_rgb.x - 1.0f) * tangent.xyz + (2.0f * normal_rgb.y - 1.0f) * binormal);
                }
            }

            if (material.albedo_tex_id >= 0) {
                albedo = albedo * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.albedo_tex_id)].SampleLevel(g_wrap_linear_sampler, uv, 0).xyzw;
            }
            RayGbuffer gbuffer;
            gbuffer.material_id      = sinfo.material_id;
            gbuffer.normal           = normal;
            gbuffer.uv               = uv;
            gbuffer.world_ray_length = opaque_query.CommittedRayT();
            #ifdef HSR_TRANSPARENT_QUERY
              gbuffer.skip             = albedo.w < 0.5;
            #else // HSR_TRANSPARENT_QUERY
              gbuffer.skip             = false;
            #endif // HSR_TRANSPARENT_QUERY
            packed_gbuffer           = FFX_Reflections_PackGbuffer(gbuffer);
        }
    }
#    ifdef HSR_TRANSPARENT_QUERY
    if (FFX_Reflections_GbufferIsSkip(packed_gbuffer)) { // Try transparent query
#        ifdef HSR_DEBUG
        if (g_hsr_mask & HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY) debug_value = float4(0.0, 1.0, 0.0, 1.0);
#        endif // HSR_DEBUG
        RayQuery<RAY_FLAG_NONE> transparent_query;
        RayDesc                 ray;
        ray.Origin    = world_space_origin + world_space_normal * 3.0e-3 * length(view_space_ray);
        ray.Direction = world_space_reflected_direction;
        ray.TMin      = FFX_Reflections_GbufferGetRayLength(packed_gbuffer) - 1.0e-2;
        ray.TMax      =  max_t - ray.TMin;
        transparent_query.TraceRayInline(g_transparent,
                                         0, // OR'd with flags above
                                         0xff, ray);
        while (transparent_query.Proceed()) {
            if (transparent_query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
                uint          instance_id;
                uint          geometry_id;
                uint          surface_id;
                uint          triangle_id;
                Surface_Info  sinfo;
                Material_Info material;
                Instance_Info iinfo;
                float2        bary;
                uint3         face3;
                float3x3      obj_to_world;
                obj_to_world = float3x3(transparent_query.CandidateObjectToWorld3x4()[0].xyz, transparent_query.CandidateObjectToWorld3x4()[1].xyz,
                                        transparent_query.CandidateObjectToWorld3x4()[2].xyz);
                bary         = transparent_query.CandidateTriangleBarycentrics();
                instance_id  = transparent_query.CandidateInstanceID();
                geometry_id  = transparent_query.CandidateGeometryIndex();
                triangle_id  = transparent_query.CandidatePrimitiveIndex();

                // bool opaque = true;
                iinfo      = g_rw_instance_info.Load<Instance_Info>(sizeof(Instance_Info) * instance_id);
                surface_id = g_rw_surface_id.Load(4 * (iinfo.surface_id_table_offset + geometry_id /*+ (opaque ? 0 : iinfo.num_opaque_surfaces)*/));
                sinfo      = g_rw_surface_info.Load<Surface_Info>(sizeof(Surface_Info) * surface_id);
                material   = g_rw_material_info.Load<Material_Info>(sizeof(Material_Info) * sinfo.material_id);
                if (sinfo.index_type == SURFACE_INFO_INDEX_TYPE_U16) {
                    FFX_Fetch_Face_Indices_U16(/* out */ face3, /* in */ sinfo.index_offset, /* in */ triangle_id);
                } else { // SURFACE_INFO_INDEX_TYPE_U32
                    FFX_Fetch_Face_Indices_U32(/* out */ face3, /* in */ sinfo.index_offset, /* in */ triangle_id);
                }
                float3 normal;
                float2 uv;
                // Fetch interpolated local geometry basis
                FFX_Fetch_Local_Basis(/* in */ sinfo, /* in */ face3, /* in */ bary, /* out */ uv, /* out */ normal);
                float4 albedo = (1.0).xxxx;
                if (material.albedo_tex_id >= 0) {
                    albedo = albedo * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.albedo_tex_id)].SampleLevel(g_wrap_linear_sampler, uv, 0).xyzw;
                }
                
                normal = normalize(rotate(obj_to_world, normal));

                if (albedo.w > 0.1) {
#        ifdef HSR_DEBUG
                    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY) debug_value = float4(1.0, 0.0, 1.0, 1.0);
#        endif // HSR_DEBUG
                    RayGbuffer gbuffer;
                    gbuffer.material_id      = sinfo.material_id;
                    gbuffer.normal           = normal;
                    gbuffer.uv               = uv;
                    gbuffer.world_ray_length = transparent_query.CandidateTriangleRayT();
                    gbuffer.skip             = false;
                    packed_gbuffer           = FFX_Reflections_PackGbuffer(gbuffer);
                    transparent_query.CommitNonOpaqueTriangleHit();
                }
            }
        }
    }
#    endif // HSR_TRANSPARENT_QUERY

    FFX_Reflections_StoreRayGBuffer(ray_index, packed_gbuffer);

#endif // USE_INLINE_RAYTRACING

#ifdef HSR_DEBUG
#    ifdef USE_SSR
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_WAVES) debug_value = heatmapGradient(frac(float(group_id) / 100.0)).xyzz;
#    else // USE_SSR
    if (g_hsr_mask & HSR_FLAGS_VISUALIZE_WAVES) return;
#    endif // USE_SSR
    if ((g_hsr_mask & HSR_FLAGS_VISUALIZE_HIT_COUNTER) || (g_hsr_mask & HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY)) {
        bool clear_debug = false;
        if (g_hsr_mask & HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY) clear_debug = true;
        g_rw_debug[coords] = (!clear_debug ? g_rw_debug[coords] : (0.0).xxxx) + debug_value;
        uint2 copy_target  = coords ^ 0b1; // Flip last bit to find the mirrored coords along the x and y axis within a quad.
        {
            bool copy_horizontal;
            bool copy_vertical;
            bool copy_diagonal;
            UnpackRayCoords(packed_coords, coords, copy_horizontal, copy_vertical, copy_diagonal);
            if (copy_horizontal) {
                uint2 copy_coords       = uint2(copy_target.x, coords.y);
                g_rw_debug[copy_coords] = (!clear_debug ? g_rw_debug[copy_coords] : (0.0).xxxx) + debug_value;
            }
            if (copy_vertical) {
                uint2 copy_coords       = uint2(coords.x, copy_target.y);
                g_rw_debug[copy_coords] = (!clear_debug ? g_rw_debug[copy_coords] : (0.0).xxxx) + debug_value;
            }
            if (copy_diagonal) {
                uint2 copy_coords       = copy_target;
                g_rw_debug[copy_coords] = (!clear_debug ? g_rw_debug[copy_coords] : (0.0).xxxx) + debug_value;
            }
        }
    }
#endif // HSR_DEBUG
}


// Kernel for deferred shading of the results of ray traced intersection
// Each thread loads a RayGBuffer and evaluates lights+shadows+IBL - application decides how to shade
[numthreads(32, 1, 1)]
void DeferredShade(uint group_index : SV_GroupIndex,
                   uint group_id    : SV_GroupID) {
    uint ray_index     = group_id * 32 + group_index;
    uint packed_coords = g_rw_hw_ray_list.Load(sizeof(uint) * ray_index);
    int2 coords;
    bool copy_horizontal;
    bool copy_vertical;
    bool copy_diagonal;
    UnpackRayCoords(packed_coords, coords, copy_horizontal, copy_vertical, copy_diagonal);

    uint2  screen_size                     = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    float2 uv                              = float2(coords + 0.5) / float2(screen_size);
    float3 world_space_normal              = FFX_SSSR_LoadWorldSpaceNormal(coords);
    float  roughness                       = g_extracted_roughness.Load(int3(coords, 0));
    float  z                               = FFX_SSSR_LoadDepth(coords, 0);
    float3 screen_uv_space_ray_origin      = float3(uv, z);
    float3 view_space_ray                  = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 view_space_surface_normal       = mul(float4(normalize(world_space_normal), 0), g_view).xyz;
    float3 view_space_ray_direction        = normalize(view_space_ray);
    float3 view_space_reflected_direction  = SampleReflectionVector(view_space_ray_direction, view_space_surface_normal, roughness, coords);
    screen_uv_space_ray_origin             = ProjectPosition(view_space_ray, g_proj);
    float3 screen_space_ray_direction      = ProjectDirection(view_space_ray, view_space_reflected_direction, screen_uv_space_ray_origin, g_proj);
    float3 world_space_reflected_direction = normalize(mul(float4(view_space_reflected_direction, 0), g_inv_view).xyz);
    float3 world_space_origin              = mul(float4(view_space_ray, 1), g_inv_view).xyz;

    RayGbuffer gbuffer = FFX_Reflections_UnpackGbuffer(FFX_Reflections_LoadRayGBuffer(ray_index));

    if (gbuffer.skip) {
        // Fall back to the pre-filtered sample or a stochastic one
        float3 reflection_radiance = FFX_GetEnvironmentSample(world_space_origin, world_space_reflected_direction, 0.0);
        FFX_Reflections_WriteRadiance(packed_coords, float4(reflection_radiance, FFX_REFLECTIONS_SKY_DISTANCE));
        return;
    }
    float3 world_space_hit = world_space_origin + world_space_reflected_direction * gbuffer.world_ray_length;
    float4 projected       = mul(mul(float4(world_space_hit, 1), g_view), g_proj).xyzw;
    float2 hituv           = (projected.xy / projected.w) * float2(0.5, -0.5) + float2(0.5, 0.5);
#ifdef HSR_SHADING_USE_SCREEN
    if (all(abs(projected.xy) < projected.w) && projected.w > 0.0 &&
        abs(FFX_DNSR_Reflections_GetLinearDepth(hituv, projected.z / projected.w) - FFX_DNSR_Reflections_GetLinearDepth(hituv, FFX_DNSR_Reflections_SampleDepth(hituv))) <
            g_depth_buffer_thickness) {
        float3 reflection_radiance = g_lit_scene_history.SampleLevel(g_wrap_linear_sampler, hituv - FFX_DNSR_Reflections_SampleMotionVector(hituv), 0.0).xyz;
        FFX_Reflections_WriteRadiance(packed_coords, float4(reflection_radiance, gbuffer.world_ray_length));
    } else {
#endif // #ifdef HSR_SHADING_USE_SCREEN
        Material_Info material = g_rw_material_info.Load<Material_Info>(sizeof(Material_Info) * gbuffer.material_id);
        float4        albedo   = float4(material.albedo_factor_x, material.albedo_factor_y, material.albedo_factor_z, material.albedo_factor_w);
        if (material.albedo_tex_id >= 0) {
            albedo = albedo * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.albedo_tex_id)]
                .SampleLevel(g_wrap_linear_sampler, gbuffer.uv, 0).xyzw;
        }
        float4 arm = float4(1.0, material.arm_factor_y, material.arm_factor_z, 1.0);
        if (material.arm_tex_id >= 0) {
            arm = arm * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.arm_tex_id)].SampleLevel(g_wrap_linear_sampler, gbuffer.uv, 0).xyzw;
        }
        float4 emission = float4(material.emission_factor_x, material.emission_factor_y, material.emission_factor_z, 1.0);
        if (material.emission_tex_id >= 0) {
            emission = emission * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.emission_tex_id)]
                .SampleLevel(g_wrap_linear_sampler, gbuffer.uv, 0).xyzw;
        }
        ShadingInfo local_basis;
        local_basis.WorldPos  = world_space_hit;
        local_basis.Normal    = gbuffer.normal;
        local_basis.View      = -world_space_reflected_direction;
        local_basis.Albedo    = albedo.xyz;
        local_basis.Roughness = arm.y;
        local_basis.Metalness = arm.z;

        float3 reflection_radiance = doPbrLighting(local_basis, myPerFrame, (1.0 - local_basis.Roughness) * myPerFrame.u_iblFactor);

        reflection_radiance += emission.xyz * myPerFrame.u_EmissiveFactor;

        FFX_Reflections_WriteRadiance(packed_coords, float4(reflection_radiance, gbuffer.world_ray_length));
#ifdef HSR_SHADING_USE_SCREEN
    }
#endif // #ifdef HSR_SHADING_USE_SCREEN
    
}
