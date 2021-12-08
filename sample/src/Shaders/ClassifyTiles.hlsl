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
Texture2D<float4> g_gbuffer_roughness; // Current GBuffer/specular_roughness in reflection target resolution 
Texture2D<float4> g_gbuffer_depth_history; // Previous GBuffer/depth in reflection target resolution 
Texture2D<min16float> g_radiance_variance_1; // Variance target 1 - history 
SamplerState g_linear_sampler;
Texture2D<float4> g_gbuffer_normal; // Current GBuffer/normal in reflection target resolution 
Texture2D<float4> g_gbuffer_normal_history; // Previous GBuffer/normal in reflection target resolution 
Texture2D<uint> g_hit_counter_history;
RWTexture2D<uint> g_rw_hit_counter;
RWByteAddressBuffer g_rw_ray_list;
RWByteAddressBuffer g_rw_hw_ray_list;
Texture2D<float4> g_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
RWTexture2D<float4> g_rw_extracted_roughness; // Current extracted GBuffer/roughness in reflection target resolution 
RWByteAddressBuffer g_rw_ray_counter;
RWTexture2D<min16float3> g_rw_radiance_0; // Radiance target 0 - intersection results 
RWByteAddressBuffer g_rw_denoise_tile_list;
RWTexture2D<float4> g_rw_debug;
RWTexture2D<float4> g_rw_random_number_image; // Baked random numbers 128x128 for the current frame 
#endif

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
// Hash without Sine
// https://www.shadertoy.com/view/4djSRW
// MIT License...
/* Copyright (c)2014 David Hoskins.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/

///  2 out, 2 in...
float2 hash22(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * float3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return frac((p3.xx+p3.yz)*p3.zy);

}
//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------

//////////////////////////////////
///// Load/Store Interface ///////
//////////////////////////////////

struct PushConstants {
    uint padding0;
    uint depth_mip_bias; // Used in 1/4 mode to use the 2nd mip level of the depth
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc : DX12_PUSH_CONSTANTS;

// Input
float  FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate) { return g_gbuffer_roughness.Load(int3(pixel_coordinate, 0)).w; }
float  FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate) { return g_gbuffer_depth.Load(int3(pixel_coordinate, 0)).x; }
float  FFX_DNSR_Reflections_SampleVarianceHistory(float2 uv) { return g_radiance_variance_1.SampleLevel(g_linear_sampler, uv, 0.0f); }
float3 FFX_DNSR_Reflections_LoadWorldSpaceNormal(int2 pixel_coordinate) { return normalize(2 * g_gbuffer_normal.Load(int3(pixel_coordinate, 0)).xyz - 1); }
float3 FFX_DNSR_Reflections_LoadWorldSpaceNormalHistory(int2 pixel_coordinate) { return normalize(2 * g_gbuffer_normal_history.Load(int3(pixel_coordinate, 0)).xyz - 1); }
uint   FFX_DNSR_Reflections_LoadHitCounterHistory(int2 pixel_coordinate) { return g_hit_counter_history[pixel_coordinate]; }
float2 FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate) { return g_motion_vector.Load(int3(pixel_coordinate, 0)).xy * float2(0.5, -0.5); }
float2 FFX_DNSR_Reflections_GetRandom(uint2 index) {
    float v    = 0.152f;
    float2 pos = (float2(index) * v + float(g_frame_info.frame_index) / 60.0f  * 1500.0f + 50.0f);
    return hash22(pos);
}
float2 FFX_DNSR_Reflections_GetRandomLastFrame(uint2 index) {
    float v    = 0.152f;
    float2 pos = (float2(index) * v + float(g_frame_info.frame_index - 1) / 60.0f  * 1500.0f + 50.0f);
    return hash22(pos);
}
bool   FFX_DNSR_Reflections_IsConverged(int2 pixel_coordinate, float2 uv) {
    float2 motion_vector = FFX_DNSR_Reflections_LoadMotionVector(int2(pixel_coordinate));
    return FFX_DNSR_Reflections_SampleVarianceHistory(uv - motion_vector) < g_frame_info.vrt_variance_threshold;
}
float2 FFX_DNSR_Reflections_GetSurfaceReprojection(float2 uv, float2 motion_vector) {
    // Reflector position reprojection
    float2 history_uv = uv - motion_vector;
    return history_uv;
}
bool FFX_DNSR_Reflections_IsSW(float hitcounter, float misscounter, float rnd) {
    return rnd <= (
                      // Turn a random tile full hybrid once in a while to get the opportunity for testing HiZ traversal
                      + g_frame_info.hybrid_spawn_rate + hitcounter - misscounter * g_frame_info.hybrid_miss_weight);
}

// Output
float FFX_DNSR_Reflections_StoreExtractedRoughness(int2 pixel_coordinate, float roughness) { return g_rw_extracted_roughness[pixel_coordinate] = roughness.xxxx; }
void  FFX_DNSR_Reflections_IncrementRayCounterSW(uint value, out uint original_value) { g_rw_ray_counter.InterlockedAdd(RAY_COUNTER_SW_OFFSET, value, original_value); }
void  FFX_DNSR_Reflections_IncrementRayCounterHW(uint value, out uint original_value) { g_rw_ray_counter.InterlockedAdd(RAY_COUNTER_HW_OFFSET, value, original_value); }
void  FFX_DNSR_Reflections_StoreRaySW(int index, uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal) {
    g_rw_ray_list.Store(4 * index, PackRayCoords(ray_coord, copy_horizontal, copy_vertical, copy_diagonal));
}
void  FFX_DNSR_Reflections_StoreRaySWHelper(int index) {
    g_rw_ray_list.Store(4 * index, 0xffffffffu);
}
void FFX_DNSR_Reflections_StoreRayHW(int index, uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal) {
    g_rw_hw_ray_list.Store(4 * index, PackRayCoords(ray_coord, copy_horizontal, copy_vertical, copy_diagonal));
}
void FFX_DNSR_Reflections_StoreHitCounter(int2 pixel_coordinate, int value) { g_rw_hit_counter[pixel_coordinate.xy] = value; }
// In case no ray is traced we need to clear the buffers
void FFX_DNSR_Reflections_FillEnvironment(uint2 ray_coord, float factor) {
    // Fall back to the environment probe
    uint2  screen_size                     = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    float2 uv                              = (ray_coord + 0.5) / screen_size;
    float3 world_space_normal              = FFX_DNSR_Reflections_LoadWorldSpaceNormal(ray_coord);
    float  roughness                       = FFX_DNSR_Reflections_LoadRoughness(ray_coord);
    float  z                               = FFX_DNSR_Reflections_LoadDepth(ray_coord);
    float3 screen_uv_space_ray_origin      = float3(uv, z);
    float3 view_space_ray                  = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 view_space_ray_direction        = normalize(view_space_ray);
    float3 view_space_surface_normal       = mul(float4(normalize(world_space_normal), 0), g_view).xyz;
    float3 view_space_reflected_direction  = reflect(view_space_ray_direction, view_space_surface_normal);
    float3 world_space_reflected_direction = mul(float4(view_space_reflected_direction, 0), g_inv_view).xyz;
    float3 world_space_ray_origin          = mul(float4(view_space_ray, 1), g_inv_view).xyz;
    float3 env_sample                      = (1.0 - roughness) * FFX_GetEnvironmentSample(world_space_ray_origin, normalize(world_space_reflected_direction), roughness);

    g_rw_radiance_0[ray_coord]     = env_sample.xyzz * factor;
}
void FFX_DNSR_Reflections_ZeroBuffers(uint2 dispatch_thread_id) {
    g_rw_radiance_0[dispatch_thread_id]          = (0.0f).xxx;
}
void FFX_DNSR_Reflections_StoreDenoiseTile(uint2 tile_coord) {
    uint tile_index;
    g_rw_ray_counter.InterlockedAdd(RAY_COUNTER_DENOISE_OFFSET, 1, tile_index);
    g_rw_denoise_tile_list.Store(4 * tile_index, tile_coord.x | (tile_coord.y << 16));
}
//////////////////////////////////
//////////////////////////////////
//////////////////////////////////

#define FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED

#include "ffx_denoiser_reflections_common.h"

bool FFX_DNSR_Reflections_IsBaseRay(uint2 dispatch_thread_id, uint samples_per_quad) {
    switch (samples_per_quad) {
    case 1: return ((dispatch_thread_id.x & 1) | (dispatch_thread_id.y & 1)) == 0; // Deactivates 3 out of 4 rays
    case 2: return (dispatch_thread_id.x & 1) == (dispatch_thread_id.y & 1);       // Deactivates 2 out of 4 rays. Keeps diagonal.
    default:                                                                       // case 4:
        return true;
    }
}

groupshared uint g_FFX_DNSR_TileCount;
groupshared int g_FFX_DNSR_TileClass;
groupshared int g_FFX_DNSR_SWCount;
groupshared int g_FFX_DNSR_SWCountTotal;
groupshared int g_FFX_DNSR_base_ray_index_sw;

#define TILE_CLASS_FULL_SW 0
#define TILE_CLASS_HALF_SW 1
#define TILE_CLASS_FULL_HW 2

void FFX_DNSR_Reflections_ClassifyTiles(int2 dispatch_thread_id, int2 group_thread_id,
                                        float roughness,
                                        float3 view_space_surface_normal,
                                        float depth,
                                        int2 screen_size,
                                        uint samples_per_quad,
                                        bool enable_temporal_variance_guided_tracing,
                                        bool enable_hitcounter,
                                        bool enable_screen_space_tracing,
                                        bool enable_hw_ray_tracing
                                        ) {
    int flat_group_thread_id   = group_thread_id.x + group_thread_id.y * 8;
    int wave_id                = flat_group_thread_id / 32;
    bool is_first_lane_of_wave = WaveIsFirstLane();

    if (all(group_thread_id == 0)) {
        // Initialize group shared variables
        g_FFX_DNSR_TileCount         = 0;
        g_FFX_DNSR_SWCount           = 0;
        g_FFX_DNSR_SWCountTotal      = 0;
        g_FFX_DNSR_base_ray_index_sw = 0;

#ifdef FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED
        // Initialize per 8x8 tile hit counter
        if (enable_hitcounter) {
            if (// In case we do hybrid
                enable_screen_space_tracing && enable_hw_ray_tracing
            ) {
                // Feedback counters
                // See Intersect.hlsl
                uint hitcounter = 0;

                // Use surface motion vectors of one of the 8x8 pixels in the tile to reproject statistics from the previous frame
                // Helps a lot in movement to sustain temoporal coherence
#define FFX_DNSR_CLASSIFICATION_REPROJECT_HITCOUNTER
#ifdef FFX_DNSR_CLASSIFICATION_REPROJECT_HITCOUNTER
                {
                    // Grab motion vector from a random point in the subgroup
                    float2 xi                      = FFX_DNSR_Reflections_GetRandom(dispatch_thread_id.xy / 8);
                    int2   mix                     = int2(xi * 8.0f);
                    float2 motion_vector           = FFX_DNSR_Reflections_LoadMotionVector(int2(dispatch_thread_id) + mix);
                    float2 uv8                     = (float2(dispatch_thread_id.xy + mix)) / FFX_DNSR_Reflections_RoundUp8(screen_size);
                    float2 surface_reprojection_uv = FFX_DNSR_Reflections_GetSurfaceReprojection(uv8, motion_vector);
                    hitcounter                     = FFX_DNSR_Reflections_LoadHitCounterHistory(int2(surface_reprojection_uv * (FFX_DNSR_Reflections_RoundUp8(screen_size) / 8)));
                }
#endif // FFX_DNSR_CLASSIFICATION_REPROJECT_HITCOUNTER

                // Use 3x3 region to grab the biggest success rate and create a safe band of hybrid rays to hide artefacts in movements
#define FFX_DNSR_CLASSIFICATION_SAFEBAND
#ifdef FFX_DNSR_CLASSIFICATION_SAFEBAND
                uint  same_pixel_hitcounter = 0;
                // We need a safe band for some geometry not in the BVH to avoid fireflies 
                const int radius = 1;
                for (int y = -radius; y <= radius; y++) {
                    for (int x = -radius; x <= radius; x++) {
                        uint  pt = FFX_DNSR_Reflections_LoadHitCounterHistory(dispatch_thread_id.xy / 8 + int2(x, y));
                        if (FFX_Hitcounter_GetSWHits(pt) > FFX_Hitcounter_GetSWHits(same_pixel_hitcounter))
                            same_pixel_hitcounter = pt;
                    }
                }
#else // FFX_DNSR_CLASSIFICATION_SAFEBAND
                uint  same_pixel_hitcounter = FFX_DNSR_Reflections_LoadHitCounterHistory(dispatch_thread_id.xy / 8);
#endif // FFX_DNSR_CLASSIFICATION_SAFEBAND

                // Again compare with the same pixel and Pick the one with the biggest success rate
                if (FFX_Hitcounter_GetSWHits(hitcounter) < FFX_Hitcounter_GetSWHits(same_pixel_hitcounter))
                    hitcounter = same_pixel_hitcounter;

                float rnd                   = FFX_DNSR_Reflections_GetRandom(dispatch_thread_id.xy / 8).x;
                float rnd_last              = FFX_DNSR_Reflections_GetRandomLastFrame(dispatch_thread_id.xy / 8).x;
                float sw_hitcount_new       = float(FFX_Hitcounter_GetSWHits(hitcounter));
                float sw_hitcount_old       = float(FFX_Hitcounter_GetOldSWHits(hitcounter));
                float sw_misscount_new      = float(FFX_Hitcounter_GetSWMisses(hitcounter));
                float sw_misscount_old      = float(FFX_Hitcounter_GetOldSWMisses(hitcounter));
                int   new_class             = FFX_DNSR_Reflections_IsSW(sw_hitcount_new, sw_misscount_new, rnd);
                int   old_class             = FFX_DNSR_Reflections_IsSW(sw_hitcount_old, sw_misscount_old, rnd_last);

                // To make transition less obvious we do and extra checkerboard stage
                if (new_class == old_class) {
                    if (new_class) {
                        g_FFX_DNSR_TileClass = TILE_CLASS_FULL_SW;
                    } else {
                        g_FFX_DNSR_TileClass = TILE_CLASS_FULL_HW;
                    }
                } else {
                    g_FFX_DNSR_TileClass = TILE_CLASS_HALF_SW;
                }
                sw_hitcount_old                         = sw_hitcount_new;
                sw_misscount_old                        = sw_misscount_new;
                FFX_DNSR_Reflections_StoreHitCounter(dispatch_thread_id.xy / 8,
                                (uint(clamp(sw_hitcount_old, 0.0f, 255.0f)) << 8)
                                | (uint(clamp(sw_misscount_old, 0.0f, 255.0f)) << 24)
                );
            }
        } else {
            g_FFX_DNSR_TileClass = TILE_CLASS_FULL_SW;
        }
#endif // FFX_HYBRID_REFLECTIONS
    }
    GroupMemoryBarrierWithGroupSync();

    // First we figure out on a per thread basis if we need to shoot a reflection ray
    bool is_on_screen = (dispatch_thread_id.x < screen_size.x) && (dispatch_thread_id.y < screen_size.y);
    // Allow for additional engine side checks. For example engines could additionally only cast reflection rays for specific depth ranges
    bool is_surface = !FFX_DNSR_Reflections_IsBackground(depth);
    // Don't shoot a ray on very rough surfaces
    bool is_glossy_reflection = is_surface && FFX_DNSR_Reflections_IsGlossyReflection(roughness);
    bool needs_ray            = is_on_screen && is_glossy_reflection;

    // Decide which ray to keep
    bool is_base_ray  = FFX_DNSR_Reflections_IsBaseRay(dispatch_thread_id, samples_per_quad);
    bool is_converged = true;
    if (enable_temporal_variance_guided_tracing) {
        float2 uv    = (dispatch_thread_id + 0.5) / screen_size;
        is_converged = FFX_DNSR_Reflections_IsConverged(dispatch_thread_id, uv);
    }

    needs_ray        = needs_ray && (is_base_ray || !is_converged);

    // Extra check for back-facing rays, fresnel, mirror etc.
    if (abs(view_space_surface_normal.z) > g_frame_info.reflections_backfacing_threshold) {
        FFX_DNSR_Reflections_FillEnvironment(dispatch_thread_id, g_frame_info.perFrame.u_iblFactor);
        needs_ray = false;
    }

    // We need denoiser even for mirrors since ssr/hw transition ends up creating poping tile firefies.
    bool needs_denoiser = is_glossy_reflection;

    // Next we have to figure out for which pixels that ray is creating the values for. Thus, if we have to copy its value horizontal, vertical or across.
    bool require_copy    = !needs_ray && needs_denoiser; // Our pixel only requires a copy if we want to run a denoiser on it but don't want to shoot a ray for it.
    bool copy_horizontal = (samples_per_quad != 4) && is_base_ray && WaveReadLaneAt(require_copy, WaveGetLaneIndex() ^ 0b01); // QuadReadAcrossX
    bool copy_vertical   = (samples_per_quad == 1) && is_base_ray && WaveReadLaneAt(require_copy, WaveGetLaneIndex() ^ 0b10); // QuadReadAcrossY
    bool copy_diagonal   = (samples_per_quad == 1) && is_base_ray && WaveReadLaneAt(require_copy, WaveGetLaneIndex() ^ 0b11); // QuadReadAcrossDiagonal

    bool needs_sw_ray = true;

    // In case there's only software rays we don't do hybridization
#ifdef FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED
    needs_sw_ray = needs_ray && enable_screen_space_tracing;

    bool needs_hw_ray = false;
    if (enable_hw_ray_tracing && roughness < g_frame_info.rt_roughness_threshold) {
        bool checkerboard = ((group_thread_id.x ^ group_thread_id.y) & 1) == 0;
        needs_sw_ray      = needs_sw_ray
                            && ((g_FFX_DNSR_TileClass == TILE_CLASS_FULL_SW ? true :
                                    (g_FFX_DNSR_TileClass == TILE_CLASS_HALF_SW ?
                                                            checkerboard :
                                                            false)
                                    )
                                );
        needs_hw_ray      = needs_ray && !needs_sw_ray;
    }
#endif // FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED

    uint local_ray_index_in_wave_sw = WavePrefixCountBits(needs_sw_ray);
    uint wave_ray_offset_in_group_sw;
    uint wave_ray_count_sw          = WaveActiveCountBits(needs_sw_ray);

#ifdef FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED
    uint local_ray_index_in_wave_hw = WavePrefixCountBits(needs_hw_ray);
    uint wave_ray_count_hw          = WaveActiveCountBits(needs_hw_ray);
    uint base_ray_index_hw          = 0;
#endif // FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED

    if (is_first_lane_of_wave) {
        if (wave_ray_count_sw)
            InterlockedAdd(g_FFX_DNSR_SWCount, wave_ray_count_sw, wave_ray_offset_in_group_sw);
            
#ifdef FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED
        if (wave_ray_count_hw)
            FFX_DNSR_Reflections_IncrementRayCounterHW(wave_ray_count_hw, base_ray_index_hw);
#endif // FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED

    }

    base_ray_index_hw           = WaveReadLaneFirst(base_ray_index_hw);
    wave_ray_offset_in_group_sw = WaveReadLaneFirst(wave_ray_offset_in_group_sw);

    GroupMemoryBarrierWithGroupSync();
    if (flat_group_thread_id == 0 && g_FFX_DNSR_SWCount > 0) {
        // [IMPORTANT] We need to round up to the multiple of 32 for software rays, because of the atomic increment coalescing optimization
        g_FFX_DNSR_SWCountTotal = g_FFX_DNSR_SWCount < 32 ? 32 : (g_FFX_DNSR_SWCount > 32 ? 64 : 32);
        FFX_DNSR_Reflections_IncrementRayCounterSW(g_FFX_DNSR_SWCountTotal, g_FFX_DNSR_base_ray_index_sw);
    }
    GroupMemoryBarrierWithGroupSync();

    if (needs_sw_ray) {
        int ray_index_sw = g_FFX_DNSR_base_ray_index_sw + wave_ray_offset_in_group_sw + local_ray_index_in_wave_sw;
        FFX_DNSR_Reflections_StoreRaySW(ray_index_sw, dispatch_thread_id, copy_horizontal, copy_vertical, copy_diagonal);
    }

#ifdef FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED
    else if (needs_hw_ray) {
        int ray_index_hw = base_ray_index_hw + local_ray_index_in_wave_hw;
        FFX_DNSR_Reflections_StoreRayHW(ray_index_hw, dispatch_thread_id, copy_horizontal, copy_vertical, copy_diagonal);
    }
#endif // FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED

    if (flat_group_thread_id < g_FFX_DNSR_SWCountTotal - g_FFX_DNSR_SWCount) {
        // [IMPORTANT] We need to round up to the multiple of 32 for software rays, because of the atomic increment coalescing optimization
        // Emit helper(dead) lanes to fill up 32 lanes per 8x8 tile
        int ray_index_sw = g_FFX_DNSR_base_ray_index_sw + g_FFX_DNSR_SWCount + flat_group_thread_id;
        FFX_DNSR_Reflections_StoreRaySWHelper(ray_index_sw);
    }

    // We only need denoiser if we trace any rays in the tile
    if (is_first_lane_of_wave && (wave_ray_count_sw
#ifdef FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED
    || wave_ray_count_hw
#endif // FFX_DNSR_CLASSIFICATION_HW_RAYTRACING_ENABLED

    )) {
        InterlockedAdd(g_FFX_DNSR_TileCount, 1);
    }

    GroupMemoryBarrierWithGroupSync(); // Wait until all waves wrote into g_FFX_DNSR_TileCount
    
    if (g_FFX_DNSR_TileCount) {
        if (all(group_thread_id == 0)) {
            FFX_DNSR_Reflections_StoreDenoiseTile(dispatch_thread_id);
        }
    }

    if (
        !needs_ray && !require_copy // Discarded for some reason
        || needs_ray && !needs_hw_ray && !needs_sw_ray // Or needs a ray but was discarded for some other reason
    ) {
        if (is_surface)
            FFX_DNSR_Reflections_FillEnvironment(dispatch_thread_id, g_frame_info.perFrame.u_iblFactor);
        else
            FFX_DNSR_Reflections_ZeroBuffers(dispatch_thread_id);
    }
}

// Entry point for classification
[numthreads(8, 8, 1)]
void main(uint2 group_id : SV_GroupID,
          uint group_index : SV_GroupIndex) {
    uint2  group_thread_id    = FFX_DNSR_Reflections_RemapLane8x8(group_index); // Remap lanes to ensure four neighboring lanes are arranged in a quad pattern
    uint2  dispatch_thread_id = group_id * 8 + group_thread_id;

#ifdef HSR_DEBUG
    // Clear
    if (!(g_hsr_mask & HSR_FLAGS_INTERSECTION_ACCUMULATE))
        g_rw_debug[dispatch_thread_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#endif

    if (all(dispatch_thread_id.xy < 128)) {
        float2 xi = float2(
                    SampleRandomNumber(dispatch_thread_id.x, dispatch_thread_id.y, g_frame_index , 0u, g_frame_info.random_samples_per_pixel),
                    SampleRandomNumber(dispatch_thread_id.x, dispatch_thread_id.y, g_frame_index , 1u, g_frame_info.random_samples_per_pixel));
        g_rw_random_number_image[dispatch_thread_id] = xi.xyyy;
    }
    uint2  screen_size                    = uint2(g_frame_info.reflection_width, g_frame_info.reflection_height);
    float  roughness                      = FFX_DNSR_Reflections_LoadRoughness(dispatch_thread_id);
    float3 world_space_normal             = FFX_DNSR_Reflections_LoadWorldSpaceNormal(dispatch_thread_id.xy);
    float  depth                          = FFX_DNSR_Reflections_LoadDepth(dispatch_thread_id.xy);
    float3 view_space_surface_normal      = mul(float4(normalize(world_space_normal), 0), g_view).xyz;
    // Classify tile
    FFX_DNSR_Reflections_ClassifyTiles(dispatch_thread_id,
                                            group_thread_id,
                                            roughness, view_space_surface_normal, depth,
                                            screen_size,
                                            g_samples_per_quad,
                                            g_temporal_variance_guided_tracing_enabled,
                                            g_hsr_mask & HSR_FLAGS_USE_HIT_COUNTER,
                                            g_hsr_mask & HSR_FLAGS_USE_SCREEN_SPACE,
                                            g_hsr_mask & HSR_FLAGS_USE_RAY_TRACING);
    // Extract only the channel containing the roughness to avoid loading all 4 channels in the follow up passes.
    FFX_DNSR_Reflections_StoreExtractedRoughness(dispatch_thread_id, roughness);
}