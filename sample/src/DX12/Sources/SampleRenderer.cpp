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

#include "stdafx.h"

#include "SampleRenderer.h"
#include "Utils.h"
#include <deque>

#undef max
#undef min

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreate(Device *pDevice, SwapChain *pSwapChain) {
    m_pDevice    = pDevice;
    m_pSwapChain = pSwapChain;
    // Initialize helpers
    // Global Signature
    {

        CD3DX12_ROOT_PARAMETER RTSlot[3] = {};

        int                      parameterCount                       = 3;
        CD3DX12_DESCRIPTOR_RANGE DescRange_2[GDT_SAMPLERS_NUM_RANGES] = {};
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
    DescRange_2[sampler_params++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, num_of_registers, register_offset, space, heap_offset);

        INIT_GLOBAL_RANGES(/*in SPACE_ID*/ 1);

#undef ADD_TLAS_RANGE
#undef ADD_TEXTURE_RANGE
#undef ADD_UAV_TEXTURE_RANGE
#undef ADD_BUFFER_RANGE
#undef ADD_UNIFORM_BUFFER_RANGE
#undef ADD_SAMPLER_RANGE

        RTSlot[1].InitAsDescriptorTable(GDT_SAMPLERS_NUM_RANGES, &DescRange_2[0], D3D12_SHADER_VISIBILITY_ALL);
        RTSlot[0].InitAsDescriptorTable(GDT_CBV_SRV_UAV_NUM_RANGES, globalDescritorRanges);
        RTSlot[2].InitAsConstants(32, DX12_PUSH_CONSTANTS_REGISTER, 0);

        CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
        descRootSignature.NumParameters               = parameterCount;
        descRootSignature.pParameters                 = &RTSlot[0];
        // deny uneccessary access to certain pipeline stages
        descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob *pOutBlob   = nullptr;
        ID3DBlob *pErrorBlob = nullptr;
        ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
        ThrowIfFailed(m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pGlobalRootSignature)));

        pOutBlob->Release();
        if (pErrorBlob) pErrorBlob->Release();
    }

    // Create all the heaps for the resources views
    const uint32_t cbvDescriptorCount     = 1 << 16;
    const uint32_t srvDescriptorCount     = 1 << 16;
    const uint32_t uavDescriptorCount     = 1 << 16;
    const uint32_t dsvDescriptorCount     = 1 << 16;
    const uint32_t rtvDescriptorCount     = 1 << 16;
    const uint32_t samplerDescriptorCount = 2048;
    m_ResourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, dsvDescriptorCount, rtvDescriptorCount, samplerDescriptorCount);
    m_CpuVisibleHeap.OnCreate(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 20, true);
    m_downsampleCounter.InitBuffer(m_pDevice, "HSR - Downsample Counter", &CD3DX12_RESOURCE_DESC::Buffer(12 * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), 4,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // Create a commandlist ring for the Direct queue
    uint32_t commandListsPerBackBuffer = 8;
    m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer, pDevice->GetGraphicsQueue()->GetDesc());

    // Create a 'dynamic' constant buffer
    const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
    m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, &m_ResourceViewHeaps);

    // Create a 'static' pool for vertices, indices and constant buffers
    const uint32_t staticGeometryMemSize = 5 * 128 * 1024 * 1024;
    m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, USE_VID_MEM, "StaticGeom");
    // initialize the GPU time stamps module
    m_GPUTimer.OnCreate(pDevice, backBufferCount);
    m_GBuffer.OnCreate(m_pDevice, &m_ResourceViewHeaps,
                       {
                           {GBUFFER_DEPTH, DXGI_FORMAT_D32_FLOAT},
                           {GBUFFER_DIFFUSE, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
                           {GBUFFER_FORWARD, DXGI_FORMAT_R16G16B16A16_FLOAT},
                           {GBUFFER_SPECULAR_ROUGHNESS, DXGI_FORMAT_R8G8B8A8_UNORM},
                           {GBUFFER_MOTION_VECTORS, DXGI_FORMAT_R16G16_FLOAT},
                           {GBUFFER_NORMAL_BUFFER, DXGI_FORMAT_R10G10B10A2_UNORM},
                       },
                       1);
    m_ReflectionGBuffer.OnCreate(m_pDevice, &m_ResourceViewHeaps,
                                 {
                                     {GBUFFER_DEPTH, DXGI_FORMAT_D32_FLOAT},
                                     {GBUFFER_DIFFUSE, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB},
                                     {GBUFFER_FORWARD, DXGI_FORMAT_R16G16B16A16_FLOAT},
                                     {GBUFFER_SPECULAR_ROUGHNESS, DXGI_FORMAT_R8G8B8A8_UNORM},
                                     {GBUFFER_MOTION_VECTORS, DXGI_FORMAT_R16G16_FLOAT},
                                     {GBUFFER_NORMAL_BUFFER, DXGI_FORMAT_R10G10B10A2_UNORM},
                                 },
                                 1);

    m_GBufferRenderPass.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_DIFFUSE | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS | GBUFFER_NORMAL_BUFFER | GBUFFER_SPECULAR_ROUGHNESS);

    m_ReflectionGBufferRenderPass.OnCreate(&m_ReflectionGBuffer,
                                           GBUFFER_DEPTH | GBUFFER_DIFFUSE | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS | GBUFFER_NORMAL_BUFFER | GBUFFER_SPECULAR_ROUGHNESS);

    m_GBuffer.OnCreateWindowSizeDependentResources(m_pSwapChain, 512, 512);
    m_Magnifier.OnCreate(m_pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, m_GBuffer.m_HDR.GetFormat(), false);
    m_GBufferRenderPass.OnCreateWindowSizeDependentResources(512, 512);
    m_ReflectionGBuffer.OnCreateWindowSizeDependentResources(m_pSwapChain, 512, 512);
    m_ReflectionGBufferRenderPass.OnCreateWindowSizeDependentResources(512, 512);
    m_taa.OnCreate(pDevice, &m_ResourceViewHeaps, &m_VidMemBufferPool);
    // Quick helper to upload resources, it has it's own commandList and uses suballocation.
    const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
    m_UploadHeap.OnCreate(pDevice, uploadHeapMemSize); // initialize an upload heap (uses suballocation for faster results)

    for (uint32_t i = 0; i < 32; i++) {
        static char namebuf[0x100];
        sprintf_s(namebuf, sizeof(namebuf), "m_pShadowMap_%i", i);
        m_shadowmaps[i].m_ShadowMap.InitDepthStencil(pDevice, namebuf,
                                                     &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, 4096, 4096, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
        m_ResourceViewHeaps.AllocDSVDescriptor(1, &m_shadowmaps[i].m_ShadowMapDSV);
        m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_shadowmaps[i].m_ShadowMapSRV);
        m_shadowmaps[i].m_ShadowMap.CreateDSV(0, &m_shadowmaps[i].m_ShadowMapDSV);
        m_shadowmaps[i].m_ShadowMap.CreateSRV(0, &m_shadowmaps[i].m_ShadowMapSRV);
    }
    m_Wireframe.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    m_WireframeBox.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
    m_DownSample.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_Bloom.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_AtmosphereRenderer.OnCreate(pDevice, m_pGlobalRootSignature, 1024);
    m_DecalRenderer.OnCreate(pDevice, &m_UploadHeap, m_pGlobalRootSignature);
    m_BrdfLut.InitFromFile(pDevice, &m_UploadHeap, "BrdfLut.dds", false); // LUT images are stored as linear

    // Create tonemapping pass
    m_ToneMapping.OnCreate(pDevice, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());

    // Initialize UI rendering resources
    m_ImGUI.OnCreate(pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, pSwapChain->GetFormat());

    CreateDepthDownsamplePipeline();
    m_CpuVisibleHeap.AllocDescriptor(1, &m_AtomicCounterUAV);

    m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_DepthBufferDescriptor);
    for (int i = 0; i < 13; ++i) {
        m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_DepthHierarchyDescriptors[i]);
    }
    m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_AtomicCounterUAVGPU);

    m_DownsampleDescriptorTable = m_DepthBufferDescriptor.GetGPU();

    // Create a command list for upload
    ID3D12CommandAllocator *ca;
    ThrowIfFailed(m_pDevice->GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)));
    ID3D12GraphicsCommandList *cl;
    ThrowIfFailed(m_pDevice->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, nullptr, IID_PPV_ARGS(&cl)));

    m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(GDT_CBV_SRV_UAV_SIZE, &m_globalDescriptorTables[0]);
    m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(GDT_CBV_SRV_UAV_SIZE, &m_globalDescriptorTables[1]);
    m_ResourceViewHeaps.AllocCBV_SRV_UAVDescriptor(GDT_CBV_SRV_UAV_SIZE, &m_globalDescriptorTables[2]);

    m_ResourceViewHeaps.AllocSamplerDescriptor(GDT_SAMPLERS_SIZE, &m_globalSamplerTables[0]);
    m_ResourceViewHeaps.AllocSamplerDescriptor(GDT_SAMPLERS_SIZE, &m_globalSamplerTables[1]);
    m_ResourceViewHeaps.AllocSamplerDescriptor(GDT_SAMPLERS_SIZE, &m_globalSamplerTables[2]);

    m_hsr.OnCreate(m_pDevice, m_pGlobalRootSignature, m_CpuVisibleHeap, m_ResourceViewHeaps, m_UploadHeap, m_ConstantBufferRing, backBufferCount, true);

    // Wait for the upload to finish;
    ThrowIfFailed(cl->Close());
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CommandListCast(&cl));
    m_pDevice->GPUFlush();
    cl->Release();
    ca->Release();

    // Make sure upload heap has finished uploading before continuing
#if (USE_VID_MEM == true)
    m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
    m_UploadHeap.FlushAndFinish();
#endif

    m_AtmosphereRenderer.Bind(&m_globalDescriptorTables[0]);
    m_AtmosphereRenderer.Bind(&m_globalDescriptorTables[1]);
    m_AtmosphereRenderer.Bind(&m_globalDescriptorTables[2]);
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroy() {
    m_GBuffer.OnDestroy();
    m_GBufferRenderPass.OnDestroy();
    m_ReflectionGBuffer.OnDestroy();
    m_ReflectionGBufferRenderPass.OnDestroy();
    m_ImGUI.OnDestroy();
    m_ToneMapping.OnDestroy();
    m_Bloom.OnDestroy();
    m_AtmosphereRenderer.OnDestroy();
    m_DecalRenderer.OnDestroy();
    m_DownSample.OnDestroy();
    m_WireframeBox.OnDestroy();
    m_Wireframe.OnDestroy();
    m_downsampleCounter.OnDestroy();
    for (uint32_t i = 0; i < 32; i++) {
        m_shadowmaps[i].m_ShadowMap.OnDestroy();
    }
    m_BrdfLut.OnDestroy();

    if (m_DownsamplePipelineState != nullptr) m_DownsamplePipelineState->Release();
    if (m_DownsampleRootSignature != nullptr) m_DownsampleRootSignature->Release();

    m_UploadHeap.OnDestroy();
    m_GPUTimer.OnDestroy();
    m_VidMemBufferPool.OnDestroy();
    m_ConstantBufferRing.OnDestroy();
    m_CommandListRing.OnDestroy();
    m_CpuVisibleHeap.OnDestroy();
    m_ResourceViewHeaps.OnDestroy();
    m_hsr.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnCreateWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, uint32_t Width, uint32_t Height, uint32_t ReflectionWidth, uint32_t ReflectionHeight,
                                                          State *pState) {
    m_Width            = Width;
    m_Height           = Height;
    m_ReflectionWidth  = ReflectionWidth;
    m_ReflectionHeight = ReflectionHeight;
    m_ReflectionGBuffer.OnCreateWindowSizeDependentResources(m_pSwapChain, m_ReflectionWidth, m_ReflectionHeight);
    m_GBuffer.OnCreateWindowSizeDependentResources(m_pSwapChain, m_Width, m_Height);
    // UAV Buffers for optimized downsampling
    {
        m_ReflectionUAVGbuffer.Depth.Init(
            m_pDevice, "UAV GBuffer Depth",
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, NULL);
        m_ReflectionUAVGbuffer.MotionVectors.Init(
            m_pDevice, "UAV GBuffer Motion Vectors",
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, NULL);
        m_ReflectionUAVGbuffer.Normals.Init(
            m_pDevice, "UAV GBuffer Normals",
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R10G10B10A2_UNORM, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, NULL);
        m_ReflectionUAVGbuffer.Albedo.Init(
            m_pDevice, "UAV GBuffer Albedo",
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, NULL);
        m_ReflectionUAVGbuffer.SpecularRoughness.Init(
            m_pDevice, "UAV GBuffer SpecularRoughness",
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    }
    if (m_GBuffer.m_HDR.GetResource()) m_taa.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_GBuffer);
    m_NormalHistoryBuffer.Init(m_pDevice, "m_NormalHistoryBuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R10G10B10A2_UNORM, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0),
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
    // Set the viewport
    m_PrevHDR.Init(m_pDevice, "m_PrevHDR", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, m_Width, m_Height, 1, 1, 1, 0),
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
    m_Magnifier.OnCreateWindowSizeDependentResources(&m_GBuffer.m_HDR);

    m_DepthHistoryBuffer.Init(m_pDevice, "Depth History", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, m_ReflectionWidth, m_ReflectionHeight, 1, 1, 1, 0),
                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
    m_Viewport           = {0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f};
    m_ReflectionViewport = {0.0f, 0.0f, static_cast<float>(m_ReflectionWidth), static_cast<float>(m_ReflectionHeight), 0.0f, 1.0f};

    // Create scissor rectangle
    //
    m_Scissor           = {0, 0, (LONG)m_Width, (LONG)m_Height};
    m_ReflectionScissor = {0, 0, (LONG)m_ReflectionWidth, (LONG)m_ReflectionHeight};

    // update bloom and downscaling effect
    //
    if (m_GBuffer.m_HDR.GetResource()) m_DownSample.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_GBuffer.m_HDR, 5); // downsample the HDR texture 5 times
    m_Bloom.OnCreateWindowSizeDependentResources(m_Width / 2, m_Height / 2, m_DownSample.GetTexture(), 5, &m_GBuffer.m_HDR);

    // update the pipelines if the swapchain render pass has changed (for example when the format of the swapchain changes)
    //
    m_ToneMapping.UpdatePipelines(pSwapChain->GetFormat());
    m_ImGUI.UpdatePipeline(pSwapChain->GetFormat());

    // Depth downsampling pass with single CS
    {
        uint32_t hiz_width  = m_ReflectionWidth;
        uint32_t hiz_height = m_ReflectionHeight;
        if (pState->bOptimizedDownsample) {
            hiz_width  = m_Width;
            hiz_height = m_Height;
        }
        m_DepthMipLevelCount = static_cast<uint32_t>(std::log2(std::max(hiz_width, hiz_height))) + 1;

        // Downsampled depth buffer
        CD3DX12_RESOURCE_DESC dsResDesc =
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, hiz_width, hiz_height, 1, m_DepthMipLevelCount, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_DepthHierarchy.Init(m_pDevice, "m_DepthHierarchy", &dsResDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
        UINT i = 0;
        for (; i < 13u; ++i) {
            m_DepthHierarchy.CreateUAV(0, &m_DepthHierarchyDescriptors[i], std::min(i, m_DepthMipLevelCount - 1));
        }

        // Atomic counter
        CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        resDesc.Format                = DXGI_FORMAT_R32_UINT;
        m_AtomicCounter.InitBuffer(m_pDevice, "m_AtomicCounter", &resDesc, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_AtomicCounter.CreateBufferUAV(0, NULL, &m_AtomicCounterUAV);
        m_AtomicCounter.CreateBufferUAV(0, NULL, &m_AtomicCounterUAVGPU);
    }

    HSRCreationInfo sssr_input_textures;
    sssr_input_textures.inputHeight  = m_Height;
    sssr_input_textures.inputWidth   = m_Width;
    sssr_input_textures.outputWidth  = m_ReflectionWidth;
    sssr_input_textures.outputHeight = m_ReflectionHeight;
    m_hsr.OnCreateWindowSizeDependentResources(sssr_input_textures);
}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroyWindowSizeDependentResources() {
    m_pDevice->GPUFlush();
    m_Bloom.OnDestroyWindowSizeDependentResources();
    m_DownSample.OnDestroyWindowSizeDependentResources();
    m_hsr.OnDestroyWindowSizeDependentResources();

    m_DepthHierarchy.OnDestroy();
    m_AtomicCounter.OnDestroy();
    m_NormalHistoryBuffer.OnDestroy();
    m_PrevHDR.OnDestroy();
    m_DepthHistoryBuffer.OnDestroy();
    m_GBuffer.OnDestroyWindowSizeDependentResources();
    m_ReflectionGBuffer.OnDestroyWindowSizeDependentResources();

    m_taa.OnDestroyWindowSizeDependentResources();
    m_Magnifier.OnDestroyWindowSizeDependentResources();
    m_ReflectionUAVGbuffer.Depth.OnDestroy();
    m_ReflectionUAVGbuffer.Albedo.OnDestroy();
    m_ReflectionUAVGbuffer.MotionVectors.OnDestroy();
    m_ReflectionUAVGbuffer.Normals.OnDestroy();
    m_ReflectionUAVGbuffer.SpecularRoughness.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int SampleRenderer::LoadScene(GLTFCommon *pGLTFCommon, int stage) {
    // show loading progress
    //
    ImGui::OpenPopup("Loading");
    if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        float progress = (float)stage / 13.0f;
        ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
        ImGui::EndPopup();
    }

    AsyncPool *pAsyncPool = &m_AsyncPool;

    // Loading stages
    //
    if (stage == 0) {
    } else if (stage == 5) {
        Profile p("m_pGltfLoader->Load");

        m_pGLTFTexturesAndBuffers = new GLTFTexturesAndBuffers();
        m_pGLTFTexturesAndBuffers->OnCreate(m_pDevice, pGLTFCommon, &m_UploadHeap, &m_VidMemBufferPool, &m_ConstantBufferRing);
    } else if (stage == 6) {
        Profile p("LoadTextures");

        // here we are loading onto the GPU all the textures and the inverse matrices
        // this data will be used to create the PBR and Depth passes
        m_pGLTFTexturesAndBuffers->LoadTextures(pAsyncPool);
    } else if (stage == 7) {
        {
            Profile p("m_gltfDepth->OnCreate");

            // create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
            m_gltfDepth = new GltfDepthPass();
            m_gltfDepth->OnCreate(m_pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, m_pGLTFTexturesAndBuffers, pAsyncPool);
        }

    } else if (stage == 9) {
        Profile p("m_gltfPBR->OnCreate");

        // same thing as above but for the PBR pass
        m_gltfPBR = new RTGltfPbrPass();
        m_gltfPBR->OnCreate(m_pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, m_pGLTFTexturesAndBuffers, &m_VidMemBufferPool,
                            m_AtmosphereRenderer.GetSpecularLUT(), m_AtmosphereRenderer.GetDiffuseLUT(), false, false, &m_GBufferRenderPass, pAsyncPool);
    } else if (stage == 10) {
        Profile p("m_gltfBBox->OnCreate");

        // just a bounding box pass that will draw boundingboxes instead of the geometry itself
        m_gltfBBox = new GltfBBoxPass();
        m_gltfBBox->OnCreate(m_pDevice, &m_UploadHeap, &m_ResourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, m_pGLTFTexturesAndBuffers, &m_Wireframe);
#if (USE_VID_MEM == true)
        // we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
        m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
#endif
    } else if (stage == 11) {
        Profile p("Flush");

        m_UploadHeap.FlushAndFinish();

#if (USE_VID_MEM == true)
        // once everything is uploaded we dont need he upload heaps anymore
        m_VidMemBufferPool.FreeUploadHeap();
#endif

        m_gltfPBR->InitializeAccelerationStructures(GetCurrentUAVHeap());

        // Initialize static geometry buffers and textures
        m_gltfPBR->BindMaterialResources(&m_globalDescriptorTables[0]);
        m_gltfPBR->BindMaterialResources(&m_globalDescriptorTables[1]);
        m_gltfPBR->BindMaterialResources(&m_globalDescriptorTables[2]);

        m_AtmosphereRenderer.Bind(&m_globalDescriptorTables[0]);
        m_AtmosphereRenderer.Bind(&m_globalDescriptorTables[1]);
        m_AtmosphereRenderer.Bind(&m_globalDescriptorTables[2]);

        m_ready = true;
        // tell caller that we are done loading the map
        return 0;
    }

    stage++;
    return stage;
}

//--------------------------------------------------------------------------------------
//
// UnloadScene
//
//--------------------------------------------------------------------------------------
void SampleRenderer::UnloadScene() {
    m_pDevice->GPUFlush();
    if (m_gltfPBR) {
        m_gltfPBR->OnDestroy();
        delete m_gltfPBR;
        m_gltfPBR = NULL;
    }

    if (m_gltfDepth) {
        m_gltfDepth->OnDestroy();
        delete m_gltfDepth;
        m_gltfDepth = NULL;
    }

    if (m_gltfBBox) {
        m_gltfBBox->OnDestroy();
        delete m_gltfBBox;
        m_gltfBBox = NULL;
    }

    if (m_pGLTFTexturesAndBuffers) {
        m_pGLTFTexturesAndBuffers->OnDestroy();
        delete m_pGLTFTexturesAndBuffers;
        m_pGLTFTexturesAndBuffers = NULL;
    }
}

void SampleRenderer::StallFrame(float targetFrametime) {
    // Simulate lower frame rates
    static std::chrono::system_clock::time_point last = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point        now  = std::chrono::system_clock::now();
    std::chrono::duration<double>                diff = now - last;
    last                                              = now;
    float deltaTime                                   = 1000 * static_cast<float>(diff.count());
    if (deltaTime < targetFrametime) {
        int deltaCount = static_cast<int>(targetFrametime - deltaTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(deltaCount));
    }
}

void SampleRenderer::BeginFrame() {
    // Timing values
    //
    m_pDevice->GetGraphicsQueue()->GetTimestampFrequency(&m_GpuTicksPerSecond);

    // Let our resource managers do some house keeping
    //
    m_ConstantBufferRing.OnBeginFrame();
    m_GPUTimer.OnBeginFrame(m_GpuTicksPerSecond, &m_TimeStamps);
    m_CommandListRing.OnBeginFrame();
}

per_frame *SampleRenderer::FillFrameConstants(State *pState) {
    // Sets the perFrame data (Camera and lights data), override as necessary and set them as constant buffers --------------
    //
    per_frame *pPerFrame = NULL;
    if (m_pGLTFTexturesAndBuffers) {
        pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(pState->camera);

        // override gltf camera with ours
        pPerFrame->mCameraCurrViewProj = pState->camera.GetProjection() * pState->camera.GetView();
        pPerFrame->cameraPos           = pState->camera.GetPosition();
        pPerFrame->emmisiveFactor      = pState->emmisiveFactor;
        pPerFrame->iblFactor           = pState->iblFactor;
        pPerFrame->lodBias             = pState->mipBias;

        // if the gltf doesn't have any lights set a directional light
        if (pPerFrame->lightCount == 0) {
            { // Sun

                pPerFrame->lights[0].color[0] = pState->lightColor.x;
                pPerFrame->lights[0].color[1] = pState->lightColor.y;
                pPerFrame->lights[0].color[2] = pState->lightColor.z;
                GetXYZ(pPerFrame->lights[0].position, pState->sunPosition);
                GetXYZ(pPerFrame->lights[0].direction, pState->sunDirection);
                pPerFrame->lights[0].innerConeCos   = 1.0f;
                pPerFrame->lights[0].outerConeCos   = 1.0f;
                pPerFrame->lights[0].range          = 30.0f; // in meters
                pPerFrame->lights[0].type           = LightType_Directional;
                pPerFrame->lights[0].intensity      = pState->SunLightIntensity;
                pPerFrame->lights[0].mLightViewProj = pState->sunProj * pState->sunView;
                pPerFrame->lights[0].mLightView     = pState->sunView;
                pPerFrame->lightCount++;
            }
            for (int i = 0; i < pState->NumSpotLights; i++) {
                float delta = pState->SpotLightSpread;
                pState->SpotLightLookAt[i].setX(0.0f);
                pState->SpotLightLookAt[i].setY(0.0f);
                pState->SpotLightLookAt[i].setZ((-pState->NumSpotLights / 2.0f + i) * delta);
                pState->SpotLightLookAt[i].setW(0.0f);
                pState->SpotLightPositions[i].setX(-5.0f);
                pState->SpotLightPositions[i].setY(2.0f);
                pState->SpotLightPositions[i].setZ((-pState->NumSpotLights / 2.0f + i) * delta);
                pState->SpotLightPositions[i].setW(0.0f);

                using namespace Vectormath::SSE;
                Camera lightCamera = pState->camera;
                lightCamera.LookAt(pState->SpotLightPositions[i], pState->SpotLightLookAt[i]);

                pPerFrame->lights[pPerFrame->lightCount].color[0] = 0.0f;
                pPerFrame->lights[pPerFrame->lightCount].color[1] = 0.3f;
                pPerFrame->lights[pPerFrame->lightCount].color[2] = 1.2f;

                GetXYZ(pPerFrame->lights[pPerFrame->lightCount].position, lightCamera.GetPosition());
                GetXYZ(pPerFrame->lights[pPerFrame->lightCount].direction, lightCamera.GetDirection());

                pPerFrame->lights[pPerFrame->lightCount].range          = 30.0f; // in meters
                pPerFrame->lights[pPerFrame->lightCount].type           = LightType_Spot;
                pPerFrame->lights[pPerFrame->lightCount].intensity      = 2.0f;
                pPerFrame->lights[pPerFrame->lightCount].innerConeCos   = cosf(lightCamera.GetFovV() * 0.2f / 2.0f);
                pPerFrame->lights[pPerFrame->lightCount].outerConeCos   = cosf(lightCamera.GetFovV() / 2.0f);
                pPerFrame->lights[pPerFrame->lightCount].mLightViewProj = lightCamera.GetProjection() * lightCamera.GetView();
                pPerFrame->lights[pPerFrame->lightCount].mLightView     = lightCamera.GetView();
                pPerFrame->lightCount++;
            }
            if (pState->bFlashLight) {

                using namespace Vectormath::SSE;
                if (pState->bAttachFlashLight) {
                    pState->flashlightCamera      = pState->camera;
                    Vectormath::Vector4 newPos    = pState->camera.GetPosition() + pState->camera.GetView().getRow(0) * -0.1f + pState->camera.GetView().getRow(1) * 0.1f;
                    Vectormath::Vector4 newLookAt = -pState->camera.GetDirection() + newPos;
                    pState->flashlightCamera.LookAt(newPos, newLookAt);
                }
                pPerFrame->lights[pPerFrame->lightCount].color[0] = 1.0f;
                pPerFrame->lights[pPerFrame->lightCount].color[1] = 1.0f;
                pPerFrame->lights[pPerFrame->lightCount].color[2] = 1.0f;

                GetXYZ(pPerFrame->lights[pPerFrame->lightCount].position, pState->flashlightCamera.GetPosition());
                GetXYZ(pPerFrame->lights[pPerFrame->lightCount].direction, pState->flashlightCamera.GetDirection());

                pPerFrame->lights[pPerFrame->lightCount].range          = 30.0f; // in meters
                pPerFrame->lights[pPerFrame->lightCount].type           = LightType_Spot;
                pPerFrame->lights[pPerFrame->lightCount].intensity      = pState->FlashLightIntensity;
                pPerFrame->lights[pPerFrame->lightCount].innerConeCos   = cosf(pState->flashlightCamera.GetFovV() * 0.9f / 2.0f);
                pPerFrame->lights[pPerFrame->lightCount].outerConeCos   = cosf(pState->flashlightCamera.GetFovV() / 2.0f);
                pPerFrame->lights[pPerFrame->lightCount].mLightViewProj = pState->flashlightCamera.GetProjection() * pState->flashlightCamera.GetView();
                pPerFrame->lights[pPerFrame->lightCount].mLightView     = pState->flashlightCamera.GetView();
                pPerFrame->lightCount++;
            }
        }

        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++) {
            if ((pPerFrame->lights[i].type == LightType_Spot)) {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index so the color pass knows which shadow map to use
                pPerFrame->lights[i].depthBias      = 20.0f / 100000.0f;
            } else if ((pPerFrame->lights[i].type == LightType_Directional)) {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // same as above
                pPerFrame->lights[i].depthBias      = 1.0f / 100000.0f;
            } else {
                pPerFrame->lights[i].shadowMapIndex = -1; // no shadow for this light
            }
        }

        m_pGLTFTexturesAndBuffers->SetPerFrameConstants();

        m_pGLTFTexturesAndBuffers->SetSkinningMatricesForSkeletons();
    }

    return pPerFrame;
}

void SampleRenderer::RenderSpotLights(ID3D12GraphicsCommandList *pCmdLst1, per_frame *pPerFrame) {
    UserMarker marker(pCmdLst1, "Shadow Map");

    for (uint32_t i = 0; i < pPerFrame->lightCount; i++) {
        if (pPerFrame->lights[i].type == LightType_Point || pPerFrame->lights[i].shadowMapIndex < 0) continue;
        // Set the RT's quadrant where to render the shadomap (these viewport offsets need to match the ones in shadowFiltering.h)
        uint32_t viewportWidth  = m_shadowmaps[pPerFrame->lights[i].shadowMapIndex].m_ShadowMap.GetWidth();
        uint32_t viewportHeight = m_shadowmaps[pPerFrame->lights[i].shadowMapIndex].m_ShadowMap.GetHeight();
        SetViewportAndScissor(pCmdLst1, 0, 0, viewportWidth, viewportHeight);
        pCmdLst1->OMSetRenderTargets(0, NULL, false, &m_shadowmaps[i].m_ShadowMapDSV.GetCPU());

        per_frame *cbDepthPerFrame           = m_gltfDepth->SetPerFrameConstants();
        cbDepthPerFrame->mCameraCurrViewProj = pPerFrame->lights[i].mLightViewProj;

        m_gltfDepth->Draw(pCmdLst1);

        m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow Map");
    }
}

void SampleRenderer::RenderLightFrustums(ID3D12GraphicsCommandList *pCmdLst1, per_frame *pPerFrame, State *pState) {
    UserMarker marker(pCmdLst1, "Light frustrums");

    Vectormath::Vector4 vCenter = Vectormath::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    Vectormath::Vector4 vRadius = Vectormath::Vector4(1.0f, 1.0f, 1.0f, 0.0f);
    Vectormath::Vector4 vColor  = Vectormath::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    for (uint32_t i = 0; i < pPerFrame->lightCount; i++) {
        Vectormath::Matrix4 spotlightMatrix = Vectormath::inverse(pPerFrame->lights[i].mLightViewProj);
        Vectormath::Matrix4 worldMatrix     = spotlightMatrix * pPerFrame->mCameraCurrViewProj;
        m_WireframeBox.Draw(pCmdLst1, &m_Wireframe, worldMatrix, vCenter, vRadius, vColor);
    }

    m_GPUTimer.GetTimeStamp(pCmdLst1, "Light frustums");
}

void SampleRenderer::DownsampleDepthBuffer(ID3D12GraphicsCommandList *pCmdLst1, State *pState) {
    UserMarker marker(pCmdLst1, "Downsample Depth");
    if (pState->bOptimizedDownsample || pState->m_ReflectionResolutionMultiplier == 1.0f)
        m_GBuffer.m_DepthBuffer.CreateSRV(0, &m_DepthBufferDescriptor);
    else
        m_ReflectionGBuffer.m_DepthBuffer.CreateSRV(0, &m_DepthBufferDescriptor);

    ID3D12DescriptorHeap *descriptorHeaps[] = {m_ResourceViewHeaps.GetCBV_SRV_UAVHeap()};
    pCmdLst1->SetDescriptorHeaps(1, descriptorHeaps);
    pCmdLst1->SetComputeRootSignature(m_DownsampleRootSignature);
    pCmdLst1->SetComputeRootDescriptorTable(0, m_DownsampleDescriptorTable);
    pCmdLst1->SetPipelineState(m_DownsamplePipelineState);

    // Each threadgroup works on 64x64 texels
    if (pState->bOptimizedDownsample || pState->m_ReflectionResolutionMultiplier == 1.0f)
        pCmdLst1->Dispatch((m_Width + 63) / 64, (m_Height + 63) / 64, 1);
    else
        pCmdLst1->Dispatch((m_ReflectionWidth + 63) / 64, (m_ReflectionHeight + 63) / 64, 1);

    m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample Depth");
}

void SampleRenderer::RenderScreenSpaceReflections(ID3D12GraphicsCommandList *pCmdLst1, per_frame *pPerFrame, State *pState) {
    HSR::ReflectionGBuffer rgbuffer{};
    rgbuffer.pDepth             = &m_ReflectionUAVGbuffer.Depth;
    rgbuffer.pAlbedo            = &m_ReflectionUAVGbuffer.Albedo;
    rgbuffer.pMotionVectors     = &m_ReflectionUAVGbuffer.MotionVectors;
    rgbuffer.pNormals           = &m_ReflectionUAVGbuffer.Normals;
    rgbuffer.pSpecularRoughness = &m_ReflectionUAVGbuffer.SpecularRoughness;
    m_hsr.Draw(pCmdLst1, &m_GBuffer.m_HDR, &rgbuffer, GetCurrentUAVHeap(), GetCurrentSamplerHeap(), pState);
    for (int i = 0; i < (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT; i++) {
        pState->hsr_timestamps[i] += (1000000.0 * static_cast<double>(m_hsr.GetTimestamp(i)) / m_GpuTicksPerSecond - pState->hsr_timestamps[i]) * 0.5;
    }

    m_GPUTimer.GetTimeStamp(pCmdLst1, "Hybrid Reflections");
}

void SampleRenderer::CopyHistorySurfaces(ID3D12GraphicsCommandList *pCmdLst1, State *pState) {
    UserMarker marker(pCmdLst1, "Copy History Normals and Roughness");
    // Keep copy of normal roughness buffer for next frame
    if (pState->bOptimizedDownsample) {
        CopyToTexture(pCmdLst1, m_ReflectionUAVGbuffer.Depth.GetResource(), m_DepthHistoryBuffer.GetResource(), m_ReflectionWidth, m_ReflectionHeight);
        CopyToTexture(pCmdLst1, m_ReflectionUAVGbuffer.Normals.GetResource(), m_NormalHistoryBuffer.GetResource(), m_ReflectionWidth, m_ReflectionHeight);
    } else {
        if (pState->m_ReflectionResolutionMultiplier != 1.0f) {
            CopyToTexture(pCmdLst1, m_ReflectionGBuffer.m_DepthBuffer.GetResource(), m_DepthHistoryBuffer.GetResource(), m_ReflectionWidth, m_ReflectionHeight);
            CopyToTexture(pCmdLst1, m_ReflectionGBuffer.m_NormalBuffer.GetResource(), m_NormalHistoryBuffer.GetResource(), m_ReflectionWidth, m_ReflectionHeight);
        } else {
            CopyToTexture(pCmdLst1, m_GBuffer.m_DepthBuffer.GetResource(), m_DepthHistoryBuffer.GetResource(), m_ReflectionWidth, m_ReflectionHeight);
            CopyToTexture(pCmdLst1, m_GBuffer.m_NormalBuffer.GetResource(), m_NormalHistoryBuffer.GetResource(), m_ReflectionWidth, m_ReflectionHeight);
        }
    }

    CopyToTexture(pCmdLst1, m_GBuffer.m_HDR.GetResource(), m_PrevHDR.GetResource(), m_Width, m_Height);
}

void SampleRenderer::DownsampleScene(ID3D12GraphicsCommandList *pCmdLst1) {
    UserMarker marker(pCmdLst1, "Downsample Scene");

    D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = {m_GBuffer.m_HDRRTV.GetCPU()};
    pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

    m_DownSample.Draw(pCmdLst1);
    // m_downSample.Gui();
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample Scene");
}

void SampleRenderer::RenderBloom(ID3D12GraphicsCommandList *pCmdLst1) {
    UserMarker marker(pCmdLst1, "Render Bloom");

    D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = {m_GBuffer.m_HDRRTV.GetCPU()};
    pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

    m_Bloom.Draw(pCmdLst1, &m_GBuffer.m_HDR);
    m_GPUTimer.GetTimeStamp(pCmdLst1, "Render Bloom");
}

void SampleRenderer::ApplyTonemapping(ID3D12GraphicsCommandList *pCmdLst2, State *pState, SwapChain *pSwapChain) {
    UserMarker marker(pCmdLst2, "Apply Tonemapping");

    pCmdLst2->RSSetViewports(1, &m_Viewport);
    pCmdLst2->RSSetScissorRects(1, &m_Scissor);
    pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), false, NULL);

    if (pState->bUseMagnifier)
        m_ToneMapping.Draw(pCmdLst2, &m_Magnifier.GetPassOutputSRV(), pState->exposure, pState->toneMapper);
    else
        m_ToneMapping.Draw(pCmdLst2, &m_GBuffer.m_HDRSRV, pState->exposure, pState->toneMapper);
    m_GPUTimer.GetTimeStamp(pCmdLst2, "Apply Tonemapping");
}

void SampleRenderer::RenderHUD(ID3D12GraphicsCommandList *pCmdLst2, SwapChain *pSwapChain) {
    UserMarker marker(pCmdLst2, "Render HUD");

    pCmdLst2->RSSetViewports(1, &m_Viewport);
    pCmdLst2->RSSetScissorRects(1, &m_Scissor);
    pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), false, NULL);

    m_ImGUI.Draw(pCmdLst2);

    m_GPUTimer.GetTimeStamp(pCmdLst2, "Render HUD");
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnRender(State *pState, SwapChain *pSwapChain) {
    StallFrame(pState->targetFrametime);
    BeginFrame();

    per_frame *pPerFrame = FillFrameConstants(pState);

    // command buffer calls
    //
    ID3D12GraphicsCommandList *pCmdLst1 = m_CommandListRing.GetNewCommandList();

    m_GPUTimer.GetTimeStamp(pCmdLst1, "Begin Frame");

    // TODO: Sort
    std::vector<RTGltfPbrPass::BatchList> OpaqueBatchList;
    std::vector<RTGltfPbrPass::BatchList> TransparentBatchList;

    // Update descriptor table
    m_frameID++;
    if (m_gltfPBR) {
        m_gltfPBR->BuildBatchLists(&OpaqueBatchList, &TransparentBatchList);
        ID3D12GraphicsCommandList5 *pCommandList5 = NULL;
        pCmdLst1->QueryInterface(&pCommandList5);
        if (pCommandList5) {
            m_gltfPBR->UpdateAccelerationStructures(pCommandList5, GetCurrentUAVHeap());
        }
        for (uint32_t i = 0; i < 32; i++) {
            m_shadowmaps[i].m_ShadowMap.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_SHADOW_MAP_BEGIN_SLOT + i, GetCurrentUAVHeap());
        }
        m_GPUTimer.GetTimeStamp(pCmdLst1, "UpdateAccelerationStructures");
    }

    {
        D3D12_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter         = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.MinLOD         = 0.0f;
        desc.MaxLOD         = D3D12_FLOAT32_MAX;
        desc.MipLODBias     = 0;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.MaxAnisotropy  = 1;
        m_pDevice->GetDevice()->CreateSampler(&desc, GetCurrentSamplerHeap()->GetCPU(GDT_CMP_SAMPLERS_HEAP_OFFSET + 0));
    }
    {
        D3D12_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter        = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU      = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.AddressV      = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.AddressW      = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        desc.MinLOD        = 0.0f;
        desc.MaxLOD        = D3D12_FLOAT32_MAX;
        desc.MipLODBias    = 0;
        desc.MaxAnisotropy = 1;
        m_pDevice->GetDevice()->CreateSampler(&desc, GetCurrentSamplerHeap()->GetCPU(GDT_SAMPLERS_HEAP_OFFSET + 0));
    }
    {
        D3D12_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter         = D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        desc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        desc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.BorderColor[0] = 0;
        desc.BorderColor[1] = 0;
        desc.BorderColor[2] = 0;
        desc.BorderColor[3] = 0;
        desc.MinLOD         = 0.0f;
        desc.MaxLOD         = D3D12_FLOAT32_MAX;
        desc.MipLODBias     = 0;
        desc.MaxAnisotropy  = 1;
        m_pDevice->GetDevice()->CreateSampler(&desc, GetCurrentSamplerHeap()->GetCPU(GDT_SAMPLERS_HEAP_OFFSET + 1));
    }
    {
        D3D12_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        desc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        desc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.BorderColor[0] = 0;
        desc.BorderColor[1] = 0;
        desc.BorderColor[2] = 0;
        desc.BorderColor[3] = 0;
        desc.MinLOD         = 0.0f;
        desc.MaxLOD         = D3D12_FLOAT32_MAX;
        desc.MipLODBias     = 0;
        desc.MaxAnisotropy  = 1;
        m_pDevice->GetDevice()->CreateSampler(&desc, GetCurrentSamplerHeap()->GetCPU(GDT_SAMPLERS_HEAP_OFFSET + GDT_SAMPLERS_WRAP_LINEAR_SAMPLER_SLOT));
    }
    {
        const Camera *      camera    = &pState->camera;
        Vectormath::Matrix4 view      = camera->GetView();
        Vectormath::Matrix4 proj      = camera->GetProjection();
        Vectormath::Matrix4 prev_view = camera->GetPrevView();

        pState->frameInfo.view     = Vectormath::transpose(view);
        pState->frameInfo.proj     = Vectormath::transpose(proj);
        pState->frameInfo.inv_proj = Vectormath::transpose(Vectormath::inverse(proj));
        pState->frameInfo.inv_view = Vectormath::transpose(Vectormath::inverse(view));
        if (pPerFrame) {
            pState->frameInfo.inv_view_proj  = Vectormath::transpose(pPerFrame->mInverseCameraCurrViewProj);
            pState->frameInfo.prev_view_proj = Vectormath::transpose(pPerFrame->mCameraPrevViewProj);
            pState->frameInfo.prev_view      = Vectormath::transpose(prev_view);
            memcpy(&pState->frameInfo.perFrame, pPerFrame, sizeof(*pPerFrame));
        }
        pState->frameInfo.frame_index                              = m_frame_index;
        pState->frameInfo.simulation_time                          = pState->bUpdateSimulation ? pState->time : pState->frameInfo.simulation_time;
        pState->frameInfo.max_traversal_intersections              = pState->maxTraversalIterations;
        pState->frameInfo.min_traversal_occupancy                  = pState->minTraversalOccupancy;
        pState->frameInfo.most_detailed_mip                        = pState->mostDetailedDepthHierarchyMipLevel;
        pState->frameInfo.depth_buffer_thickness                   = pState->depthBufferThickness;
        pState->frameInfo.samples_per_quad                         = pState->samplesPerQuad;
        pState->frameInfo.temporal_variance_guided_tracing_enabled = pState->bEnableVarianceGuidedTracing ? 1 : 0;
        pState->frameInfo.roughness_threshold                      = pState->roughnessThreshold;

        int width8                      = RoundedDivide(m_ReflectionWidth, 8u) * 8;
        int height8                     = RoundedDivide(m_ReflectionHeight, 8u) * 8;
        pState->frameInfo.x_to_u_factor = 1.0f / float(width8);
        pState->frameInfo.y_to_v_factor = 1.0f / float(height8);
    }
    D3D12_GPU_VIRTUAL_ADDRESS frame_info_cb = m_ConstantBufferRing.AllocConstantBuffer(sizeof(pState->frameInfo), &pState->frameInfo);
    {

        m_GBuffer.m_HDR.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_LIT_SCREEN_SLOT, GetCurrentUAVHeap());
        m_GBuffer.m_HDR.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_FULL_LIT_SCENE_SLOT, GetCurrentUAVHeap());

        m_DepthHierarchy.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_HIZ_SLOT, GetCurrentUAVHeap());
        if (pState->bOptimizedDownsample) {
            m_ReflectionUAVGbuffer.Depth.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_DEPTH_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.Albedo.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_ALBEDO_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.SpecularRoughness.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_ROUGHNESS_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.Normals.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_NORMAL_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.MotionVectors.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_MOTION_VECTOR_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.Depth.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_GBUFFER_DEPTH_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.Albedo.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_GBUFFER_ALBEDO_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.SpecularRoughness.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_GBUFFER_ROUGHNESS_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.Normals.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_GBUFFER_NORMAL_SLOT, GetCurrentUAVHeap());
            m_ReflectionUAVGbuffer.MotionVectors.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_MOTION_VECTOR_SLOT, GetCurrentUAVHeap());
        } else {
            if (pState->m_ReflectionResolutionMultiplier != 1.0f) {
                m_ReflectionGBuffer.m_DepthBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_DEPTH_SLOT, GetCurrentUAVHeap());
                m_ReflectionGBuffer.m_Diffuse.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_ALBEDO_SLOT, GetCurrentUAVHeap());
                m_ReflectionGBuffer.m_SpecularRoughness.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_ROUGHNESS_SLOT, GetCurrentUAVHeap());
                m_ReflectionGBuffer.m_NormalBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_NORMAL_SLOT, GetCurrentUAVHeap());
                m_ReflectionGBuffer.m_MotionVectors.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_MOTION_VECTOR_SLOT, GetCurrentUAVHeap());
            } else {
                m_GBuffer.m_DepthBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_DEPTH_SLOT, GetCurrentUAVHeap());
                m_GBuffer.m_Diffuse.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_ALBEDO_SLOT, GetCurrentUAVHeap());
                m_GBuffer.m_SpecularRoughness.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_ROUGHNESS_SLOT, GetCurrentUAVHeap());
                m_GBuffer.m_NormalBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_NORMAL_SLOT, GetCurrentUAVHeap());
                m_GBuffer.m_MotionVectors.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_MOTION_VECTOR_SLOT, GetCurrentUAVHeap());
            }
        }
        m_GBuffer.m_DepthBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_FULL_DEPTH_SLOT, GetCurrentUAVHeap());
        m_GBuffer.m_SpecularRoughness.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_FULL_ROUGHNESS_SLOT, GetCurrentUAVHeap());
        m_GBuffer.m_NormalBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_FULL_NORMAL_SLOT, GetCurrentUAVHeap());
        m_GBuffer.m_Diffuse.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_FULL_ALBEDO_SLOT, GetCurrentUAVHeap());
        m_GBuffer.m_MotionVectors.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_FULL_MOTION_VECTOR_SLOT, GetCurrentUAVHeap());
        m_NormalHistoryBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_NORMAL_HISTORY_SLOT, GetCurrentUAVHeap());
        m_PrevHDR.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_LIT_SCENE_HISTORY_SLOT, GetCurrentUAVHeap());
        m_DepthHistoryBuffer.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_GBUFFER_DEPTH_HISTORY_SLOT, GetCurrentUAVHeap());
        m_downsampleCounter.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_DOWNSAMPLE_COUNTER_SLOT, NULL, GetCurrentUAVHeap());
        m_BrdfLut.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_BRDF_LUT_SLOT, GetCurrentUAVHeap());
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbdesc{};
        cbdesc.BufferLocation = frame_info_cb;
        cbdesc.SizeInBytes    = (UINT)DivideRoundingUp(sizeof(pState->frameInfo), (size_t)256) * 256;
        m_pDevice->GetDevice()->CreateConstantBufferView(&cbdesc, GetCurrentUAVHeap()->GetCPU(GDT_FRAME_INFO_HEAP_OFFSET));
    }
    m_DecalRenderer.Bind(GetCurrentUAVHeap());
    if (pPerFrame) {
        UserMarker            marker(pCmdLst1, "Update Atmosphere");
        ID3D12DescriptorHeap *descriptorHeaps[] = {m_ResourceViewHeaps.GetCBV_SRV_UAVHeap(), m_ResourceViewHeaps.GetSamplerHeap()};
        pCmdLst1->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
        pCmdLst1->SetComputeRootSignature(m_pGlobalRootSignature);
        pCmdLst1->SetComputeRootDescriptorTable(1, GetCurrentSamplerHeap()->GetGPU());
        pCmdLst1->SetComputeRootDescriptorTable(0, GetCurrentUAVHeap()->GetGPU());
        m_AtmosphereRenderer.BarriersToUpdate(pCmdLst1);
        m_AtmosphereRenderer.Update(pCmdLst1, Vectormath::Vector3(0.0f, 0.0f, 0.0f), Vectormath::ToVec3(pPerFrame->lights[0].direction),
                                    {pState->SunLightIntensity * 2.4f, pState->SunLightIntensity * 2.2f, pState->SunLightIntensity * 2.0f});
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Update Atmosphere");
    }

    // Clears -----------------------------------------------------------------------
    //
    {
        UserMarker marker(pCmdLst1, "Clear shadow maps");
        if (m_gltfDepth && pPerFrame != NULL) {
            for (uint32_t i = 0; i < pPerFrame->lightCount; i++) {
                pCmdLst1->ClearDepthStencilView(m_shadowmaps[i].m_ShadowMapDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            }
        }
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear shadow map");
    }
    {
        UserMarker marker(pCmdLst1, "Clear render targets");
        float      clearValuesFloat[] = {0.0f, 0.0f, 0.0f, 0.0f};
        pCmdLst1->ClearRenderTargetView(m_ReflectionGBuffer.m_HDRRTV.GetCPU(), clearValuesFloat, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_GBuffer.m_DiffuseRTV.GetCPU(), clearValuesFloat, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_GBuffer.m_HDRRTV.GetCPU(), clearValuesFloat, 0, nullptr);
        float clearColor[]    = {0.0f, 0.0f, 0.0f, 0.0f};
        float clearColorOne[] = {1.0f, 1.0f, 1.0f, 1.0f};
        pCmdLst1->ClearRenderTargetView(m_ReflectionGBuffer.m_SpecularRoughnessRTV.GetCPU(), clearColorOne, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_GBuffer.m_MotionVectorsRTV.GetCPU(), clearColor, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_GBuffer.m_NormalBufferRTV.GetCPU(), clearColor, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_GBuffer.m_SpecularRoughnessRTV.GetCPU(), clearColorOne, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_ReflectionGBuffer.m_MotionVectorsRTV.GetCPU(), clearColor, 0, nullptr);
        pCmdLst1->ClearRenderTargetView(m_ReflectionGBuffer.m_NormalBufferRTV.GetCPU(), clearColor, 0, nullptr);

        m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear HDR");

        pCmdLst1->ClearDepthStencilView(m_ReflectionGBuffer.m_DepthBufferDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        pCmdLst1->ClearDepthStencilView(m_GBuffer.m_DepthBufferDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear depth");
    }

    {

        UINT clearValuesUint[4] = {0, 0, 0, 0};
        pCmdLst1->ClearUnorderedAccessViewUint(m_AtomicCounterUAVGPU.GetGPU(), m_AtomicCounterUAV.GetCPU(), m_AtomicCounter.GetResource(), clearValuesUint, 0,
                                               nullptr); // Set atomic counter to 0.
    }
    // Render to shadow map atlas for spot lights ------------------------------------------
    //
    if (m_gltfDepth && pPerFrame != NULL) {
        RenderSpotLights(pCmdLst1, pPerFrame);
    }
    {
        UserMarker marker(pCmdLst1, "Shadow map barriers (WRITE->READ)");
        for (uint32_t i = 0; i < 32; i++) {
            Barriers(pCmdLst1, {CD3DX12_RESOURCE_BARRIER::Transition(m_shadowmaps[i].m_ShadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)});
        }
    }

    // Render Scene to the HDR RT ------------------------------------------------
    //

    if (pPerFrame != NULL) {
        {
            pCmdLst1->RSSetViewports(1, &m_Viewport);
            pCmdLst1->RSSetScissorRects(1, &m_Scissor);

            D3D12_CPU_DESCRIPTOR_HANDLE rts[] = {m_GBuffer.m_HDRRTV.GetCPU(), m_GBuffer.m_MotionVectorsRTV.GetCPU(), m_GBuffer.m_NormalBufferRTV.GetCPU(),
                                                 m_GBuffer.m_DiffuseRTV.GetCPU(), m_GBuffer.m_SpecularRoughnessRTV.GetCPU()};
            pCmdLst1->OMSetRenderTargets(5, rts, false, &m_GBuffer.m_DepthBufferDSV.GetCPU());
            // Render scene to color buffer
            if (m_gltfPBR) {
                UserMarker marker(pCmdLst1, "RTGltfPbrPass");
                m_AtmosphereRenderer.BarriersForPixelResource(pCmdLst1);
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_shadowmaps[0].m_ShadowMapSRV, &OpaqueBatchList);
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_shadowmaps[0].m_ShadowMapSRV, &TransparentBatchList);
            }
            // Draw object bounding boxes
            if (m_gltfBBox) {
                if (pState->bDrawBoundingBoxes) {
                    m_gltfBBox->Draw(pCmdLst1, pPerFrame->mCameraCurrViewProj);
                    m_GPUTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
                }
            }

            // Draw light frustum
            if (pState->bDrawLightFrustum) {
                RenderLightFrustums(pCmdLst1, pPerFrame, pState);
            }

            {
                Barriers(pCmdLst1,
                         {
                             CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                             CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                         });
                UserMarker            marker(pCmdLst1, "Skydome");
                ID3D12DescriptorHeap *descriptorHeaps[] = {m_ResourceViewHeaps.GetCBV_SRV_UAVHeap(), m_ResourceViewHeaps.GetSamplerHeap()};
                pCmdLst1->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
                pCmdLst1->SetComputeRootSignature(m_pGlobalRootSignature);
                pCmdLst1->SetComputeRootDescriptorTable(1, GetCurrentSamplerHeap()->GetGPU());
                pCmdLst1->SetComputeRootDescriptorTable(0, GetCurrentUAVHeap()->GetGPU());
                m_AtmosphereRenderer.BarriersForNonPixelResource(pCmdLst1);
                m_AtmosphereRenderer.Draw(pCmdLst1, m_Width, m_Height);
                Barriers(pCmdLst1,
                         {
                             CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET),
                             CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE),
                         });
            }
        }
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Rendering scene");
        if (m_gltfPBR && pState->bRenderDecals) {
            UserMarker marker(pCmdLst1, "Decal Rendering");
            Barriers(pCmdLst1, {
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                               });
            m_DecalRenderer.Apply(pCmdLst1, m_Width, m_Height);
            Barriers(pCmdLst1, {
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                                        D3D12_RESOURCE_STATE_DEPTH_WRITE),
                               });
        }
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Rendering decals");
        // Render the same version in reflections resolution
        if (!pState->bOptimizedDownsample && pState->m_ReflectionResolutionMultiplier != 1.0f) {
            pCmdLst1->RSSetViewports(1, &m_ReflectionViewport);
            pCmdLst1->RSSetScissorRects(1, &m_ReflectionScissor);

            D3D12_CPU_DESCRIPTOR_HANDLE rts[] = {m_ReflectionGBuffer.m_HDRRTV.GetCPU(), m_ReflectionGBuffer.m_MotionVectorsRTV.GetCPU(),
                                                 m_ReflectionGBuffer.m_NormalBufferRTV.GetCPU(), m_ReflectionGBuffer.m_DiffuseRTV.GetCPU(),
                                                 m_ReflectionGBuffer.m_SpecularRoughnessRTV.GetCPU()};
            pCmdLst1->OMSetRenderTargets(5, rts, false, &m_ReflectionGBuffer.m_DepthBufferDSV.GetCPU());
            // Render scene to color buffer
            if (m_gltfPBR) {
                UserMarker marker(pCmdLst1, "RTGltfPbrPass");
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_shadowmaps[0].m_ShadowMapSRV, &OpaqueBatchList);
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_shadowmaps[0].m_ShadowMapSRV, &TransparentBatchList);
            }
        }
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Rendering scene in low res");
    }

    {
        UserMarker marker(pCmdLst1, "Shadow map Barriers (READ->WRITE)");
        for (uint32_t i = 0; i < 32; i++) {
            Barriers(pCmdLst1, {
                                   CD3DX12_RESOURCE_BARRIER::Transition(m_shadowmaps[i].m_ShadowMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                        D3D12_RESOURCE_STATE_DEPTH_WRITE),
                               });
        }
    }

    {
        UserMarker marker(pCmdLst1, "Depth buffer Barriers (WRITE->READ)");
        Barriers(pCmdLst1,
                 {
                     CD3DX12_RESOURCE_BARRIER::UAV(m_AtomicCounter.GetResource()),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                 });
    }

    // Downsample depth buffer
    if (pPerFrame != NULL) {
        DownsampleDepthBuffer(pCmdLst1, pState);
    }

    {
        UserMarker marker(pCmdLst1, "Misc Mixed Barriers");
        Barriers(
            pCmdLst1,
            {
                CD3DX12_RESOURCE_BARRIER::UAV(m_DepthHierarchy.GetResource()),
                CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHierarchy.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
                CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                     0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                     D3D12_RESOURCE_STATE_DEPTH_WRITE),
                CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                     0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
                CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
            });
    }

    // Stochastic Screen Space Reflections
    if (m_gltfPBR && pPerFrame != NULL) // Only draw reflections if we draw objects
    {
        m_AtmosphereRenderer.BarriersForNonPixelResource(pCmdLst1);
        {
            UserMarker marker(pCmdLst1, "Shadow map barriers (WRITE->READ)");
            for (uint32_t i = 0; i < 32; i++) {
                Barriers(pCmdLst1, {
                                       CD3DX12_RESOURCE_BARRIER::Transition(m_shadowmaps[i].m_ShadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                                   });
            }
        }
        Barriers(pCmdLst1,
                 {
                     CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_gltfPBR->m_infoTables.m_pSrcGeometryBufferResource,
                                                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                 });
        {
            UserMarker marker(pCmdLst1, "Screen Space Reflections");
            RenderScreenSpaceReflections(pCmdLst1, pPerFrame, pState);
        }
        Barriers(pCmdLst1,
                 {
                     CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                          D3D12_RESOURCE_STATE_DEPTH_WRITE),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
                     CD3DX12_RESOURCE_BARRIER::Transition(m_gltfPBR->m_infoTables.m_pSrcGeometryBufferResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                 });
        {
            UserMarker marker(pCmdLst1, "Shadow map barriers (READ->WRITE)");
            for (uint32_t i = 0; i < 32; i++) {
                Barriers(pCmdLst1, {
                                       CD3DX12_RESOURCE_BARRIER::Transition(m_shadowmaps[i].m_ShadowMap.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                                            D3D12_RESOURCE_STATE_DEPTH_WRITE),
                                   });
            }
        }
    }

    Barriers(pCmdLst1,
             {
                 CD3DX12_RESOURCE_BARRIER::Transition(m_hsr.GetDebugTexture()->GetResource(), D3D12_RESOURCE_STATE_COMMON,
                                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE), // Wait for reflection target to be written
                 CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHierarchy.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
             });

    Barriers(
        pCmdLst1,
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 0),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET, 0),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET, 0),
        });

    Barriers(
        pCmdLst1,
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionUAVGbuffer.Depth.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionUAVGbuffer.Normals.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_NormalHistoryBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(m_PrevHDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHistoryBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
        });

    CopyHistorySurfaces(pCmdLst1, pState); // Keep this frames results for next frame

    Barriers(
        pCmdLst1,
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionUAVGbuffer.Depth.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionUAVGbuffer.Normals.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_NormalHistoryBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_PrevHDR.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_DepthHistoryBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        });

    // Bloom, takes HDR as input and applies bloom to it.
    Barriers(
        pCmdLst1,
        {
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_hsr.GetDebugTexture()->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_SpecularRoughness.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_Diffuse.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        });

    if (pState->bDrawBloom) {
        DownsampleScene(pCmdLst1);
        RenderBloom(pCmdLst1);
    }

    // Submit command buffer
    ThrowIfFailed(pCmdLst1->Close());
    ID3D12CommandList *CmdListList1[] = {pCmdLst1};
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

    // Wait for swapchain (we are going to render to it)
    pSwapChain->WaitForSwapChain();
    ID3D12GraphicsCommandList *pCmdLst2 = m_CommandListRing.GetNewCommandList();

    Barriers(pCmdLst2, {
                           CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
                       });
    if (pState && pState->bTAA && m_GBuffer.m_HDR.GetResource()) m_taa.Draw(pCmdLst2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    pCmdLst2->RSSetViewports(1, &m_Viewport);
    pCmdLst2->RSSetScissorRects(1, &m_Scissor);
    m_Magnifier.Draw(pCmdLst2, pState->magnifierParams, m_GBuffer.m_HDRSRV);

    Barriers(pCmdLst2,
             {
                 CD3DX12_RESOURCE_BARRIER::Transition(m_Magnifier.GetPassOutputResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
             });

    // Tonemapping
    ApplyTonemapping(pCmdLst2, pState, pSwapChain);

    Barriers(pCmdLst2,
             {
                 CD3DX12_RESOURCE_BARRIER::Transition(m_ReflectionGBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                 CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                 CD3DX12_RESOURCE_BARRIER::Transition(m_Magnifier.GetPassOutputResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
             });

    // Render HUD
    RenderHUD(pCmdLst2, pSwapChain);

    if (pState->screenshotName.size()) {
        m_SaveTexture.CopyRenderTargetIntoStagingTexture(m_pDevice->GetDevice(), pCmdLst2, pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    // Transition swapchain into present mode
    Barriers(pCmdLst2, {CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)});

    m_GPUTimer.OnEndFrame();

    m_GPUTimer.CollectTimings(pCmdLst2);

    // Close & Submit the command list
    ThrowIfFailed(pCmdLst2->Close());

    ID3D12CommandList *CmdListList2[] = {pCmdLst2};
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);

    if (pState->screenshotName.size()) {
        m_SaveTexture.SaveStagingTextureAsJpeg(m_pDevice->GetDevice(), m_pDevice->GetGraphicsQueue(), pState->screenshotName.c_str());
        pState->screenshotName = "";
    }

    // Update previous camera matrices
    pState->camera.UpdatePreviousMatrices();
    if (pPerFrame) {
        m_prev_view_projection = pPerFrame->mCameraPrevViewProj;
    }
    m_frame_index++;
}

void SampleRenderer::Recompile() { m_hsr.Recompile(); }

void SampleRenderer::CreateDepthDownsamplePipeline() {
    HRESULT hr;

    static constexpr uint32_t numRootParameters = 1;
    CD3DX12_ROOT_PARAMETER    root[numRootParameters];

    CD3DX12_DESCRIPTOR_RANGE ranges[3] = {};
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 13, 0);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 13);

    root[0].InitAsDescriptorTable(3, ranges);

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters             = numRootParameters;
    rsDesc.pParameters               = root;
    rsDesc.NumStaticSamplers         = 0;
    rsDesc.pStaticSamplers           = nullptr;

    ID3DBlob *rs, *rsError;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &rsError);
    if (FAILED(hr)) {
        Trace("Failed to serialize root signature for downsampling pipeline.\n");
        ThrowIfFailed(hr);
    }

    hr = m_pDevice->GetDevice()->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), IID_PPV_ARGS(&m_DownsampleRootSignature));
    if (FAILED(hr)) {
        Trace("Failed to create root signature for downsampling pipeline.\n");
        ThrowIfFailed(hr);
    }

    hr = m_DownsampleRootSignature->SetName(L"Depth Downsample RootSignature");
    if (FAILED(hr)) {
        Trace("Failed to name root signature for downsampling pipeline.\n");
        ThrowIfFailed(hr);
    }

    D3D12_SHADER_BYTECODE shaderByteCode = {};
    DefineList            defines;
    CompileShaderFromFile("DepthDownsample.hlsl", &defines, "main", "-T cs_6_0", &shaderByteCode);

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature                    = m_DownsampleRootSignature;
    desc.CS                                = shaderByteCode;

    hr = m_pDevice->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_DownsamplePipelineState));
    if (FAILED(hr)) {
        Trace("Failed to create downsampling pipeline.\n");
        ThrowIfFailed(hr);
    }

    hr = m_DownsamplePipelineState->SetName(L"Depth Downsample Pipeline");
    if (FAILED(hr)) {
        Trace("Failed to name downsampling pipeline.\n");
        ThrowIfFailed(hr);
    }

    rs->Release();
}