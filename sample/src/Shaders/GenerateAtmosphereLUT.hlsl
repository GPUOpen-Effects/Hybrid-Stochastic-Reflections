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

#include "Atmosphere.h"

/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h // 
/////////////////////////////////////////////////////

#if 0
TextureCube<float4> g_atmosphere_mip; // Environment Mip chain 
SamplerState g_linear_sampler; 
#endif

struct PushConstants {
    float3 up;
    uint   mip_level;
    float3 right;
    uint   pad0;
    float3 view;
    uint   dst_slice;
    float3 world_offset;
    uint   pad1;
    float3 sun_direction;
    uint   pad2;
    float3 sun_intensity;
    uint   pad3;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc : DX12_PUSH_CONSTANTS;

#define PI 3.141592653589793

float4 SampleCubemap(float3 r, float mip_level) { return g_atmosphere_mip.SampleLevel(g_linear_sampler, r, mip_level).xyzw; }

// Src: Hacker's Delight, Henry S. Warren, 2001
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N) { return float2(float(i) / float(N), RadicalInverse_VdC(i)); }

// Based on Karis 2014
float3 Sample_GGX_IPDF(float2 xi, float roughness, float3 N, float3 V, out float inv_pdf) {
    float  a           = roughness * roughness;
    float  a2          = a * a;
    float  phi         = 2.0f * PI * xi.x;
    float  epsilon     = clamp(xi.y, 0.001f, 1.0f);
    float  cos_theta_2 = (1.0 - epsilon) / ((a2 - 1.0f) * epsilon + 1.0f);
    float  cos_theta   = sqrt(cos_theta_2);
    float  sin_theta   = sqrt(1.0 - cos_theta_2);
    float3 t           = normalize(cross(N.yzx, N));
    float3 b           = cross(N, t);
    float3 H           = t * sin_theta * cos(phi) + b * sin_theta * sin(phi) + N * cos_theta;
    float  den         = (a2 - 1.0f) * cos_theta_2 + 1.0f;
    float  D           = a2 / (PI * den * den);
    float  pdf         = D * cos_theta / (4.0f * dot(H, V));
    inv_pdf            = 1.0f / (PI * (pdf + 1.0e-6f));
    return H;
}

float4 Integrate_Specular(float roughness, float3 R, int img_size) {
    float3     N            = R;
    float3     V            = R;
    float4     radiance_acc = float4(0.0f, 0.0f, 0.0f, 0.0f);
    const uint num_samples  = 64u;
    float      weight_acc   = 0.0f;
    for (uint i = 0u; i < num_samples; i++) {
        float2 xi = Hammersley(i, num_samples);
        float  inv_pdf;
        float3 H   = Sample_GGX_IPDF(xi, roughness, N, V, /* out */ inv_pdf);
        float3 L   = 2.0f * dot(V, H) * H - V;
        float  NoL = saturate(dot(N, L));
        float  NoH = saturate(dot(N, H));
        if (NoL > 0.0f) {
            float solid_angle_of_cone    = inv_pdf / float(num_samples);
            float solid_angle_of_a_pixel = 4.0f * PI / (6.0f * img_size * img_size);
            // We select mip level based on how much pixels the sampling cone covers
            // number of pixels   = solid_angle_of_cone / solid_angle_of_a_pixel
            // width of rectangle = sqrt(number of pixels)
            // mip level          = log2(width of rectangle)
            float mip_level              = max(0.5f * log2(solid_angle_of_cone / solid_angle_of_a_pixel), 0.0f);
            radiance_acc += SampleCubemap(L, mip_level) * NoL;
            weight_acc += NoL;
        }
    }
    radiance_acc   = radiance_acc / (weight_acc + 1.0e-6f);
    radiance_acc.a = 1.0f;
    return radiance_acc;
}

float4 Integrate_Diffuse(float3 r) {
    uint   N              = 1 << 12;
    float4 irradiance_acc = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < N; i++) {
        float2 xi       = Hammersley(i, N);
        float phi       = xi.y * 2.0f * PI;
        float cos_theta = sqrt(1.0f - xi.x);
        float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
        float3 rxi      = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
        float3 tangent  = normalize(cross(r.yxz, r));
        float3 binormal = cross(r, tangent);
        float3 rand_dir = normalize(r * rxi.z + tangent * rxi.x + binormal * rxi.y);
        irradiance_acc += SampleCubemap(rand_dir, 0.0f);
    }
    irradiance_acc   = irradiance_acc / float(N);
    irradiance_acc.a = 1.0f;
    return irradiance_acc;
}

[numthreads(8, 8, 1)] void mainSpecular(uint3 tid
                                        : SV_DispatchThreadID) {
    uint3 size;
    g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_LUT_BEGIN_SLOT + pc.dst_slice].GetDimensions(size.x, size.y, size.z);
    if (any(tid.xy >= size.xy)) return;
    float2 uv  = (float2(tid.xy) + float2(0.5f, 0.5f)) / float2(size.xy);
    float2 xy  = uv * 2.0f - float2(1.0f, 1.0f);
    xy.y       = -xy.y;
    float3 dir = normalize(pc.up * xy.y + pc.right * xy.x + pc.view);
    if (pc.mip_level == 0) { // First iteration initializes the target
        // Reset downsampling counter
        if (tid.y == 0 && tid.x < 6) g_rw_buffers[GDT_BUFFERS_DOWNSAMPLE_COUNTER_SLOT].Store(4 * tid.x, 0);
        float3 transmittance;
        float3 environment_lookup = IntegrateScattering(pc.world_offset, dir, 1.0f / 0.0f, pc.sun_direction, pc.sun_intensity, transmittance);
        g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_LUT_BEGIN_SLOT + pc.dst_slice][int3(tid.xy, 0)] = environment_lookup.xyzz;
        g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_MIP_BEGIN_SLOT + pc.dst_slice][int3(tid.xy, 0)] = environment_lookup.xyzz;
    } else { // Compute mip chain
        g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_LUT_BEGIN_SLOT + pc.dst_slice][int3(tid.xy, 0)] = Integrate_Specular(pc.mip_level / 12.0f, dir, size.x);
    }
}

    [numthreads(8, 8, 1)] void mainDiffuse(uint3 tid
                                           : SV_DispatchThreadID) {
    uint3 size;
    g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_DIF_BEGIN_SLOT + pc.dst_slice].GetDimensions(size.x, size.y, size.z);
    if (any(tid.xy >= size.xy)) return;
    float2 uv  = (float2(tid.xy) + float2(0.5f, 0.5f)) / float2(size.xy);
    float2 xy  = uv * 2.0f - float2(1.0f, 1.0f);
    xy.y       = -xy.y;
    float3 dir = normalize(pc.up * xy.y + pc.right * xy.x + pc.view);
    float3 transmittance;
    float3 environment_lookup                                                                  = Integrate_Diffuse(dir);
    g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_DIF_BEGIN_SLOT + pc.dst_slice][int3(tid.xy, 0)] = environment_lookup.xyzz;
}