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

#pragma once

#include <memory>

#include "GltfPbrPass.h"
#include "HSR.h"
#include "PostProc/MagnifierPS.h"
#include "base/SaveTexture.h"

// We are queuing (backBufferCount + 0.5) frames, so we need to triple buffer the resources that get modified each frame
static const int backBufferCount = 3;

#define USE_VID_MEM true

using namespace CAULDRON_DX12;
using namespace RTCAULDRON_DX12;
using namespace HSR_SAMPLE_DX12;

inline void Barriers(ID3D12GraphicsCommandList *pCmdLst, const std::vector<D3D12_RESOURCE_BARRIER> &barriers) {
    pCmdLst->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

class DecalRenderer {
public:
    void OnCreate(Device *pDevice, UploadHeap *pUploadHeap, ID3D12RootSignature *pGlobalRootSignature) {
        m_DecalAlbedo.InitFromFile(pDevice, pUploadHeap, "../media/decal_mask.png", true);
        {
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            CompileShaderFromFile("ApplyDecals.hlsl", &defines, "main", "-T cs_6_5 /Zi /Zss", &shaderByteCode);
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS                                = shaderByteCode;
            descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature                    = pGlobalRootSignature;
            descPso.NodeMask                          = 0;
            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pApplyDecalPSO)));
        }
    }
    void Bind(CBV_SRV_UAV *pGlobalTable) { m_DecalAlbedo.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_DECAL_ALBEDO_SLOT, pGlobalTable); }
    void Apply(ID3D12GraphicsCommandList *pCommandList, int width, int height) {
        pCommandList->SetPipelineState(m_pApplyDecalPSO);
        pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    }
    void OnDestroy() {
        m_DecalAlbedo.OnDestroy();
        if (m_pApplyDecalPSO) m_pApplyDecalPSO->Release();
    }

private:
    Texture              m_DecalAlbedo;
    ID3D12PipelineState *m_pApplyDecalPSO = NULL;
};
// Generates atmoshpere LUT for specular and diffuse image based lightning
class AtmosphereRenderer {
public:
    D3D12_RESOURCE_STATES m_CubeLUT_State  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_CubeMIP_State  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_CubeDiff_State = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    void                  OnCreate(Device *pDevice, ID3D12RootSignature *pGlobalRootSignature, uint32_t size) {
        assert(size);
        int mip_levels = 0;
        int mip_size   = size;
        while (mip_size) {
            mip_size /= 2;
            mip_levels++;
        }
        CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, size, size, 6, mip_levels, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_CubeLUT.Init(pDevice, "Atmosphere LUT", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
        m_CubeMIP.Init(pDevice, "Atmosphere Mip", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
        desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, 128, 128, 6, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_CubeDiff.Init(pDevice, "Atmosphere Diffuse", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

        {
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            CompileShaderFromFile("GenerateAtmosphereLUT.hlsl", &defines, "mainSpecular", "-T cs_6_5 /Zi /Zss", &shaderByteCode);
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS                                = shaderByteCode;
            descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature                    = pGlobalRootSignature;
            descPso.NodeMask                          = 0;
            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pUpdateLUTPSO)));
        }
        {
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            CompileShaderFromFile("GenerateAtmosphereLUT.hlsl", &defines, "mainDiffuse", "-T cs_6_5 /Zi /Zss", &shaderByteCode);
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS                                = shaderByteCode;
            descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature                    = pGlobalRootSignature;
            descPso.NodeMask                          = 0;
            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pUpdateDiffPSO)));
        }
        {
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            CompileShaderFromFile("DownsampleAtmosphere.hlsl", &defines, "main", "-T cs_6_5 /Zi /Zss", &shaderByteCode);
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS                                = shaderByteCode;
            descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature                    = pGlobalRootSignature;
            descPso.NodeMask                          = 0;
            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pUpdateMIPPSO)));
        }
        {
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            CompileShaderFromFile("DrawAtmosphere.hlsl", &defines, "main", "-T cs_6_5 /Zi /Zss", &shaderByteCode);
            D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
            descPso.CS                                = shaderByteCode;
            descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
            descPso.pRootSignature                    = pGlobalRootSignature;
            descPso.NodeMask                          = 0;
            ThrowIfFailed(pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_pDrawPSO)));
        }
    }
    void Bind(CBV_SRV_UAV *pGlobalTable) {
        m_CubeLUT.CreateCubeSRV(GDT_CTEXTURES_HEAP_OFFSET + GDT_CTEXTURES_ATMOSPHERE_LUT_SLOT, pGlobalTable);
        m_CubeMIP.CreateCubeSRV(GDT_CTEXTURES_HEAP_OFFSET + GDT_CTEXTURES_ATMOSPHERE_MIP_SLOT, pGlobalTable);
        m_CubeDiff.CreateCubeSRV(GDT_CTEXTURES_HEAP_OFFSET + GDT_CTEXTURES_ATMOSPHERE_DIF_SLOT, pGlobalTable);

        for (int layer = 0; layer < 6; layer++) {
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Texture2DArray.MipSlice        = 0;
                uavDesc.Texture2DArray.FirstArraySlice = layer;
                uavDesc.Texture2DArray.ArraySize       = 1;
                uavDesc.Texture2DArray.PlaneSlice      = 0;
                uavDesc.Format                         = DXGI_FORMAT_R16G16B16A16_FLOAT;
                uavDesc.ViewDimension                  = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                m_CubeDiff.CreateUAV(GDT_RW_ATEXTURES_HEAP_OFFSET + GDT_RW_ATEXTURES_ATMOSPHERE_DIF_BEGIN_SLOT + layer, NULL, pGlobalTable, &uavDesc);
            }
            for (UINT mip = 0; mip < m_CubeLUT.GetMipCount(); mip++) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Texture2DArray.MipSlice        = mip;
                uavDesc.Texture2DArray.FirstArraySlice = layer;
                uavDesc.Texture2DArray.ArraySize       = 1;
                uavDesc.Texture2DArray.PlaneSlice      = 0;
                uavDesc.Format                         = DXGI_FORMAT_R16G16B16A16_FLOAT;
                uavDesc.ViewDimension                  = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                m_CubeLUT.CreateUAV(GDT_RW_ATEXTURES_HEAP_OFFSET + GDT_RW_ATEXTURES_ATMOSPHERE_LUT_BEGIN_SLOT + layer * m_CubeLUT.GetMipCount() + mip, NULL, pGlobalTable,
                                    &uavDesc);
                m_CubeMIP.CreateUAV(GDT_RW_ATEXTURES_HEAP_OFFSET + GDT_RW_ATEXTURES_ATMOSPHERE_MIP_BEGIN_SLOT + layer * m_CubeMIP.GetMipCount() + mip, NULL, pGlobalTable,
                                    &uavDesc);
            }
        }
    }
    void BarriersToUpdate(ID3D12GraphicsCommandList *pCommandList) {
        if (m_CubeLUT_State == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) return;
        Barriers(pCommandList, {
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_CubeLUT.GetResource(), m_CubeLUT_State, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_CubeDiff.GetResource(), m_CubeDiff_State, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                               });
        m_CubeLUT_State  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_CubeDiff_State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    void BarriersForNonPixelResource(ID3D12GraphicsCommandList *pCommandList) {
        UserMarker marker(pCommandList, "Atmosphere BarriersForNonPixelResource");
        if (m_CubeLUT_State == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) return;
        Barriers(pCommandList, {
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_CubeLUT.GetResource(), m_CubeLUT_State, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_CubeDiff.GetResource(), m_CubeDiff_State, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                               });
        m_CubeLUT_State  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_CubeDiff_State = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    void BarriersForPixelResource(ID3D12GraphicsCommandList *pCommandList) {
        if (m_CubeLUT_State == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) return;
        Barriers(pCommandList, {
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_CubeLUT.GetResource(), m_CubeLUT_State, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_CubeDiff.GetResource(), m_CubeDiff_State, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                               });
        m_CubeLUT_State  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_CubeDiff_State = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    void Update(ID3D12GraphicsCommandList *pCommandList, Vectormath::Vector3 viewPos, Vectormath::Vector3 sunDirection, Vectormath::Vector3 sunIntensity) {
        if (Vectormath::SSE::length(m_PrevViewPos - viewPos) < 1.0e-3f && Vectormath::SSE::length(m_PrevSunDirection - sunDirection) < 1.0e-3f &&
            Vectormath::SSE::length(m_PrevSunIntensity - sunIntensity) < 1.0e-3f)
            return;
        m_PrevViewPos      = viewPos;
        m_PrevSunDirection = sunDirection;
        m_PrevSunIntensity = sunIntensity;

        struct PushConstants {
            hlsl::float3 up;
            hlsl::uint   mip_level;
            hlsl::float3 right;
            hlsl::uint   pad0;
            hlsl::float3 view;
            hlsl::uint   dst_slice;
            hlsl::float3 world_offset;
            hlsl::uint   pad1;
            hlsl::float3 sun_direction;
            hlsl::uint   pad2;
            hlsl::float3 sun_intensity;
            hlsl::uint   pad3;
        } pc{};
        // Local Basis for each face
        hlsl::float3 views[] = {
            {{1.0f, 0.0f, 0.0f}},  // X+
            {{-1.0f, 0.0f, 0.0f}}, // X-
            {{0.0f, 1.0f, 0.0f}},  // Y+
            {{0.0f, -1.0f, 0.0f}}, // Y-
            {{0.0f, 0.0f, 1.0f}},  // Z+
            {{0.0f, 0.0f, -1.0f}}, // Z-
        };
        hlsl::float3 ups[] = {
            {{0.0f, 1.0f, 0.0f}},  // X+
            {{0.0f, 1.0f, 0.0f}},  // X-
            {{0.0f, 0.0f, -1.0f}}, // Y+
            {{0.0f, 0.0f, 1.0f}},  // Y-
            {{0.0f, 1.0f, 0.0f}},  // Z+
            {{0.0f, 1.0f, 0.0f}},  // Z-
        };
        hlsl::float3 rights[] = {
            {{0.0f, 0.0f, -1.0f}}, // X+
            {{0.0f, 0.0f, 1.0f}},  // X-
            {{1.0f, 0.0f, 0.0f}},  // Y+
            {{1.0f, 0.0f, 0.0f}},  // Y-
            {{1.0f, 0.0f, 0.0f}},  // Z+
            {{-1.0f, 0.0f, 0.0f}}, // Z-
        };
        Barriers(pCommandList,
                 {
                     CD3DX12_RESOURCE_BARRIER::Transition(m_CubeMIP.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                 });
        pCommandList->SetPipelineState(m_pUpdateLUTPSO);
        for (int layer = 0; layer < 6; layer++) {
            pc.mip_level     = 0;
            pc.dst_slice     = layer * m_CubeLUT.GetMipCount();
            pc.right         = rights[layer];
            pc.up            = ups[layer];
            pc.view          = views[layer];
            pc.world_offset  = viewPos;
            pc.sun_direction = sunDirection;
            pc.sun_intensity = sunIntensity;
            pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);
            int size = max(1, m_CubeLUT.GetWidth());
            pCommandList->Dispatch((size + 7) / 8, (size + 7) / 8, 1);
        }

        Barriers(pCommandList, {CD3DX12_RESOURCE_BARRIER::UAV(m_CubeLUT.GetResource())});
        {
            struct PushConstants {
                hlsl::uint  mips;
                hlsl::uint  numWorkGroups;
                hlsl::uint2 workGroupOffset;
            } pc{};
            pc.numWorkGroups        = ((m_CubeLUT.GetWidth() + 63) / 64) * ((m_CubeLUT.GetWidth() + 63) / 64);
            pc.mips                 = m_CubeMIP.GetMipCount();
            pc.workGroupOffset.m[0] = 0;
            pc.workGroupOffset.m[1] = 0;
            pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);
            pCommandList->SetPipelineState(m_pUpdateMIPPSO);
            pCommandList->Dispatch((m_CubeLUT.GetWidth() + 63) / 64, (m_CubeLUT.GetWidth() + 63) / 64, 6);
        }
        Barriers(pCommandList,
                 {
                     CD3DX12_RESOURCE_BARRIER::UAV(m_CubeMIP.GetResource()),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_CubeMIP.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                 });
        pCommandList->SetPipelineState(m_pUpdateLUTPSO);
        for (int layer = 0; layer < 6; layer++) {
            for (UINT mip = 1; mip < m_CubeLUT.GetMipCount(); mip++) {
                pc.mip_level     = mip;
                pc.dst_slice     = layer * m_CubeLUT.GetMipCount() + mip;
                pc.right         = rights[layer];
                pc.up            = ups[layer];
                pc.view          = views[layer];
                pc.world_offset  = viewPos;
                pc.sun_direction = sunDirection;
                pc.sun_intensity = sunIntensity;
                pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);
                int size = max(1, m_CubeLUT.GetWidth() >> mip);
                pCommandList->Dispatch((size + 7) / 8, (size + 7) / 8, 1);
            }
        }
        Barriers(pCommandList, {CD3DX12_RESOURCE_BARRIER::UAV(m_CubeLUT.GetResource())});
        pCommandList->SetPipelineState(m_pUpdateDiffPSO);
        for (int layer = 0; layer < 6; layer++) {
            pc.mip_level     = 0;
            pc.dst_slice     = layer;
            pc.right         = rights[layer];
            pc.up            = ups[layer];
            pc.view          = views[layer];
            pc.world_offset  = viewPos;
            pc.sun_direction = sunDirection;
            pc.sun_intensity = sunIntensity;
            pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);
            int size = max(1, m_CubeLUT.GetWidth());
            pCommandList->Dispatch((size + 7) / 8, (size + 7) / 8, 1);
        }
    }
    void Draw(ID3D12GraphicsCommandList *pCommandList, uint32_t width, uint32_t height) {
        pCommandList->SetPipelineState(m_pDrawPSO);
        pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    }
    void OnDestroy() {
        m_CubeLUT.OnDestroy();
        if (m_pUpdateLUTPSO) m_pUpdateLUTPSO->Release();
        if (m_pUpdateMIPPSO) m_pUpdateMIPPSO->Release();
        if (m_pUpdateDiffPSO) m_pUpdateDiffPSO->Release();
    }
    Texture *GetSpecularLUT() { return &m_CubeLUT; }
    Texture *GetDiffuseLUT() { return &m_CubeDiff; }

private:
    Vectormath::Vector3  m_PrevViewPos;
    Vectormath::Vector3  m_PrevSunDirection;
    Vectormath::Vector3  m_PrevSunIntensity;
    Texture              m_CubeMIP;
    Texture              m_CubeLUT;
    Texture              m_CubeDiff;
    ID3D12PipelineState *m_pUpdateLUTPSO  = NULL;
    ID3D12PipelineState *m_pUpdateMIPPSO  = NULL;
    ID3D12PipelineState *m_pUpdateDiffPSO = NULL;
    ID3D12PipelineState *m_pDrawPSO       = NULL;
};

namespace Vectormath {
inline Vector3 ToVec3(float *v) { return Vector3(v[0], v[1], v[2]); }
} // namespace Vectormath

//
// This class deals with the GPU side of the sample.
//
class SampleRenderer {
public:
    void OnCreate(Device *pDevice, SwapChain *pSwapChain);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, uint32_t Width, uint32_t Height, uint32_t ReflectionWidth, uint32_t ReflectionHeight, State *pState);
    void OnDestroyWindowSizeDependentResources();

    int  LoadScene(GLTFCommon *pGLTFCommon, int stage = 0);
    void UnloadScene();

    const std::vector<TimeStamp> &GetTimingValues() { return m_TimeStamps; }

    void OnRender(State *pState, SwapChain *pSwapChain);
    void Recompile();

    uint32_t getWidth() { return m_Width; }
    uint32_t getHeight() { return m_Height; }
    bool     IsReady() const { return m_ready; }

private:
    void CreateDepthDownsamplePipeline();
    void StallFrame(float targetFrametime);
    void BeginFrame();

    per_frame *FillFrameConstants(State *pState);
    void       RenderSpotLights(ID3D12GraphicsCommandList *pCmdLst1, per_frame *pPerFrame);
    void       RenderLightFrustums(ID3D12GraphicsCommandList *pCmdLst1, per_frame *pPerFrame, State *pState);
    void       DownsampleDepthBuffer(ID3D12GraphicsCommandList *pCmdLst1, State *pState);
    void       RenderScreenSpaceReflections(ID3D12GraphicsCommandList *pCmdLst1, per_frame *pPerFrame, State *pState);
    void       CopyHistorySurfaces(ID3D12GraphicsCommandList *pCmdLst1, State *pState);
    void       DownsampleScene(ID3D12GraphicsCommandList *pCmdLst1);
    void       RenderBloom(ID3D12GraphicsCommandList *pCmdLst1);
    void       ApplyTonemapping(ID3D12GraphicsCommandList *pCmdLst2, State *pState, SwapChain *pSwapChain);
    void       RenderHUD(ID3D12GraphicsCommandList *pCmdLst2, SwapChain *pSwapChain);

private:
    bool       m_ready = false;
    Device *   m_pDevice;
    SwapChain *m_pSwapChain;

    GBuffer           m_GBuffer;
    GBufferRenderPass m_GBufferRenderPass;
    Texture           m_PrevHDR;

    struct ReflectionGBuffer {
        Texture Depth;
        Texture Normals;
        Texture SpecularRoughness;
        Texture Albedo;
        Texture MotionVectors;
    } m_ReflectionUAVGbuffer;

    GBuffer           m_ReflectionGBuffer;
    GBufferRenderPass m_ReflectionGBufferRenderPass;

    MagnifierPS m_Magnifier;

    uint32_t m_Width;
    uint32_t m_ReflectionWidth;
    uint32_t m_Height;
    uint32_t m_ReflectionHeight;

    D3D12_VIEWPORT m_ReflectionViewport;
    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT     m_Scissor;
    D3D12_RECT     m_ReflectionScissor;

    // Initialize helper classes
    ResourceViewHeaps      m_ResourceViewHeaps;
    StaticResourceViewHeap m_CpuVisibleHeap;
    UploadHeap             m_UploadHeap;
    DynamicBufferRing      m_ConstantBufferRing;
    StaticBufferPool       m_VidMemBufferPool;
    CommandListRing        m_CommandListRing;
    GPUTimestamps          m_GPUTimer;

    Texture m_downsampleCounter;

    // gltf passes
    RTGltfPbrPass *         m_gltfPBR;
    GltfBBoxPass *          m_gltfBBox;
    GltfDepthPass *         m_gltfDepth;
    GLTFTexturesAndBuffers *m_pGLTFTexturesAndBuffers;

    // effects
    Bloom              m_Bloom;
    AtmosphereRenderer m_AtmosphereRenderer;
    DownSamplePS       m_DownSample;
    ToneMapping        m_ToneMapping;

    DecalRenderer m_DecalRenderer;

    // SSSR
    HSR                 m_hsr;
    uint32_t            m_frame_index = 0;
    Vectormath::Matrix4 m_prev_view_projection;

    // BRDF LUT
    Texture m_BrdfLut;

    // GUI
    ImGUI m_ImGUI;

    TAA     m_taa;
    Texture m_NormalHistoryBuffer;
    Texture m_DepthHistoryBuffer;

    // shadowmaps
    struct ShadowMap {
        Texture     m_ShadowMap;
        DSV         m_ShadowMapDSV;
        CBV_SRV_UAV m_ShadowMapSRV;
    } m_shadowmaps[32];

    // widgets
    Wireframe    m_Wireframe;
    WireframeBox m_WireframeBox;

    std::vector<TimeStamp> m_TimeStamps;

    // Depth downsampling with single CS
    ID3D12RootSignature *       m_DownsampleRootSignature;
    ID3D12PipelineState *       m_DownsamplePipelineState;
    D3D12_GPU_DESCRIPTOR_HANDLE m_DownsampleDescriptorTable;
    CBV_SRV_UAV                 m_DepthBufferDescriptor;
    CBV_SRV_UAV                 m_DepthHierarchyDescriptors[13];
    CBV_SRV_UAV                 m_AtomicCounterUAVGPU;
    Texture                     m_DepthHierarchy;
    Texture                     m_AtomicCounter;
    CBV_SRV_UAV                 m_AtomicCounterUAV;
    UINT                        m_DepthMipLevelCount = 0;

    UINT64 m_GpuTicksPerSecond;

    SaveTexture m_SaveTexture;

    // For multithreaded texture loading
    AsyncPool m_AsyncPool;

    ID3D12RootSignature *m_pGlobalRootSignature = nullptr;
    CBV_SRV_UAV          m_globalDescriptorTables[3]{};
    SAMPLER              m_globalSamplerTables[3]{};
    int                  m_frameID = 0;

    CBV_SRV_UAV *GetCurrentUAVHeap() { return &m_globalDescriptorTables[m_frameID % 3]; }

    SAMPLER *GetCurrentSamplerHeap() { return &m_globalSamplerTables[m_frameID % 3]; }
};
