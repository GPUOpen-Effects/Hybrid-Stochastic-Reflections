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

/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h //
/////////////////////////////////////////////////////

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

void   FFX_Fetch_Face_Indices_U32(out uint3 face3, in uint offset, in uint triangle_id) { face3 = g_rw_geometry.Load<uint3>(offset * 4 + 12 * triangle_id); }
void   FFX_Fetch_Face_Indices_U16(out uint3 face3, in uint offset, in uint triangle_id) {
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
void   FFX_Fetch_Local_Basis(in Surface_Info sinfo, in uint3 face3, in float2 bary, out float2 uv, out float3 normal, out float4 tangent) {
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

float3 rotate(float4x4 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }
float3 rotate(float3x4 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }
float3 rotate(float3x3 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }

struct LocalBasis {
    float2 uv;
    float2 uv_ddx;
    float2 uv_ddy;
    float3 normal;
    float4 tangent;
};

struct GBuffer {
    float3 normal;
    float4 albedo;
    float  roughness;
    float3 specular;
    float  metalness;
    float  ambient_occlusion;
    float3 emission;
};

void GetGBufferFromMaterial(/* out */ out GBuffer gbuffer, /* in */ in LocalBasis local_basis, /* in */ in Material_Info material) {
    float3 normal  = normalize(local_basis.normal);
    float3 tangent = local_basis.tangent.xyz;
    if (material.normal_tex_id >= 0 && any(abs(tangent.xyz) > 0.0f)) {
        tangent           = normalize(tangent);
        float3 binormal   = normalize(cross(normal, tangent) * local_basis.tangent.w);
        float3 normal_rgb = float3(0.0f, 0.0f, 1.0f);
        normal_rgb        = g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.normal_tex_id)]
                         .SampleLevel(g_wrap_linear_sampler, local_basis.uv, 2.0)
                         .rgb;
        normal_rgb.z = sqrt(saturate(1.0 - normal_rgb.r * normal_rgb.r - normal_rgb.g * normal_rgb.g));         
        normal = normalize(normal_rgb.z * normal + (2.0f * normal_rgb.x - 1.0f) * tangent + (2.0f * normal_rgb.y - 1.0f) * binormal);
    }
    gbuffer.albedo = float4(material.albedo_factor_x, material.albedo_factor_y, material.albedo_factor_z, material.albedo_factor_w);
    if (material.albedo_tex_id >= 0) {
        gbuffer.albedo = gbuffer.albedo * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.albedo_tex_id)]
        .SampleLevel(g_wrap_linear_sampler, local_basis.uv, 2.0)
        ;
    }
    float3 arm = float3(1.0f, material.arm_factor_y, material.arm_factor_z);
    if (material.arm_tex_id >= 0) {
        arm = arm * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.arm_tex_id)]
                        .SampleLevel(g_wrap_linear_sampler, local_basis.uv, 2.0)
                        .xyz;
    }
    float3 emission = (0.0f).xxx;
    if (material.emission_tex_id >= 0) {
        emission = // material.emission_factor *
            g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.emission_tex_id)]
                .SampleLevel(g_wrap_linear_sampler, local_basis.uv, 2.0)
                .xyz;
    }
    gbuffer.normal            = normal;
    gbuffer.roughness         = saturate(arm.g);
    gbuffer.metalness         = arm.b;
    gbuffer.ambient_occlusion = arm.x;
    gbuffer.emission          = emission;
    gbuffer.specular          = lerp((0.04).xxx, gbuffer.albedo.xyz, gbuffer.metalness);
}

[numthreads(8, 8, 1)] void main(uint3 tid
                                : SV_DispatchThreadID) {
    uint width, height;
    g_rw_debug.GetDimensions(width, height);
    if (tid.x >= width || tid.y >= height) return;

    float2 screen_uv = (float2(tid.xy) + float2(0.5f, 0.5f)) / float2(width, height);

    float  z                          = g_textures[GDT_TEXTURES_GBUFFER_DEPTH_SLOT].Load(int3(tid.xy, 0)).x;
    float3 screen_uv_space_ray_origin = float3(screen_uv, z);
    float3 view_space_hit             = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(screen_uv_space_ray_origin);
    float3 world_space_hit            = mul(float4(view_space_hit, 1), g_inv_view).xyz;
    float3 camera_position            = FFX_DNSR_Reflections_ViewSpaceToWorldSpace(float4(0.0f, 0.0f, 0.0f, 1.0f));

    float3 wnormal   = normalize(g_textures[GDT_TEXTURES_GBUFFER_NORMAL_SLOT].Load(int3(tid.xy, 0)).xyz * 2.0f - (1.0f).xxx);
    float3 ve        = FFX_DNSR_Reflections_ScreenSpaceToViewSpace(float3(screen_uv, z));
    float3 vo        = float3(0.0f, 0.0f, 0.0f);
    float3 wo        = mul(float4(vo, 1.0f), g_inv_view).xyz;
    float3 we        = mul(float4(ve, 1.0f), g_inv_view).xyz;
    float3 wdir      = normalize(we - wo);
    float3 reflected = -reflect(wdir, wnormal);

    bool   hit  = false;
    float2 bary = float2(0.0, 0.0);
    RayQuery<RAY_FLAG_CULL_NON_OPAQUE              //
             | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES //
             >
            q;
    RayDesc ray;
    ray.Origin    = wo;
    ray.Direction = wdir;
    ray.TMin      = 0.1f;
    ray.TMax      = 1000.0f;
    q.TraceRayInline(g_global, 0, 0xff, ray);

    q.Proceed();
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        bary = q.CommittedTriangleBarycentrics();
        hit  = true;

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

        obj_to_world = float3x3(q.CommittedObjectToWorld3x4()[0].xyz, q.CommittedObjectToWorld3x4()[1].xyz, q.CommittedObjectToWorld3x4()[2].xyz);
        bary         = q.CommittedTriangleBarycentrics();
        instance_id  = q.CommittedInstanceID();
        geometry_id  = q.CommittedGeometryIndex();
        triangle_id  = q.CommittedPrimitiveIndex();

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

        normal      = normalize(rotate(obj_to_world, normal));
        tangent.xyz = normalize(rotate(obj_to_world, tangent.xyz));

        GBuffer    gbuffer;
        LocalBasis lbasis;
        lbasis.uv      = uv;
        lbasis.uv_ddx  = 1.0e-3f;
        lbasis.uv_ddy  = 1.0e-3f;
        lbasis.normal  = normal;
        lbasis.tangent = tangent;

        GetGBufferFromMaterial(/* out */ gbuffer, /* in */ lbasis, /* in */ material);

        float3 N = gbuffer.normal;
        float3 V = normalize(camera_position - world_space_hit);
        float3 L = reflect(-V, N);
        float3 H = normalize(L + V);

        ShadingInfo local_basis;
        local_basis.WorldPos  = ray.Origin + ray.Direction * q.CommittedRayT();
        local_basis.Normal    = gbuffer.normal;
        local_basis.View      = V;
        local_basis.Albedo    = gbuffer.albedo.xyz;
        local_basis.Roughness = gbuffer.roughness;
        local_basis.Metalness = gbuffer.metalness;

        float3 reflection_radiance = doPbrLighting(local_basis, myPerFrame, (1.0 - local_basis.Roughness) * myPerFrame.u_iblFactor);

        float4 emission = float4(material.emission_factor_x, material.emission_factor_y, material.emission_factor_z, 1.0);
        if (material.emission_tex_id >= 0) {
            emission = emission * g_textures[NonUniformResourceIndex(GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + material.emission_tex_id)]
                .SampleLevel(g_wrap_linear_sampler, uv, 0).xyzw;
        }
        reflection_radiance += emission.xyz * myPerFrame.u_EmissiveFactor;

        g_rw_debug[tid.xy] = float4(reflection_radiance, 1.0f);
        return;
    }

    g_rw_debug[tid.xy] = float4(0.0, 0.0, 0.0, 1.0f);
}