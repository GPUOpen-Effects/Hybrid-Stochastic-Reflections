// AMD Cauldron code
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "Declarations.h"

HLSL_INIT_GLOBAL_BINDING_TABLE(1)

#include "Common.hlsl"

/////////////////////////////////////////////////////
// Used resources:                                 //
// See aliases in Descriptors.h and Declarations.h // 
/////////////////////////////////////////////////////

#if 0
RWByteAddressBuffer g_rw_surface_info; // Array of Surface_Info 
RWByteAddressBuffer g_rw_geometry; // Vertex/Index buffers for all the geometry in the TLAS 
#endif

struct Matrix2 {
    matrix m_current;
    matrix m_previous;
};

cbuffer cbPerSkeleton : register(b0) { Matrix2 myPerSkeleton_u_ModelMatrix[200]; };

struct PushConstants {
    int src_surface_id;
    int dst_surface_id;
    int num_vertices;
    int padding0;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc : DX12_PUSH_CONSTANTS;

float3 rotate(float4x4 mat, float3 v) { return mul(float3x3(mat[0].xyz, mat[1].xyz, mat[2].xyz), v); }

matrix GetCurrentSkinningMatrix(float4 Weights, uint4 Joints) {
    matrix skinningMatrix = Weights.x * myPerSkeleton_u_ModelMatrix[Joints.x].m_current + Weights.y * myPerSkeleton_u_ModelMatrix[Joints.y].m_current +
                            Weights.z * myPerSkeleton_u_ModelMatrix[Joints.z].m_current + Weights.w * myPerSkeleton_u_ModelMatrix[Joints.w].m_current;
    return skinningMatrix;
}

// Apply skinning for skeletal animated geometry and write the results to the separate range that is used to refit the BLAS
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    if (i >= pc.num_vertices) return;

    Surface_Info src_surface_info = g_rw_surface_info.Load<Surface_Info>(sizeof(Surface_Info) * pc.src_surface_id);
    Surface_Info dst_surface_info = g_rw_surface_info.Load<Surface_Info>(sizeof(Surface_Info) * pc.dst_surface_id);

    matrix transform = float4x4(1.0f, 0.0f, 0.0f, 0.0f, //
                                0.0f, 1.0f, 0.0f, 0.0f, //
                                0.0f, 0.0f, 1.0f, 0.0f, //
                                0.0f, 0.0f, 0.0f, 1.0f);

    if (src_surface_info.weight_attribute_offset >= 0 && src_surface_info.joints_attribute_offset >= 0) {
        float4 weights       = g_rw_geometry.Load<float4>(src_surface_info.weight_attribute_offset * 4 + sizeof(float4) * i);
        uint   packed_joints = g_rw_geometry.Load<uint>(src_surface_info.joints_attribute_offset * 4 + sizeof(uint) * i);
        uint4  joints        = uint4((packed_joints >> 0) & 0xffu, (packed_joints >> 8) & 0xffu, (packed_joints >> 16) & 0xffu, (packed_joints >> 24) & 0xffu);
        transform            = GetCurrentSkinningMatrix(weights, joints);
    }

    if (src_surface_info.position_attribute_offset >= 0 && dst_surface_info.position_attribute_offset >= 0) {
        float3 src_position = g_rw_geometry.Load<float3>(src_surface_info.position_attribute_offset * 4 + sizeof(float3) * i);
        float3 dst_position = mul(transform, float4(src_position, 1.0f)).xyz;
        g_rw_geometry.Store<float3>(dst_surface_info.position_attribute_offset * 4 + sizeof(float3) * i, dst_position);
    }

    if (src_surface_info.normal_attribute_offset >= 0 && dst_surface_info.normal_attribute_offset >= 0) {
        float3 src = g_rw_geometry.Load<float3>(src_surface_info.normal_attribute_offset * 4 + sizeof(float3) * i);
        float3 dst = rotate(transform, src);
        g_rw_geometry.Store<float3>(dst_surface_info.normal_attribute_offset * 4 + sizeof(float3) * i, dst);
    }

    if (src_surface_info.tangent_attribute_offset >= 0 && dst_surface_info.tangent_attribute_offset >= 0) {
        float4 src = g_rw_geometry.Load<float4>(src_surface_info.tangent_attribute_offset * 4 + sizeof(float4) * i);
        float4 dst = float4(rotate(transform, src.xyz), src.w);
        g_rw_geometry.Store<float4>(dst_surface_info.tangent_attribute_offset * 4 + sizeof(float4) * i, dst);
    }
}
