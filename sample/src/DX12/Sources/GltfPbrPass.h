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
#pragma once

#include "../Common/GLTF/GltfPbrMaterial.h"
#include "GLTF/GLTFTexturesAndBuffers.h"
#include "PostProc/SkyDome.h"
#include "base/GBuffer.h"

namespace hlsl {

#include "../../Shaders/Declarations.h"

}

#include <unordered_set>

namespace RTCAULDRON_DX12 {
struct PBRMaterial {
    int         m_materialID   = -1;
    int         m_textureCount = 0;
    CBV_SRV_UAV m_texturesTable;

    D3D12_STATIC_SAMPLER_DESC m_samplers[10];

    PBRMaterialParameters m_pbrMaterialParameters;
};

struct PBRPrimitives {
    Geometry m_geometry;

    PBRMaterial *m_pMaterial = NULL;

    ID3D12RootSignature *m_RootSignature;
    ID3D12PipelineState *m_PipelineRender;
    ID3D12PipelineState *m_PipelineWireframeRender;

    void DrawPrimitive(ID3D12GraphicsCommandList *pCommandList, CBV_SRV_UAV *pShadowBufferSRV, D3D12_GPU_VIRTUAL_ADDRESS perSceneDesc, D3D12_GPU_VIRTUAL_ADDRESS perObjectDesc,
                       D3D12_GPU_VIRTUAL_ADDRESS pPerSkeleton, bool bWireframe);
};

struct PBRMesh {
    std::vector<PBRPrimitives> m_pPrimitives;
};

class RTGltfPbrPass {
public:
    struct per_object {
        math::Matrix4 mCurrentWorld;
        math::Matrix4 mPreviousWorld;

        PBRMaterialParametersConstantBuffer m_pbrParams;
    };

    struct BatchList {
        float                     m_depth;
        PBRPrimitives *           m_pPrimitive;
        D3D12_GPU_VIRTUAL_ADDRESS m_perFrameDesc;
        D3D12_GPU_VIRTUAL_ADDRESS m_perObjectDesc;
        D3D12_GPU_VIRTUAL_ADDRESS m_pPerSkeleton;
                                  operator float() { return -m_depth; }
    };

    void OnCreate(Device *pDevice, UploadHeap *pUploadHeap, ResourceViewHeaps *pHeaps, DynamicBufferRing *pDynamicBufferRing, GLTFTexturesAndBuffers *pGLTFTexturesAndBuffers,
                  StaticBufferPool *pStaticBufferPool, Texture *pSpecularLUT, Texture *pDiffuseLUT, bool bUseSSAOMask, bool bUseShadowMask, GBufferRenderPass *pGBufferRenderPass,
                  AsyncPool *pAsyncPool = NULL);

    void OnDestroy();
    void OnUpdateWindowSizeDependentResources(Texture *pSSAO);
    void BuildBatchLists(std::vector<BatchList> *pSolid, std::vector<BatchList> *pTransparent, bool bWireframe = false);
    void DrawBatchList(ID3D12GraphicsCommandList *pCommandList, CBV_SRV_UAV *pShadowBufferSRV, std::vector<BatchList> *pBatchList, bool bWireframe = false);

private:
    Device *           m_pDevice            = NULL;
    GBufferRenderPass *m_pGBufferRenderPass = NULL;

    GLTFTexturesAndBuffers *m_pGLTFTexturesAndBuffers = NULL;

    ResourceViewHeaps *m_pResourceViewHeaps = NULL;
    DynamicBufferRing *m_pDynamicBufferRing = NULL;

    std::vector<PBRMesh>     m_meshes;
    std::vector<PBRMaterial> m_materialsData;

    RTGltfPbrPass::per_frame m_cbPerFrame;

    PBRMaterial m_defaultMaterial;

    Texture m_BrdfLut;

    bool m_doLighting;

    DXGI_FORMAT              m_depthFormat;
    std::vector<DXGI_FORMAT> m_outFormats;
    uint32_t                 m_sampleCount;
    void CreateDescriptorTableForMaterialTextures(PBRMaterial *tfmat, std::map<std::string, Texture *> &texturesBase, Texture *pSpecularLUT, Texture *pDiffuseLUT,
                                                  bool bUseShadowMask, bool bUseSSAOMask);
    void CreateRootSignature(bool bUsingSkinning, DefineList &defines, PBRPrimitives *pPrimitive, bool bUseSSAOMask);
    void CreatePipeline(std::vector<D3D12_INPUT_ELEMENT_DESC> layout, const DefineList &defines, PBRPrimitives *pPrimitive);

    ///////////////////
    // Ray Tracing   //
    ///////////////////
public:
    ID3D12Device5 *   m_pDevice5          = NULL;
    StaticBufferPool *m_pStaticBufferPool = NULL;
    struct Skinned_Surface_Info {
        int32_t instance_id    = -1;
        int32_t src_surface_id = -1;
        int32_t dst_surface_id = -1;
        int32_t padding0;
    };

    struct BakeSkinning {
        ID3D12RootSignature *m_pRootSignature     = NULL;
        ID3D12PipelineState *m_pPipeline          = NULL;
        Device *             m_pDevice            = NULL;
        ResourceViewHeaps *  m_pResourceViewHeaps = NULL;

        // Copy from SkinForBLAS.hlsl
        struct PushConstants {
            int src_surface_id;
            int dst_surface_id;
            int num_vertices;
            int padding0;
        };

        void OnCreate(Device *pDevice, ResourceViewHeaps *pResourceViewHeaps) {
            m_pDevice = pDevice;

            m_pResourceViewHeaps = pResourceViewHeaps;

            // Compile shaders
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            CompileShaderFromFile("SkinForBLAS.hlsl", &defines, "main", "-T cs_6_5", &shaderByteCode);

            // Create root signature
            //
            {

                CD3DX12_ROOT_PARAMETER RTSlot[3];

                int parameterCount = 0;
                // Global descriptor table for the scene

                CD3DX12_DESCRIPTOR_RANGE globalDescritorRanges[GDT_CBV_SRV_UAV_NUM_RANGES];

                int uav_params     = 0;
                int sampler_params = 0;

#define ADD_TLAS_RANGE(num_of_registers, register_offset, space, heap_offset)                                                                                                      \
    globalDescritorRanges[uav_params++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, num_of_registers, register_offset, space, heap_offset);
#define ADD_TEXTURE_RANGE(num_of_registers, register_offset, space, heap_offset)                                                                                                   \
    globalDescritorRanges[uav_params++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, num_of_registers, register_offset, space, heap_offset);
#define ADD_UAV_TEXTURE_RANGE(num_of_registers, register_offset, space, heap_offset)                                                                                               \
    globalDescritorRanges[uav_params++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, num_of_registers, register_offset, space, heap_offset);
#define ADD_BUFFER_RANGE(num_of_registers, register_offset, space, heap_offset)                                                                                                    \
    globalDescritorRanges[uav_params++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, num_of_registers, register_offset, space, heap_offset);
#define ADD_UNIFORM_BUFFER_RANGE(num_of_registers, register_offset, space, heap_offset)                                                                                            \
    globalDescritorRanges[uav_params++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, num_of_registers, register_offset, space, heap_offset);
#define ADD_SAMPLER_RANGE(num_of_registers, register_offset, space, heap_offset)                                                                                                   \
    {}

                INIT_GLOBAL_RANGES(/*in SPACE_ID*/ 1);

#undef ADD_TLAS_RANGE
#undef ADD_TEXTURE_RANGE
#undef ADD_UAV_TEXTURE_RANGE
#undef ADD_BUFFER_RANGE
#undef ADD_UNIFORM_BUFFER_RANGE
#undef ADD_SAMPLER_RANGE

                RTSlot[parameterCount++].InitAsDescriptorTable(GDT_CBV_SRV_UAV_NUM_RANGES, globalDescritorRanges);

                // cb matrices
                RTSlot[parameterCount++].InitAsConstantBufferView(0);

                // info push constants
                RTSlot[parameterCount++].InitAsConstants(sizeof(PushConstants) / 4, DX12_PUSH_CONSTANTS_REGISTER);

                // the root signature contains 3 slots to be used
                CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
                descRootSignature.NumParameters               = parameterCount;
                descRootSignature.pParameters                 = RTSlot;
                descRootSignature.NumStaticSamplers           = 0;
                descRootSignature.pStaticSamplers             = NULL;

                // deny uneccessary access to certain pipeline stages
                descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

                ID3DBlob *pOutBlob, *pErrorBlob = NULL;
                ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
                ThrowIfFailed(pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)));
                SetName(m_pRootSignature, "BakeSkinning::m_pRootSignature");

                pOutBlob->Release();
                if (pErrorBlob) pErrorBlob->Release();
            }

            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
                descPso.CS                                = shaderByteCode;
                descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
                descPso.pRootSignature                    = m_pRootSignature;
                descPso.NodeMask                          = 0;

                pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pPipeline));
                SetName(m_pPipeline, "BakeSkinning::m_pPipeline");
            }
        }

        void Release() {
#define SAFE_RELEASE(b)                                                                                                                                                            \
    if (b) {                                                                                                                                                                       \
        b->Release();                                                                                                                                                              \
        b = NULL;                                                                                                                                                                  \
    }
            SAFE_RELEASE(m_pRootSignature);
            SAFE_RELEASE(m_pPipeline);
#undef SAFE_RELEASE
        }

        void Draw(ID3D12GraphicsCommandList *pCommandList, CBV_SRV_UAV *pGlobalTable, D3D12_GPU_VIRTUAL_ADDRESS skinningMatrices, PushConstants pc) {
            if (m_pPipeline == NULL) return;

            // Bind Descriptor heaps and the root signature
            //
            ID3D12DescriptorHeap *pDescriptorHeaps[] = {m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap()};
            pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);
            pCommandList->SetComputeRootSignature(m_pRootSignature);

            // Bind Descriptor the descriptor sets
            //
            int params = 0;
            pCommandList->SetComputeRootDescriptorTable(params++, pGlobalTable->GetGPU());
            pCommandList->SetComputeRootConstantBufferView(params++, skinningMatrices);
            pCommandList->SetComputeRoot32BitConstants(params++, sizeof(pc) / 4, &pc, 0);

            // Bind Pipeline
            //
            pCommandList->SetPipelineState(m_pPipeline);

            // Dispatch
            //
            pCommandList->Dispatch((pc.num_vertices + 63) / 64, 1, 1);
        }
    };
    void BindMaterialResources(CBV_SRV_UAV *pGlobalTable) { m_infoTables.BindMaterialResources(pGlobalTable); }
    struct RTInfoTables {
        ////////////////////////////////////////
        //             SCENE RELATED          //
        ////////////////////////////////////////
        std::unordered_set<int> m_excludedNodes;
        RTGltfPbrPass *         m_pParent = NULL;

        // Source vertex attribute data
        ID3D12Resource *m_pSrcGeometryBufferResource = NULL;
        // Animated(Skeletal etc) vertex data
        // ID3D12Resource                        *m_pSkinnedPositionBufferResource = NULL;
        // std::vector<D3D12_GPU_VIRTUAL_ADDRESS> m_cpuGeometryBufferTable; // offsets from m_pGeometryBufferResource
        std::vector<ID3D12Resource *> m_cpuTextureTable;

        // std::vector<Vectormath::Matrix4> m_cpuInstanceTransform;
        std::vector<hlsl::Material_Info>  m_cpuMaterialBuffer;
        std::vector<hlsl::Instance_Info>  m_cpuInstanceBuffer;
        std::vector<Vectormath::Matrix4>  m_cpuInstanceTransformBuffer;
        std::vector<hlsl::Surface_Info>   m_cpuSurfaceBuffer;
        std::vector<Skinned_Surface_Info> m_cpuSkinnedSurfaces;
        std::vector<uint32_t>             m_cpuSurfaceIDsBuffer;
        std::vector<int32_t>              m_cpuSurfaceAnimatedOffsets;

        ID3D12Resource *m_pMaterialBuffer   = NULL; // material_id -> Material buffer
        ID3D12Resource *m_pSurfaceBuffer    = NULL; // surface_id -> Surface_Info buffer
        ID3D12Resource *m_pSurfaceIDsBuffer = NULL; // flat array of uint32_t
        ID3D12Resource *m_pInstanceBuffer   = NULL; // instance_id -> Instance_Info buffer

        bool m_scene_is_ready = false;

        ////////////////////////////////////////
        //   ACCELERATION STRUCTURE RELATED   //
        ////////////////////////////////////////
        BakeSkinning                  m_bakeSkinning;
        std::vector<ID3D12Resource *> m_scratchBuffers;
        std::vector<ID3D12Resource *> m_pTmpBLASBuffers;
        ID3D12Resource *              m_pScratchBuffer = NULL;
        ID3D12Resource *              m_pTmpBLAS       = NULL;

        ID3D12Resource *                            m_pOpaqueTlas       = NULL;
        ID3D12Resource *                            m_pTranpsparentTlas = NULL;
        ID3D12Resource *                            m_pGlobalTlas       = NULL;
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_cpuOpaqueTLASInstances;
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_cpuTransparentTLASInstances;
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_cpuGlobalTLASInstances;

        ID3D12Resource *m_pCpuvizOpaqueTLASInstances      = NULL;
        ID3D12Resource *m_pCpuvizTransparentTLASInstances = NULL;
        ID3D12Resource *m_pCpuvizGlobalTLASInstances      = NULL;

        ID3D12Resource *m_pOpaqueTLASInstances      = NULL;
        ID3D12Resource *m_pTransparentTLASInstances = NULL;
        ID3D12Resource *m_pGlobalTLASInstances      = NULL;
        // Array<SurfaceID> -> BLAS
        std::map<std::vector<uint32_t>, std::pair<ID3D12Resource *, uint64_t>> m_blas_table;
        std::unordered_map<uint64_t, ID3D12Resource *>                         m_hash_blas_table;
        std::unordered_set<uint64_t>                                           m_skinned_surface_sets;

        ////////////////////////////////////////
        //          BINDING RELATED           //
        ////////////////////////////////////////
        void BindMaterialResources(CBV_SRV_UAV *pGlobalTable) {
            auto bindUAVBuffer = [=](ID3D12Resource *pBuffer, uint32_t slot) {
                if (pBuffer) {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
                    desc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
                    desc.Buffer.CounterOffsetInBytes = 0;
                    desc.Buffer.FirstElement         = 0;
                    desc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_RAW;
                    desc.Buffer.NumElements          = (UINT)pBuffer->GetDesc().Width / 4;
                    desc.Buffer.StructureByteStride  = 0;
                    desc.Format                      = DXGI_FORMAT_R32_TYPELESS;
                    m_pParent->m_pDevice->GetDevice()->CreateUnorderedAccessView(pBuffer, NULL, &desc, pGlobalTable->GetCPU(GDT_BUFFERS_HEAP_OFFSET + slot));
                }
            };

            bindUAVBuffer(m_pInstanceBuffer, GDT_BUFFERS_INSTANCE_INFO_SLOT);
            bindUAVBuffer(m_pSurfaceIDsBuffer, GDT_BUFFERS_SURFACE_ID_SLOT);
            bindUAVBuffer(m_pSurfaceBuffer, GDT_BUFFERS_SURFACE_INFO_SLOT);
            bindUAVBuffer(m_pMaterialBuffer, GDT_BUFFERS_MATERIAL_INFO_SLOT);
            bindUAVBuffer(m_pSrcGeometryBufferResource, GDT_BUFFERS_GEOMETRY_SLOT);

            auto bindTexture = [=](ID3D12Resource *pTexture, uint32_t slot) {
                if (pTexture) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
                    auto                            res_desc = pTexture->GetDesc();
                    desc.Shader4ComponentMapping             = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                    desc.Format = res_desc.Format;

                    if (res_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
                        if (res_desc.DepthOrArraySize == 1) {
                            desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE1D;
                            desc.Texture1D.MipLevels       = res_desc.MipLevels;
                            desc.Texture1D.MostDetailedMip = 0;
                        } else {
                            desc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                            desc.Texture1DArray.MipLevels       = res_desc.MipLevels;
                            desc.Texture1DArray.ArraySize       = res_desc.DepthOrArraySize;
                            desc.Texture1DArray.FirstArraySlice = 0;
                            desc.Texture1DArray.MostDetailedMip = 0;
                        }
                    } else if (res_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                        if (res_desc.DepthOrArraySize == 1) {
                            desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                            desc.Texture2D.MipLevels       = res_desc.MipLevels;
                            desc.Texture2D.MostDetailedMip = 0;
                        } else {
                            desc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                            desc.Texture2DArray.MipLevels       = res_desc.MipLevels;
                            desc.Texture2DArray.ArraySize       = res_desc.DepthOrArraySize;
                            desc.Texture2DArray.FirstArraySlice = 0;
                            desc.Texture2DArray.MostDetailedMip = 0;
                        }
                    } else if (res_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                        desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE3D;
                        desc.Texture3D.MipLevels       = res_desc.MipLevels;
                        desc.Texture3D.MostDetailedMip = 0;
                    }

                    m_pParent->m_pDevice->GetDevice()->CreateShaderResourceView(pTexture, &desc,
                                                                                pGlobalTable->GetCPU(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT + slot));
                }
            };

            uint32_t i = 0;
            for (auto &item : m_cpuTextureTable) {
                bindTexture(item, i++);
            }
        }
        void UpdateDescriptorTable(CBV_SRV_UAV *pGlobalTable);

        ID3D12Resource *getScratchBuffer(size_t size) {
            if (m_pScratchBuffer == NULL || m_pScratchBuffer->GetDesc().Width < size) {
                m_pScratchBuffer = m_pParent->CreateGPULocalUAVBuffer(size, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                m_scratchBuffers.push_back(m_pScratchBuffer);
            }
            return m_pScratchBuffer;
        }

        ID3D12Resource *getTMPBLASBuffer(size_t size) {
            if (m_pTmpBLAS == NULL || m_pTmpBLAS->GetDesc().Width < size) {
                m_pTmpBLAS = m_pParent->CreateGPULocalUAVBuffer(size, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
                m_pTmpBLASBuffers.push_back(m_pTmpBLAS);
            }
            return m_pTmpBLAS;
        }

        void FlushScratchBuffers() {
            // Keep m_pScratchBuffer for later
            for (auto buf : m_pTmpBLASBuffers)
                if (buf != m_pTmpBLAS) buf->Release();
            for (auto buf : m_scratchBuffers)
                if (buf != m_pScratchBuffer) buf->Release();

            m_pTmpBLASBuffers.clear();
            m_scratchBuffers.clear();
        }

        void ReleaseAccelerationStructuresOnly() {
            m_bakeSkinning.Release();
            for (auto buf : m_scratchBuffers) buf->Release();
            for (auto buf : m_pTmpBLASBuffers) buf->Release();
            m_scratchBuffers.clear();
            m_pTmpBLASBuffers.clear();
            m_pScratchBuffer = NULL;
            m_pTmpBLAS       = NULL;
            m_cpuOpaqueTLASInstances.clear();
            m_cpuTransparentTLASInstances.clear();
            m_cpuGlobalTLASInstances.clear();
#define SAFE_RELEASE(b)                                                                                                                                                            \
    if (b) {                                                                                                                                                                       \
        b->Release();                                                                                                                                                              \
        b = NULL;                                                                                                                                                                  \
    }
            SAFE_RELEASE(m_pOpaqueTlas);
            SAFE_RELEASE(m_pTranpsparentTlas);
            SAFE_RELEASE(m_pGlobalTlas);
            SAFE_RELEASE(m_pOpaqueTLASInstances);
            SAFE_RELEASE(m_pTransparentTLASInstances);
            SAFE_RELEASE(m_pGlobalTLASInstances);
            SAFE_RELEASE(m_pCpuvizOpaqueTLASInstances);
            SAFE_RELEASE(m_pCpuvizTransparentTLASInstances);
            SAFE_RELEASE(m_pCpuvizGlobalTLASInstances);
#undef SAFE_RELEASE
            for (auto &item : m_blas_table) item.second.first->Release();
            m_blas_table.clear();
        }

        void Release() {
            ReleaseAccelerationStructuresOnly();
            m_cpuMaterialBuffer.clear();
            m_cpuInstanceBuffer.clear();
            m_cpuSurfaceBuffer.clear();
            m_cpuTextureTable.clear();
            m_cpuSurfaceIDsBuffer.clear();
            m_pSrcGeometryBufferResource = NULL;
#define SAFE_RELEASE(b)                                                                                                                                                            \
    if (b) {                                                                                                                                                                       \
        b->Release();                                                                                                                                                              \
        b = NULL;                                                                                                                                                                  \
    }
            SAFE_RELEASE(m_pMaterialBuffer);
            SAFE_RELEASE(m_pSurfaceBuffer);
            SAFE_RELEASE(m_pSurfaceIDsBuffer);
            SAFE_RELEASE(m_pInstanceBuffer);
#undef SAFE_RELEASE
            *this = {};
        }
    } m_infoTables;

    ID3D12Resource *CreateGPULocalUAVBuffer(size_t size, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource *CreateUploadBuffer(size_t size, D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ);
    void            InitializeAccelerationStructures(CBV_SRV_UAV *pGlobalTable);
    ID3D12Resource *CreateBLASForSurfaces(ID3D12GraphicsCommandList5 *pCommandList, std::vector<uint32_t> const &surfaceIDs);
    void            UpdateBLASForSurfaces(ID3D12GraphicsCommandList5 *pCommandList, std::vector<uint32_t> const &surfaceIDs, ID3D12Resource *pBLAS);
    ID3D12Resource *CreateTLASForInstances(ID3D12GraphicsCommandList5 *pCommandList, ID3D12Resource *pInstances, uint32_t numInstaces);
    void            UpdateTLASForInstances(ID3D12GraphicsCommandList5 *pCommandList, ID3D12Resource *pInstances, uint32_t numInstaces, ID3D12Resource *pTLAS);
    void            UpdateAccelerationStructures(ID3D12GraphicsCommandList5 *pCommandList, CBV_SRV_UAV *pGlobalTable);
    void            Barriers(ID3D12GraphicsCommandList *pCmdLst, const std::vector<D3D12_RESOURCE_BARRIER> &barriers) {
        pCmdLst->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
    void BakeSkeletalAnimation(ID3D12GraphicsCommandList *pCommandList, CBV_SRV_UAV *pGlobalTable) {
        UserMarker          marker(pCommandList, "RTGltfPbrPass::BakeSkeletalAnimationForBatch");
        std::vector<tfNode> nodes = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_nodes;
        for (auto &item : m_infoTables.m_cpuSkinnedSurfaces) {
            BakeSkinning::PushConstants pc{};
            pc.src_surface_id                      = item.src_surface_id;
            pc.dst_surface_id                      = item.dst_surface_id;
            pc.num_vertices                        = m_infoTables.m_cpuSurfaceBuffer[item.src_surface_id].num_vertices;
            D3D12_GPU_VIRTUAL_ADDRESS pPerSkeleton = m_pGLTFTexturesAndBuffers->GetSkinningMatricesBuffer(nodes[item.instance_id].skinIndex);
            if (pPerSkeleton == NULL) continue;
            m_infoTables.m_bakeSkinning.Draw(pCommandList, pGlobalTable, pPerSkeleton, pc);
        }
    }
    ///////////////////
    ///////////////////
    ///////////////////
};
} // namespace RTCAULDRON_DX12
