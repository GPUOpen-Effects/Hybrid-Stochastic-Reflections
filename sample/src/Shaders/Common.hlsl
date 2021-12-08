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


#define GOLDEN_RATIO 1.61803398875f
#define FFX_REFLECTIONS_SKY_DISTANCE 100.0f

// Helper defines for hitcouter and classification
#define FFX_HITCOUNTER_SW_HIT_FLAG (1u << 0u)
#define FFX_HITCOUNTER_SW_HIT_SHIFT 0u
#define FFX_HITCOUNTER_SW_OLD_HIT_SHIFT 8u
#define FFX_HITCOUNTER_MASK 0xffu
#define FFX_HITCOUNTER_SW_MISS_FLAG (1u << 16u)
#define FFX_HITCOUNTER_SW_MISS_SHIFT 16u
#define FFX_HITCOUNTER_SW_OLD_MISS_SHIFT 24u

#define FFX_Hitcounter_GetSWHits(counter) ((counter >> FFX_HITCOUNTER_SW_HIT_SHIFT) & FFX_HITCOUNTER_MASK)
#define FFX_Hitcounter_GetSWMisses(counter) ((counter >> FFX_HITCOUNTER_SW_MISS_SHIFT) & FFX_HITCOUNTER_MASK)
#define FFX_Hitcounter_GetOldSWHits(counter) ((counter >> FFX_HITCOUNTER_SW_OLD_HIT_SHIFT) & FFX_HITCOUNTER_MASK)
#define FFX_Hitcounter_GetOldSWMisses(counter) ((counter >> FFX_HITCOUNTER_SW_OLD_MISS_SHIFT) & FFX_HITCOUNTER_MASK)

//=== Common functions of the HSRSample ===

uint PackFloat16(min16float2 v) {
    uint2 p = f32tof16(float2(v));
    return p.x | (p.y << 16);
}

min16float2 UnpackFloat16(uint a) {
    float2 tmp = f16tof32(uint2(a & 0xFFFF, a >> 16));
    return min16float2(tmp);
}

uint PackRayCoords(uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal) {
    uint ray_x_15bit          = ray_coord.x & 0b111111111111111;
    uint ray_y_14bit          = ray_coord.y & 0b11111111111111;
    uint copy_horizontal_1bit = copy_horizontal ? 1 : 0;
    uint copy_vertical_1bit   = copy_vertical ? 1 : 0;
    uint copy_diagonal_1bit   = copy_diagonal ? 1 : 0;

    uint packed = (copy_diagonal_1bit << 31) | (copy_vertical_1bit << 30) | (copy_horizontal_1bit << 29) | (ray_y_14bit << 15) | (ray_x_15bit << 0);
    return packed;
}

void UnpackRayCoords(uint packed, out uint2 ray_coord, out bool copy_horizontal, out bool copy_vertical, out bool copy_diagonal) {
    ray_coord.x     = (packed >> 0) & 0b111111111111111;
    ray_coord.y     = (packed >> 15) & 0b11111111111111;
    copy_horizontal = (packed >> 29) & 0b1;
    copy_vertical   = (packed >> 30) & 0b1;
    copy_diagonal   = (packed >> 31) & 0b1;
}

// Transforms origin to uv space
// Mat must be able to transform origin from its current space into clip space.
float3 ProjectPosition(float3 origin, float4x4 mat) {
    float4 projected = mul(float4(origin, 1), mat);
    projected.xyz /= projected.w;
    projected.xy = 0.5 * projected.xy + 0.5;
    projected.y  = (1 - projected.y);
    return projected.xyz;
}

// Origin and direction must be in the same space and mat must be able to transform from that space into clip space.
float3 ProjectDirection(float3 origin, float3 direction, float3 screen_space_origin, float4x4 mat) {
    float3 offsetted = ProjectPosition(origin + direction, mat);
    return offsetted - screen_space_origin;
}

// Mat must be able to transform origin from texture space to a linear space.
float3 InvProjectPosition(float3 coord, float4x4 mat) {
    coord.y          = (1 - coord.y);
    coord.xy         = 2 * coord.xy - 1;
    float4 projected = mul(float4(coord, 1), mat);
    projected.xyz /= projected.w;
    return projected.xyz;
}

//=== FFX_DNSR_Reflections_ override functions ===


bool FFX_DNSR_Reflections_IsGlossyReflection(float roughness) { return roughness < g_roughness_threshold; }

bool FFX_DNSR_Reflections_IsMirrorReflection(float roughness) { return roughness < 0.0001; }

float3 FFX_DNSR_Reflections_ScreenSpaceToViewSpace(float3 screen_uv_coord) { return InvProjectPosition(screen_uv_coord, g_inv_proj); }

float3 FFX_DNSR_Reflections_ViewSpaceToWorldSpace(float4 view_space_coord) { return mul(view_space_coord, g_inv_view).xyz; }

float3 FFX_DNSR_Reflections_WorldSpaceToScreenSpacePrevious(float3 world_coord) { return ProjectPosition(world_coord, g_prev_view_proj); }


float2 OctahedronUV(float3 N) {
    N.xy /= dot((1.0f).xxx, abs(N));
    if (N.z <= 0.0f) N.xy = (1.0f - abs(N.yx)) * (N.xy >= 0.0f ? (1.0f).xx : (-1.0f).xx);
    return N.xy * 0.5f + 0.5f;
}

float3 UVtoOctahedron(float2 UV) {
    UV       = 2.0f * (UV - 0.5f);
    float3 N = float3(UV, 1.0f - dot((1.0f).xx, abs(UV)));
    float  t = max(-N.z, 0.0f);
    N.xy += N.xy >= 0.0f ? -t : t;
    return normalize(N);
}

// Blue Noise Sampler by Eric Heitz. Returns a value in the range [0, 1].
float SampleRandomNumber(uint pixel_i, uint pixel_j, uint sample_index, uint sample_dimension, uint samples_per_pixel) {
    // Wrap arguments
    pixel_i          = pixel_i & 127u;
    pixel_j          = pixel_j & 127u;
    sample_index     = (sample_index % samples_per_pixel) & 255u;
    sample_dimension = sample_dimension & 255u;

#ifndef SPP
#    define SPP 256
#endif

#if SPP == 1
    const uint ranked_sample_index = sample_index ^ 0;
#else
    // xor index based on optimized ranking
    const uint ranked_sample_index = sample_index ^ g_rw_buffers[GDT_BUFFERS_RANKING_TILE_SLOT].Load(4 * (sample_dimension + (pixel_i + pixel_j * 128u) * 8u));
#endif

    // Fetch value in sequence
    uint value = g_rw_buffers[GDT_BUFFERS_SOBOL_SLOT].Load(4 * (sample_dimension + ranked_sample_index * 256u));

    // If the dimension is optimized, xor sequence value based on optimized scrambling
    value = value ^ g_rw_buffers[GDT_BUFFERS_SCRAMBLING_TILE_SLOT].Load(4 * ((sample_dimension % 8u) + (pixel_i + pixel_j * 128u) * 8u));

    // Convert to float and return
    return (value + 0.5f) / 256.0f;
}

#define GOLDEN_RATIO 1.61803398875f

float2 SampleRandomVector2DBaked(uint2 pixel) {
    int2   coord = int2(pixel.x & 127u, pixel.y & 127u);
    float2 xi    = g_random_number_image[coord].xy;
    float2 u     = float2(fmod(xi.x + (((int)(pixel.x / 128)) & 0xFFu) * GOLDEN_RATIO, 1.0f), fmod(xi.y + (((int)(pixel.y / 128)) & 0xFFu) * GOLDEN_RATIO, 1.0f));
    return u;
}

float FFX_DNSR_Reflections_GetLinearDepth(float2 uv, float depth) {
    const float3 view_space_pos = InvProjectPosition(float3(uv, depth), g_inv_proj);
    return abs(view_space_pos.z);
}

bool FFX_DNSR_Reflections_IsBackground(float depth) { return depth >= (1.0f - 1.e-6f); }

float3 FFX_GetEnvironmentSample(float3 world_space_position, float3 world_space_reflected_direction, float roughness) {
    return g_ctextures[GDT_CTEXTURES_ATMOSPHERE_LUT_SLOT].SampleLevel(g_linear_sampler, world_space_reflected_direction, sqrt(roughness) * 9.0f).xyz;
}

// Copyright (c) 2018 Eric Heitz (the Authors).
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
float3 SampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) {
    // Input Ve: view direction
    // Input alpha_x, alpha_y: roughness parameters
    // Input U1, U2: uniform random numbers
    // Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
    //
    //
    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float  lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1    = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
    float3 T2    = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float       r    = sqrt(U1);
    const float M_PI = 3.14159265358979f;
    float       phi  = 2.0 * M_PI * U2;
    float       t1   = r * cos(phi);
    float       t2   = r * sin(phi);
    float       s    = 0.5 * (1.0 + Vh.z);
    t2               = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;
}

float3 Sample_GGX_VNDF_Ellipsoid(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) { return SampleGGXVNDF(Ve, alpha_x, alpha_y, U1, U2); }

float3 Sample_GGX_VNDF_Hemisphere(float3 Ve, float alpha, float U1, float U2) { return Sample_GGX_VNDF_Ellipsoid(Ve, alpha, alpha, U1, U2); }

float3x3 CreateTBN(float3 N) {
    float3 U;
    if (abs(N.z) > 0.0) {
        float k = sqrt(N.y * N.y + N.z * N.z);
        U.x     = 0.0;
        U.y     = -N.z / k;
        U.z     = N.y / k;
    } else {
        float k = sqrt(N.x * N.x + N.y * N.y);
        U.x     = N.y / k;
        U.y     = -N.x / k;
        U.z     = 0.0;
    }

    float3x3 TBN;
    TBN[0] = U;
    TBN[1] = cross(N, U);
    TBN[2] = N;
    return transpose(TBN);
}

float2 SampleRandomVector2D(uint2 pixel, int frame_offset = 0, int frame = g_frame_index, int samples_per_pixel = 32) {
    float2 u = float2(fmod(SampleRandomNumber(pixel.x, pixel.y, frame + frame_offset, 0u, samples_per_pixel) + (((int)(pixel.x / 128)) & 0xFFu) * GOLDEN_RATIO, 1.0f),
        fmod(SampleRandomNumber(pixel.x, pixel.y, frame + frame_offset, 1u, samples_per_pixel) + (((int)(pixel.y / 128)) & 0xFFu) * GOLDEN_RATIO, 1.0f));
    return u;
}

float3 SampleReflectionVector(float3 view_direction, float3 normal, float roughness, int2 dispatch_thread_id) {
    if (roughness < 0.001f) {
        return reflect(view_direction, normal);
    }
    float3x3 tbn_transform      = CreateTBN(normal);
    float3   view_direction_tbn = mul(-view_direction, tbn_transform);
    float2   u                  = SampleRandomVector2DBaked(dispatch_thread_id);
    float3   sampled_normal_tbn = Sample_GGX_VNDF_Hemisphere(view_direction_tbn, roughness, u.x, u.y);
#ifdef PERFECT_REFLECTIONS
    sampled_normal_tbn = float3(0, 0, 1); // Overwrite normal sample to produce perfect reflection.
#endif
    float3 reflected_direction_tbn = reflect(-view_direction_tbn, sampled_normal_tbn);
    // Transform reflected_direction back to the initial space.
    float3x3 inv_tbn_transform = transpose(tbn_transform);
    return mul(reflected_direction_tbn, inv_tbn_transform);
}
