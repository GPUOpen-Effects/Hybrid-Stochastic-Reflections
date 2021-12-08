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

#include "Base\ShaderCompilerHelper.h"
#include "HSR.h"
#include "Utils.h"

#define A_CPU
#include "../../../../ffx-fsr/ffx-fsr/ffx_a.h"

#include "../../../../ffx-fsr/ffx-fsr/ffx_fsr1.h"

namespace _1spp {

#include "../../../samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_256spp.cpp"

}

/*
        The available blue noise sampler with 2spp sampling mode.
*/
struct {
    std::int32_t const (&sobol_buffer_)[256 * 256];
    std::int32_t const (&ranking_tile_buffer_)[128 * 128 * 8];
    std::int32_t const (&scrambling_tile_buffer_)[128 * 128 * 8];
} const g_blue_noise_sampler_state = {_1spp::sobol_256spp_256d, _1spp::rankingTile, _1spp::scramblingTile};

using namespace CAULDRON_DX12;
namespace HSR_SAMPLE_DX12 {
HSR::HSR() {
    m_pDevice             = nullptr;
    m_pConstantBufferRing = nullptr;
    m_pCpuVisibleHeap     = nullptr;
    m_pResourceViewHeaps  = nullptr;
    m_pUploadHeap         = nullptr;
    m_pCommandSignature   = nullptr;

    m_bufferIndex                  = 0;
    m_frameCountBeforeReuse        = 0;
    m_isPerformanceCountersEnabled = false;
    m_timestampFrameIndex          = 0;

    m_pTimestampQueryBuffer = nullptr;
    m_pTimestampQueryHeap   = nullptr;
}

void HSR::OnCreate(Device *pDevice, ID3D12RootSignature *pGlobalRootSignature, StaticResourceViewHeap &cpuVisibleHeap, ResourceViewHeaps &resourceHeap, UploadHeap &uploadHeap,
                   DynamicBufferRing &constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters) {
    m_pGlobalRootSignature         = pGlobalRootSignature;
    m_pDevice                      = pDevice;
    m_pConstantBufferRing          = &constantBufferRing;
    m_pCpuVisibleHeap              = &cpuVisibleHeap;
    m_pResourceViewHeaps           = &resourceHeap;
    m_pUploadHeap                  = &uploadHeap;
    m_frameCountBeforeReuse        = frameCountBeforeReuse;
    m_isPerformanceCountersEnabled = enablePerformanceCounters;

    m_uploadHeapBuffers.OnCreate(pDevice, 1024 * 1024);
    CreateResources();
    SetupPSOTable(0);
    SetupPerformanceCounters();
}

void HSR::OnCreateWindowSizeDependentResources(const HSRCreationInfo &input) {
    assert(input.outputWidth > 0);
    assert(input.outputHeight > 0);
    m_input = input;
    CreateWindowSizeDependentResources();
}

void HSR::OnDestroy() {
    m_uploadHeapBuffers.OnDestroy();

    for (auto &item : m_psoTables) item.second.OnDestroy();
    m_psoTables.clear();

    m_rayCounter.OnDestroy();
    m_randomNumberImage.OnDestroy();
    m_intersectionPassIndirectArgs.OnDestroy();
    m_blueNoiseSampler.OnDestroy();
    m_pPrimaryRayTracingPSO->Release();
    m_pPrimaryRayTracingPSO = NULL;

    if (m_pCommandSignature) {
        m_pCommandSignature->Release();
        m_pCommandSignature = NULL;
    }
    if (m_pTimestampQueryHeap) {
        m_pTimestampQueryHeap->Release();
        m_pTimestampQueryHeap = NULL;
    }
    if (m_pTimestampQueryBuffer) {
        m_pTimestampQueryBuffer->Release();
        m_pTimestampQueryBuffer = NULL;
    }
}

void HSR::OnDestroyWindowSizeDependentResources() {
    m_rayList.OnDestroy();
    m_hwRayList.OnDestroy();
    m_GBufferList.OnDestroy();
    m_denoiseTileList.OnDestroy();
    m_roughnessTexture[0].OnDestroy();
    m_roughnessTexture[1].OnDestroy();
    m_debugImage.OnDestroy();
    m_counterImage[0].OnDestroy();
    m_counterImage[1].OnDestroy();
    m_radianceAvg[0].OnDestroy();
    m_radianceAvg[1].OnDestroy();
    if (m_pMetricsUploadBuffer) {
        m_pMetricsUploadBuffer->Release();
        m_pMetricsUploadBuffer = NULL;
    }
    for (int i = 0; i < 3; i++) m_radianceBuffer[i].OnDestroy();
    for (int i = 0; i < 4; i++) m_radianceAux[i].OnDestroy();
    m_metricsUAVBuffer.OnDestroy();
}

static void Barriers(ID3D12GraphicsCommandList *pCmdLst, const std::vector<D3D12_RESOURCE_BARRIER> &barriers) {
    pCmdLst->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void HSR::Draw(ID3D12GraphicsCommandList *pCommandList, Texture *pHDROut, ReflectionGBuffer *pLowResGbuffer, CBV_SRV_UAV *pGlobalTable, SAMPLER *pGlobalSamplers, State *pState) {
    UserMarker marker(pCommandList, "FidelityFX HSR");

    std::unordered_map<ID3D12Resource *, D3D12_RESOURCE_STATES> default_states = {
        {m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {m_radianceBuffer[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        {m_rayCounter.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {m_GBufferList.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS},
        {pHDROut->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
    };

    std::unordered_map<ID3D12Resource *, D3D12_RESOURCE_STATES> current_states = {};

    auto barrier = [&](ID3D12Resource *pRes, D3D12_RESOURCE_STATES new_state) {
        D3D12_RESOURCE_STATES old_state = {};
        if (current_states.find(pRes) != current_states.end()) {
            old_state = current_states.find(pRes)->second;
        } else if (default_states.find(pRes) != default_states.end()) {
            old_state = default_states.find(pRes)->second;
        } else {
            old_state = D3D12_RESOURCE_STATE_COMMON;
        }
        if (old_state == new_state) {
            Barriers(pCommandList, {
                                       CD3DX12_RESOURCE_BARRIER::UAV(pRes),
                                   });
        } else {
            Barriers(pCommandList, {
                                       CD3DX12_RESOURCE_BARRIER::Transition(pRes, old_state, new_state),
                                   });
        }
        current_states[pRes] = new_state;
    };

    auto resetStates = [&]() {
        for (auto item : current_states)
            if (default_states.find(item.first) != default_states.end())
                barrier(item.first, default_states.find(item.first)->second);
            else
                barrier(item.first, D3D12_RESOURCE_STATE_COMMON);
    };

    QueryTimestamps(pCommandList);

    PSOTable psoTable = m_psoTables[                                                                           //
        ((pState->frameInfo.hsr_mask & HSR_FLAGS_SHOW_DEBUG_TARGET) ? 1 : 0)                                   //
        | ((pState->frameInfo.hsr_mask & HSR_FLAGS_RESOLVE_TRANSPARENT) ? 2 : 0)                               //
        | ((pState->frameInfo.hsr_mask & HSR_FLAGS_SHADING_USE_SCREEN) ? 4 : 0)                                //
        | ((m_input.inputWidth != m_input.outputWidth || m_input.inputHeight != m_input.outputHeight) ? 8 : 0) //
    ];

    struct PushConstants {
        uint32_t flags;
        uint32_t depth_mip_bias;
    } pc{};
    if (pState->bOptimizedDownsample) pc.depth_mip_bias = 1;

    auto putTimestamp = [this](ID3D12GraphicsCommandList *pCommandList, HSRTimestampQuery slot) {
        if (m_isPerformanceCountersEnabled) {
            auto &timestamp_queries = m_timestampQueries[m_timestampFrameIndex];
            pCommandList->EndQuery(m_pTimestampQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, GetTimestampQueryIndex());
            timestamp_queries.push_back(slot);
        }
    };
    // Set up global descriptor table
    {
        BlueNoiseSamplerD3D12 &sampler = m_blueNoiseSampler;

        m_roughnessTexture[m_bufferIndex].CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_EXTRACTED_ROUGHNESS_SLOT, pGlobalTable);
        m_roughnessTexture[(m_bufferIndex + 1) % 2].CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_EXTRACTED_ROUGHNESS_HISTORY_SLOT, pGlobalTable);
        m_roughnessTexture[m_bufferIndex].CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_EXTRACTED_ROUGHNESS_SLOT, pGlobalTable);
        sampler.sobolBuffer.CreateRawUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_SOBOL_SLOT, pGlobalTable);
        sampler.rankingTileBuffer.CreateRawUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_RANKING_TILE_SLOT, pGlobalTable);
        sampler.scramblingTileBuffer.CreateRawUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_SCRAMBLING_TILE_SLOT, pGlobalTable);
        m_rayList.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_RAY_LIST_SLOT, NULL, pGlobalTable);
        m_hwRayList.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_HW_RAY_LIST_SLOT, NULL, pGlobalTable);
        m_denoiseTileList.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_DENOISE_TILE_LIST_SLOT, NULL, pGlobalTable);
        m_GBufferList.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_RAY_GBUFFER_LIST_SLOT, NULL, pGlobalTable);
        m_counterImage[(m_bufferIndex + 0) % 2].CreateUAV(GDT_RW_UTEXTURES_HEAP_OFFSET + GDT_RW_UTEXTURES_HIT_COUNTER_SLOT, pGlobalTable);
        m_counterImage[(m_bufferIndex + 1) % 2].CreateSRV(GDT_UTEXTURES_HEAP_OFFSET + GDT_UTEXTURES_HIT_COUNTER_HISTORY_SLOT, pGlobalTable);
        m_radianceAvg[(m_bufferIndex + 0) % 2].CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_RADIANCE_MIP_SLOT, pGlobalTable);
        m_radianceAvg[(m_bufferIndex + 1) % 2].CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_RADIANCE_MIP_PREV_SLOT, pGlobalTable);
        m_radianceAvg[(m_bufferIndex + 0) % 2].CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_RADIANCE_AVG_SLOT, pGlobalTable);

        m_debugImage.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_DEBUG_SLOT, pGlobalTable);
        m_debugImage.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_DEBUG_SLOT, pGlobalTable);
        m_rayCounter.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_RAY_COUNTER_SLOT, NULL, pGlobalTable);
        m_metricsUAVBuffer.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_METRICS_SLOT, NULL, pGlobalTable);
        m_intersectionPassIndirectArgs.CreateRawBufferUAV(GDT_BUFFERS_HEAP_OFFSET + GDT_BUFFERS_INDIRECT_ARGS_SLOT, NULL, pGlobalTable);

        m_radianceBuffer[(m_bufferIndex + 0) % 2].CreateUAV(GDT_RW_TEXTURESFP16X3_HEAP_OFFSET + GDT_RW_TEXTURESFP16X3_RADIANCE_0_SLOT + 0, pGlobalTable);
        m_radianceBuffer[(m_bufferIndex + 0) % 2].CreateSRV(GDT_TEXTURESFP16X3_HEAP_OFFSET + GDT_TEXTURESFP16X3_RADIANCE_0_SLOT, pGlobalTable);
        m_radianceBuffer[(m_bufferIndex + 1) % 2].CreateUAV(GDT_RW_TEXTURESFP16X3_HEAP_OFFSET + GDT_RW_TEXTURESFP16X3_RADIANCE_1_SLOT, pGlobalTable);
        m_radianceBuffer[(m_bufferIndex + 1) % 2].CreateSRV(GDT_TEXTURESFP16X3_HEAP_OFFSET + GDT_TEXTURESFP16X3_RADIANCE_1_SLOT, pGlobalTable);
        m_randomNumberImage.CreateUAV(GDT_RW_TEXTURES_HEAP_OFFSET + GDT_RW_TEXTURES_RANDOM_NUMBER_IMAGE_SLOT, pGlobalTable);
        m_randomNumberImage.CreateSRV(GDT_TEXTURES_HEAP_OFFSET + GDT_TEXTURES_RANDOM_NUMBER_IMAGE_SLOT, pGlobalTable);

        m_radianceAux[(m_bufferIndex + 0) % 2].CreateUAV(GDT_RW_TEXTURESFP16_HEAP_OFFSET + GDT_RW_TEXTURESFP16_RADIANCE_VARIANCE_0_SLOT, pGlobalTable);
        m_radianceAux[(m_bufferIndex + 1) % 2].CreateUAV(GDT_RW_TEXTURESFP16_HEAP_OFFSET + GDT_RW_TEXTURESFP16_RADIANCE_VARIANCE_1_SLOT, pGlobalTable);
        m_radianceAux[(m_bufferIndex + 0) % 2].CreateSRV(GDT_TEXTURESFP16_HEAP_OFFSET + GDT_TEXTURESFP16_RADIANCE_VARIANCE_0_SLOT, pGlobalTable);
        m_radianceAux[(m_bufferIndex + 1) % 2].CreateSRV(GDT_TEXTURESFP16_HEAP_OFFSET + GDT_TEXTURESFP16_RADIANCE_VARIANCE_1_SLOT, pGlobalTable);

        m_radianceAux[2 + (m_bufferIndex + 0) % 2].CreateUAV(GDT_RW_TEXTURESFP16_HEAP_OFFSET + GDT_RW_TEXTURESFP16_RADIANCE_NUM_SAMPLES_0_SLOT, pGlobalTable);
        m_radianceAux[2 + (m_bufferIndex + 1) % 2].CreateUAV(GDT_RW_TEXTURESFP16_HEAP_OFFSET + GDT_RW_TEXTURESFP16_RADIANCE_NUM_SAMPLES_1_SLOT, pGlobalTable);
        m_radianceAux[2 + (m_bufferIndex + 0) % 2].CreateSRV(GDT_TEXTURESFP16_HEAP_OFFSET + GDT_TEXTURESFP16_RADIANCE_NUM_SAMPLES_0_SLOT, pGlobalTable);
        m_radianceAux[2 + (m_bufferIndex + 1) % 2].CreateSRV(GDT_TEXTURESFP16_HEAP_OFFSET + GDT_TEXTURESFP16_RADIANCE_NUM_SAMPLES_1_SLOT, pGlobalTable);

        m_radianceBuffer[2].CreateUAV(GDT_RW_TEXTURESFP16X3_HEAP_OFFSET + GDT_RW_TEXTURESFP16X3_RADIANCE_REPROJECTED_SLOT, pGlobalTable);
        m_radianceBuffer[2].CreateSRV(GDT_TEXTURESFP16X3_HEAP_OFFSET + GDT_TEXTURESFP16X3_RADIANCE_REPROJECTED_SLOT, pGlobalTable);
    }

    // Render
    ID3D12DescriptorHeap *descriptorHeaps[] = {m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap()};
    pCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    pCommandList->SetComputeRootSignature(m_pGlobalRootSignature);
    pCommandList->SetComputeRootDescriptorTable(1, pGlobalSamplers->GetGPU());
    pCommandList->SetComputeRootDescriptorTable(0, pGlobalTable->GetGPU());
    pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);

    if (pState->bOptimizedDownsample) {
        barrier(pLowResGbuffer->pAlbedo->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(pLowResGbuffer->pDepth->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(pLowResGbuffer->pNormals->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(pLowResGbuffer->pMotionVectors->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(pLowResGbuffer->pSpecularRoughness->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        UserMarker marker(pCommandList, "Downsample GBuffer");

        pCommandList->SetPipelineState(psoTable.m_pDownsampleGbuffer);
        uint32_t dim_x = RoundedDivide(m_input.outputWidth, 8u);
        uint32_t dim_y = RoundedDivide(m_input.outputHeight, 8u);
        pCommandList->Dispatch(dim_x, dim_y, 1);
        barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(pLowResGbuffer->pAlbedo->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(pLowResGbuffer->pDepth->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(pLowResGbuffer->pNormals->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(pLowResGbuffer->pMotionVectors->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(pLowResGbuffer->pSpecularRoughness->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_DOWNSAMPLE_GBUFFER);
    }

    bool render_primary = (pState->frameInfo.hsr_mask & HSR_FLAGS_VISUALIZE_PRIMARY_RAYS) && (pState->frameInfo.hsr_mask & HSR_FLAGS_SHOW_DEBUG_TARGET);
    if (render_primary) {
        pCommandList->SetPipelineState(m_pPrimaryRayTracingPSO);
        pCommandList->Dispatch(DivideRoundingUp(m_input.inputWidth, 8u), DivideRoundingUp(m_input.inputHeight, 8u), 1);
    } else {

        // Ensure that the ray list is in UA state
        barrier(m_counterImage[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(m_counterImage[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(m_roughnessTexture[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(m_radianceBuffer[2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        {
            // For clear
            barrier(m_radianceBuffer[0].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barrier(m_radianceBuffer[1].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barrier(m_randomNumberImage.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            UserMarker marker(pCommandList, "ClassifyTiles");

            pCommandList->SetPipelineState(psoTable.m_pClassifyTiles);
            uint32_t dim_x = RoundedDivide(m_input.outputWidth, 8u);

            uint32_t dim_y = RoundedDivide(m_input.outputHeight, 8u);

            barrier(m_radianceAux[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pCommandList->Dispatch(dim_x, dim_y, 1);
            barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_TILE_CLASSIFICATION);
        }

        // Ensure that the tile classification pass finished
        barrier(m_metricsUAVBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(m_rayCounter.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(m_intersectionPassIndirectArgs.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            UserMarker marker(pCommandList, "PrepareIndirectArgs");
            pCommandList->SetPipelineState(psoTable.m_pPrepareIndirectSW);
            pCommandList->Dispatch(1, 1, 1);
        }
        barrier(m_roughnessTexture[m_bufferIndex].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(m_roughnessTexture[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(m_rayCounter.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(m_intersectionPassIndirectArgs.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        barrier(m_randomNumberImage.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        {
            UserMarker marker(pCommandList, "Intersection pass");
            barrier(m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            barrier(m_radianceAux[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            if (pState->frameInfo.hsr_mask & HSR_FLAGS_USE_SCREEN_SPACE) {
                pCommandList->SetPipelineState(psoTable.m_pHybridPSODeferred);
                pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), INDIRECT_ARGS_SW_OFFSET, nullptr, 0);
                putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW);
            }
            if (pState->frameInfo.hsr_mask & HSR_FLAGS_USE_RAY_TRACING) {
                barrier(m_rayCounter.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                barrier(m_intersectionPassIndirectArgs.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                pCommandList->SetPipelineState(psoTable.m_pPrepareIndirect);
                pCommandList->Dispatch(1, 1, 1);

                barrier(m_intersectionPassIndirectArgs.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

                pCommandList->SetPipelineState(psoTable.m_pRTPSODeferred);
                pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);
                pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), INDIRECT_ARGS_HW_OFFSET, nullptr, 0);
                barrier(m_GBufferList.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                pCommandList->SetPipelineState(psoTable.m_pDeferredShadeRays);
                pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), INDIRECT_ARGS_HW_OFFSET, nullptr, 0);
                putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW);
            }
        }
        if (pState->frameInfo.hsr_mask & HSR_FLAGS_SHOW_INTERSECTION) {
            barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (pState->bClearAccumulator) pc.flags = 1;
            pState->bClearAccumulator = false;
            barrier(m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            UserMarker marker(pCommandList, "Accumulate");
            pCommandList->SetPipelineState(psoTable.m_pAccumulate);
            uint32_t dim_x = RoundedDivide(m_input.outputWidth, 8u);
            uint32_t dim_y = RoundedDivide(m_input.outputHeight, 8u);
            pCommandList->Dispatch(dim_x, dim_y, 1);
            barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        } else {
            {
                UserMarker marker(pCommandList, "FFX DNSR Reproject pass");

                barrier(m_radianceAvg[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                barrier(m_radianceAvg[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                barrier(m_radianceBuffer[2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                barrier(m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceBuffer[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                barrier(m_radianceAux[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceAux[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                barrier(m_radianceAux[2 + (m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceAux[2 + (m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                pCommandList->SetPipelineState(psoTable.m_pReproject);
                pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), INDIRECT_ARGS_DENOISE_OFFSET, nullptr, 0);
                putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_DENOISING_REPROJECT);
            }
            barrier(m_radianceAvg[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            {

                UserMarker marker(pCommandList, "FFX DNSR Prefiltering");
                barrier(m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceBuffer[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                barrier(m_radianceAux[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceAux[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                pCommandList->SetPipelineState(psoTable.m_pPrefilter);
                pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), INDIRECT_ARGS_DENOISE_OFFSET, nullptr, 0);
                putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_DENOISING_NEIGHBOR_X16);
            }

            {
                UserMarker marker(pCommandList, "FFX DNSR Temporal");
                barrier(m_radianceBuffer[2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceBuffer[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                barrier(m_radianceAux[(m_bufferIndex + 1) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_radianceAux[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                barrier(m_radianceAux[2 + (m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                pCommandList->SetPipelineState(psoTable.m_pResolveTemporal);
                pCommandList->ExecuteIndirect(m_pCommandSignature, 1, m_intersectionPassIndirectArgs.GetResource(), INDIRECT_ARGS_DENOISE_OFFSET, nullptr, 0);
                putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_DENOISING_TEMPORAL);
            }
        }
    }
    {
        UserMarker marker(pCommandList, "FFX Apply Reflections");
        struct PushConstants {
            hlsl::uint easu_const0[4];
            hlsl::uint easu_const1[4];
            hlsl::uint easu_const2[4];
            hlsl::uint easu_const3[4];
        } pc;
        FsrEasuCon(pc.easu_const0, pc.easu_const1, pc.easu_const2, pc.easu_const3, //
                   (AF1)m_input.outputWidth, (AF1)m_input.outputHeight,            //
                   (AF1)m_input.outputWidth, (AF1)m_input.outputHeight,            //
                   (AF1)m_input.inputWidth, (AF1)m_input.inputHeight);
        barrier(m_radianceBuffer[(m_bufferIndex + 0) % 2].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pCommandList->SetPipelineState(psoTable.m_pApplyReflections);
        pCommandList->SetComputeRoot32BitConstants(2, sizeof(pc) / 4, &pc, 0);
        pCommandList->Dispatch(DivideRoundingUp(m_input.inputWidth, 8u), DivideRoundingUp(m_input.inputHeight, 8u), 1);
        putTimestamp(pCommandList, HSRTimestampQuery::TIMESTAMP_QUERY_DENOISING_APPLY);
        barrier(pHDROut->GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    {
        pCommandList->SetPipelineState(psoTable.m_pResetDownsampleCounter);
        pCommandList->Dispatch(1, 1, 1);
    }
    barrier(m_metricsUAVBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    pCommandList->CopyResource(m_pMetricsUploadBuffer, m_metricsUAVBuffer.GetResource());
    barrier(m_metricsUAVBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    {
        int32_t numSWRays = m_pMetricsMap[0];
        int32_t numHWRays = m_pMetricsMap[2];
        int32_t numHYRays = m_pMetricsMap[2] - m_pMetricsMap[1];
        pState->m_numSWRays += (double(numSWRays) - pState->m_numSWRays) * 0.1;
        pState->m_numHWRays += (double(numHWRays) - pState->m_numHWRays) * 0.1;
        pState->m_numHYRays += (double(numHYRays) - pState->m_numHYRays) * 0.1;
    }

    resetStates();

    // Resolve the timestamp query data
    if (m_isPerformanceCountersEnabled) {
        auto const start_index = m_timestampFrameIndex * (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT;

        pCommandList->ResolveQueryData(m_pTimestampQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, start_index, static_cast<UINT>(m_timestampQueries[m_timestampFrameIndex].size()),
                                       m_pTimestampQueryBuffer, start_index * sizeof(std::uint64_t));

        m_timestampFrameIndex = (m_timestampFrameIndex + 1u) % m_frameCountBeforeReuse;
    }
    m_bufferIndex = (m_bufferIndex + 1) % 2;
}

void HSR::Recompile() {
    m_pDevice->GPUFlush();
    SetupPSOTable(0);
}

void HSR::CreateResources() {
    uint32_t elementSize = 4;
    //==============================Create Tile Classification-related buffers============================================
    {
        m_rayCounter.InitBuffer(m_pDevice, "HSR - Ray Counter", &CD3DX12_RESOURCE_DESC::Buffer(2 * 4 * (3 + 1), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    //==============================Create PrepareIndirectArgs-related buffers============================================
    {
        m_intersectionPassIndirectArgs.InitBuffer(m_pDevice, "HSR - Intersect Indirect Args",
                                                  &CD3DX12_RESOURCE_DESC::Buffer(3 * 4 * (3 + 1), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize,
                                                  D3D12_RESOURCE_STATE_COMMON);
    }
    //==============================Command Signature==========================================
    {
        D3D12_INDIRECT_ARGUMENT_DESC dispatch = {};
        dispatch.Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC desc = {};
        desc.ByteStride                   = sizeof(D3D12_DISPATCH_ARGUMENTS);
        desc.NodeMask                     = 0;
        desc.NumArgumentDescs             = 1;
        desc.pArgumentDescs               = &dispatch;

        ThrowIfFailed(m_pDevice->GetDevice()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&m_pCommandSignature)));
    }
    //==============================Blue Noise buffers============================================
    {
        auto const &           sampler_state = g_blue_noise_sampler_state;
        BlueNoiseSamplerD3D12 &sampler       = m_blueNoiseSampler;
        sampler.sobolBuffer.InitFromMem(m_pDevice, "HSR - Sobol Buffer", &m_uploadHeapBuffers, &sampler_state.sobol_buffer_, _countof(sampler_state.sobol_buffer_),
                                        sizeof(std::int32_t));
        sampler.rankingTileBuffer.InitFromMem(m_pDevice, "HSR - Ranking Tile Buffer", &m_uploadHeapBuffers, &sampler_state.ranking_tile_buffer_,
                                              _countof(sampler_state.ranking_tile_buffer_), sizeof(std::int32_t));
        sampler.scramblingTileBuffer.InitFromMem(m_pDevice, "HSR - Scrambling Tile Buffer", &m_uploadHeapBuffers, &sampler_state.scrambling_tile_buffer_,
                                                 _countof(sampler_state.scrambling_tile_buffer_), sizeof(std::int32_t));
        m_uploadHeapBuffers.FlushAndFinish();
    }
}

void HSR::CreateWindowSizeDependentResources() {
    int    width     = m_input.outputWidth;
    int    height    = m_input.outputHeight;
    int    width8    = RoundedDivide(m_input.outputWidth, 8u);
    int    height8   = RoundedDivide(m_input.outputHeight, 8u);
    UINT64 num_tiles = (UINT64)(width8 * height8);
    //===================================Create Output Buffer============================================
    {
        CD3DX12_RESOURCE_DESC reflDesc = // DXGI_FORMAT_R11G11B10_FLOAT or DXGI_FORMAT_R16G16B16A16_FLOAT
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, m_input.outputWidth, m_input.outputHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_radianceBuffer[0].Init(m_pDevice, "Radiance Result 0", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
        m_radianceBuffer[1].Init(m_pDevice, "Radiance Result 1", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
        m_radianceBuffer[2].Init(m_pDevice, "Radiance Result Reprojected", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    }
    {
        CD3DX12_RESOURCE_DESC reflDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8_UNORM, 128, 128, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_randomNumberImage.Init(m_pDevice, "Random Number Image", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    }
    {
        CD3DX12_RESOURCE_DESC reflDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_UINT, (m_input.outputWidth + 7) / 8, (m_input.outputHeight + 7) / 8, 1, 1, 1, 0,
                                                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_counterImage[0].Init(m_pDevice, "HSR - Hit Counter Image 0", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
        m_counterImage[1].Init(m_pDevice, "HSR - Hit Counter Image 1", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    }
    {
        CD3DX12_RESOURCE_DESC reflDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width8, height8, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_radianceAvg[0].Init(m_pDevice, "HSR - Radiance Average 0", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
        m_radianceAvg[1].Init(m_pDevice, "HSR - Radiance Average 1", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    }

    uint32_t elementSize = 4;
    //==============================Create Tile Classification-related buffers============================================
    {
        UINT64 num_pixels = (UINT64)m_input.outputWidth * m_input.outputHeight;
        m_rayList.InitBuffer(m_pDevice, "HSR - Ray List", &CD3DX12_RESOURCE_DESC::Buffer(num_pixels * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize,
                             D3D12_RESOURCE_STATE_COMMON);
        m_hwRayList.InitBuffer(m_pDevice, "HSR - HW Ray List", &CD3DX12_RESOURCE_DESC::Buffer(num_pixels * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize,
                               D3D12_RESOURCE_STATE_COMMON);
        m_denoiseTileList.InitBuffer(m_pDevice, "HSR - Denoise Tile List", &CD3DX12_RESOURCE_DESC::Buffer(num_tiles * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                     elementSize, D3D12_RESOURCE_STATE_COMMON);
        size_t gbuffer_size = 12;
        m_GBufferList.InitBuffer(m_pDevice, "HSR - Ray GBuffer List", &CD3DX12_RESOURCE_DESC::Buffer(width * height * gbuffer_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                 elementSize, D3D12_RESOURCE_STATE_COMMON);
    }
    {
        CD3DX12_RESOURCE_DESC reflDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_radianceAux[0].Init(m_pDevice, "Reflection AUX 0", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr); // Radiance variance and ray length.
        m_radianceAux[1].Init(m_pDevice, "Reflection AUX 1", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr); // Radiance variance and ray length.
        m_radianceAux[2].Init(m_pDevice, "Reflection AUX 2", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr); // Sample counter
        m_radianceAux[3].Init(m_pDevice, "Reflection AUX 3", &reflDesc, D3D12_RESOURCE_STATE_COMMON, nullptr); // Sample counter
    }
    //==============================Create denoising-related resources==============================
    {
        CD3DX12_RESOURCE_DESC roughnessTexture_Desc =
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, m_input.outputWidth, m_input.outputHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_roughnessTexture[0].Init(m_pDevice, "Reflection Denoiser - Extracted Roughness Texture 0", &roughnessTexture_Desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
        m_roughnessTexture[1].Init(m_pDevice, "Reflection Denoiser - Extracted Roughness Texture 1", &roughnessTexture_Desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    }
    {
        m_metricsUAVBuffer.InitBuffer(m_pDevice, "HSR - Metrics", &CD3DX12_RESOURCE_DESC::Buffer(16 * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), elementSize,
                                      D3D12_RESOURCE_STATE_COMMON);
        m_pMetricsUploadBuffer = AllocCPUVisible(m_pDevice->GetDevice(), 16 * elementSize);
        m_pMetricsUploadBuffer->Map(0, NULL, (void **)&m_pMetricsMap);
    }

    {
        CD3DX12_RESOURCE_DESC Desc =
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, m_input.outputWidth, m_input.outputHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_debugImage.Init(m_pDevice, "HSR Debug Image", &Desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    }
    m_bufferIndex = 0;
}

void HSR::SetupPSOTable(int mask) {
    auto createPSOTable = [this](int mask, std::map<const std::string, std::string> const &extra_defines) {
        PSOTable &old_psoTable  = m_psoTables[mask];
        PSOTable  copy_psoTable = old_psoTable;
        auto      createPSO     = [this](std::string const &filename, std::map<const std::string, std::string> const &_defines, std::string const &entry,
                                std::map<const std::string, std::string> const &extra_defines = {}) {
            D3D12_SHADER_BYTECODE shaderByteCode = {};
            DefineList            defines;
            for (auto &item : _defines) defines[item.first] = item.second;
            for (auto &item : extra_defines) defines[item.first] = item.second;
            CompileShaderFromFile(filename.c_str(), &defines, entry.c_str(), "-T cs_6_5 /Zi /Zss", &shaderByteCode);
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
                descPso.CS                                = shaderByteCode;
                descPso.Flags                             = D3D12_PIPELINE_STATE_FLAG_NONE;
                descPso.pRootSignature                    = m_pGlobalRootSignature;
                descPso.NodeMask                          = 0;
                ID3D12PipelineState *pPSO                 = NULL;
                HRESULT              hr                   = m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&pPSO));
                if (SUCCEEDED(hr))
                    return pPSO;
                else
                    return (ID3D12PipelineState *)NULL;
            }
        };
        if (extra_defines.size() == 0) {
            if (m_pPrimaryRayTracingPSO) m_pPrimaryRayTracingPSO->Release();
            m_pPrimaryRayTracingPSO = createPSO("PrimaryRayTracing.hlsl", {}, "main", {});
        }
        PSOTable new_psoTable{};
        new_psoTable.m_pAccumulate             = createPSO("Accumulate.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pClassifyTiles          = createPSO("ClassifyTiles.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pHybridPSODeferred      = createPSO("Intersect.hlsl",
                                                      {
                                                          {"USE_SSR", "1"},
                                                          {"USE_DEFERRED_SSR", "1"},
                                                      },
                                                      "main", extra_defines);
        new_psoTable.m_pPrepareIndirectSW      = createPSO("PrepareIndirectArgs.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pReproject              = createPSO("Reproject.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pPrepareIndirect        = createPSO("Intersect.hlsl", {}, "PrepareIndirect", extra_defines);
        new_psoTable.m_pResetDownsampleCounter = createPSO("Intersect.hlsl", {}, "ClearDownsampleCounter", extra_defines);
        new_psoTable.m_pPrefilter              = createPSO("Prefilter.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pResolveTemporal        = createPSO("TemporalAccumulation.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pRTPSODeferred          = createPSO("Intersect.hlsl", {{"USE_INLINE_RAYTRACING", "1"}, {"USE_DEFERRED_RAYTRACING", "1"}}, "main", extra_defines);
        new_psoTable.m_pDeferredShadeRays      = createPSO("Intersect.hlsl", {}, "DeferredShade", extra_defines);
        new_psoTable.m_pApplyReflections       = createPSO("ApplyReflections.hlsl", {}, "main", extra_defines);
        new_psoTable.m_pDownsampleGbuffer      = createPSO("HalfResGbuffer.hlsl", {}, "main", extra_defines);
        old_psoTable.m_pAccumulate             = new_psoTable.m_pAccumulate ? new_psoTable.m_pAccumulate : old_psoTable.m_pAccumulate;
        old_psoTable.m_pClassifyTiles          = new_psoTable.m_pClassifyTiles ? new_psoTable.m_pClassifyTiles : old_psoTable.m_pClassifyTiles;
        old_psoTable.m_pHybridPSODeferred      = new_psoTable.m_pHybridPSODeferred ? new_psoTable.m_pHybridPSODeferred : old_psoTable.m_pHybridPSODeferred;
        old_psoTable.m_pPrepareIndirectSW      = new_psoTable.m_pPrepareIndirectSW ? new_psoTable.m_pPrepareIndirectSW : old_psoTable.m_pPrepareIndirectSW;
        old_psoTable.m_pReproject              = new_psoTable.m_pReproject ? new_psoTable.m_pReproject : old_psoTable.m_pReproject;
        old_psoTable.m_pPrepareIndirect        = new_psoTable.m_pPrepareIndirect ? new_psoTable.m_pPrepareIndirect : old_psoTable.m_pPrepareIndirect;
        old_psoTable.m_pResetDownsampleCounter = new_psoTable.m_pResetDownsampleCounter ? new_psoTable.m_pResetDownsampleCounter : old_psoTable.m_pResetDownsampleCounter;
        old_psoTable.m_pPrefilter              = new_psoTable.m_pPrefilter ? new_psoTable.m_pPrefilter : old_psoTable.m_pPrefilter;
        old_psoTable.m_pResolveTemporal        = new_psoTable.m_pResolveTemporal ? new_psoTable.m_pResolveTemporal : old_psoTable.m_pResolveTemporal;
        old_psoTable.m_pRTPSODeferred          = new_psoTable.m_pRTPSODeferred ? new_psoTable.m_pRTPSODeferred : old_psoTable.m_pRTPSODeferred;
        old_psoTable.m_pDeferredShadeRays      = new_psoTable.m_pDeferredShadeRays ? new_psoTable.m_pDeferredShadeRays : old_psoTable.m_pDeferredShadeRays;
        old_psoTable.m_pApplyReflections       = new_psoTable.m_pApplyReflections ? new_psoTable.m_pApplyReflections : old_psoTable.m_pApplyReflections;
        old_psoTable.m_pDownsampleGbuffer      = new_psoTable.m_pDownsampleGbuffer ? new_psoTable.m_pDownsampleGbuffer : old_psoTable.m_pDownsampleGbuffer;

        if (!old_psoTable.m_pAccumulate) throw 1;
        if (!old_psoTable.m_pClassifyTiles) throw 1;
        if (!old_psoTable.m_pHybridPSODeferred) throw 1;
        if (!old_psoTable.m_pReproject) throw 1;
        if (!old_psoTable.m_pPrepareIndirectSW) throw 1;
        if (!old_psoTable.m_pPrepareIndirect) throw 1;
        if (!old_psoTable.m_pResetDownsampleCounter) throw 1;
        if (!old_psoTable.m_pPrefilter) throw 1;
        if (!old_psoTable.m_pResolveTemporal) throw 1;
        if (!old_psoTable.m_pRTPSODeferred) throw 1;
        if (!old_psoTable.m_pDeferredShadeRays) throw 1;
        if (!old_psoTable.m_pApplyReflections) throw 1;
        if (!old_psoTable.m_pDownsampleGbuffer) throw 1;

        // Release old ones
        if (copy_psoTable.m_pAccumulate && copy_psoTable.m_pAccumulate != old_psoTable.m_pAccumulate) copy_psoTable.m_pAccumulate->Release();
        if (copy_psoTable.m_pClassifyTiles && copy_psoTable.m_pClassifyTiles != old_psoTable.m_pClassifyTiles) copy_psoTable.m_pClassifyTiles->Release();
        if (copy_psoTable.m_pHybridPSODeferred && copy_psoTable.m_pHybridPSODeferred != old_psoTable.m_pHybridPSODeferred) copy_psoTable.m_pHybridPSODeferred->Release();
        if (copy_psoTable.m_pPrepareIndirectSW && copy_psoTable.m_pPrepareIndirectSW != old_psoTable.m_pPrepareIndirectSW) copy_psoTable.m_pPrepareIndirectSW->Release();
        if (copy_psoTable.m_pReproject && copy_psoTable.m_pReproject != old_psoTable.m_pReproject) copy_psoTable.m_pReproject->Release();
        if (copy_psoTable.m_pPrepareIndirect && copy_psoTable.m_pPrepareIndirect != old_psoTable.m_pPrepareIndirect) copy_psoTable.m_pPrepareIndirect->Release();
        if (copy_psoTable.m_pResetDownsampleCounter && copy_psoTable.m_pResetDownsampleCounter != old_psoTable.m_pResetDownsampleCounter)
            copy_psoTable.m_pResetDownsampleCounter->Release();
        if (copy_psoTable.m_pPrefilter && copy_psoTable.m_pPrefilter != old_psoTable.m_pPrefilter) copy_psoTable.m_pPrefilter->Release();
        if (copy_psoTable.m_pResolveTemporal && copy_psoTable.m_pResolveTemporal != old_psoTable.m_pResolveTemporal) copy_psoTable.m_pResolveTemporal->Release();
        if (copy_psoTable.m_pRTPSODeferred && copy_psoTable.m_pRTPSODeferred != old_psoTable.m_pRTPSODeferred) copy_psoTable.m_pRTPSODeferred->Release();
        if (copy_psoTable.m_pDeferredShadeRays && copy_psoTable.m_pDeferredShadeRays != old_psoTable.m_pDeferredShadeRays) copy_psoTable.m_pDeferredShadeRays->Release();
        if (copy_psoTable.m_pApplyReflections && copy_psoTable.m_pApplyReflections != old_psoTable.m_pApplyReflections) copy_psoTable.m_pApplyReflections->Release();
        if (copy_psoTable.m_pDownsampleGbuffer && copy_psoTable.m_pDownsampleGbuffer != old_psoTable.m_pDownsampleGbuffer) copy_psoTable.m_pDownsampleGbuffer->Release();
    };
    createPSOTable(0 | 0 | 0 | 0, {});
    createPSOTable(1 | 0 | 0 | 0, {{"HSR_DEBUG", "1"}});
    createPSOTable(0 | 2 | 0 | 0, {{"HSR_TRANSPARENT_QUERY", "1"}});
    createPSOTable(1 | 2 | 0 | 0, {{"HSR_TRANSPARENT_QUERY", "1"}, {"HSR_DEBUG", "1"}});

    createPSOTable(0 | 0 | 0 | 8, {{"UPSCALE", "1"}});
    createPSOTable(1 | 0 | 0 | 8, {{"UPSCALE", "1"}, {"HSR_DEBUG", "1"}});
    createPSOTable(0 | 2 | 0 | 8, {{"UPSCALE", "1"}, {"HSR_TRANSPARENT_QUERY", "1"}});
    createPSOTable(1 | 2 | 0 | 8, {{"UPSCALE", "1"}, {"HSR_TRANSPARENT_QUERY", "1"}, {"HSR_DEBUG", "1"}});

    createPSOTable(0 | 0 | 4 | 0, {{"HSR_SHADING_USE_SCREEN", "1"}});
    createPSOTable(1 | 0 | 4 | 0, {{"HSR_DEBUG", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});
    createPSOTable(0 | 2 | 4 | 0, {{"HSR_TRANSPARENT_QUERY", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});
    createPSOTable(1 | 2 | 4 | 0, {{"HSR_TRANSPARENT_QUERY", "1"}, {"HSR_DEBUG", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});

    createPSOTable(0 | 0 | 4 | 8, {{"UPSCALE", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});
    createPSOTable(1 | 0 | 4 | 8, {{"UPSCALE", "1"}, {"HSR_DEBUG", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});
    createPSOTable(0 | 2 | 4 | 8, {{"UPSCALE", "1"}, {"HSR_TRANSPARENT_QUERY", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});
    createPSOTable(1 | 2 | 4 | 8, {{"UPSCALE", "1"}, {"HSR_TRANSPARENT_QUERY", "1"}, {"HSR_DEBUG", "1"}, {"HSR_SHADING_USE_SCREEN", "1"}});
}

void HSR::SetupPerformanceCounters() {
    // Create timestamp querying resources if enabled
    if (m_isPerformanceCountersEnabled) {
        ID3D12Device *device = m_pDevice->GetDevice();

        auto const query_heap_size = (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT * m_frameCountBeforeReuse * sizeof(std::uint64_t);

        D3D12_QUERY_HEAP_DESC query_heap_desc = {};
        query_heap_desc.Type                  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query_heap_desc.Count                 = static_cast<UINT>(query_heap_size);

        ThrowIfFailed(device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&m_pTimestampQueryHeap)));

        D3D12_HEAP_PROPERTIES heap_properties = {};
        heap_properties.Type                  = D3D12_HEAP_TYPE_READBACK;
        heap_properties.CreationNodeMask      = 1u;
        heap_properties.VisibleNodeMask       = 1u;

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Width               = static_cast<UINT64>(query_heap_size);
        resource_desc.Height              = 1u;
        resource_desc.DepthOrArraySize    = 1u;
        resource_desc.MipLevels           = 1u;
        resource_desc.SampleDesc.Count    = 1u;
        resource_desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                      IID_PPV_ARGS(&m_pTimestampQueryBuffer)));

        m_pTimestampQueryBuffer->SetName(L"TimestampQueryBuffer");
        m_timestampQueries.resize(m_frameCountBeforeReuse);
        for (auto &timestamp_queries : m_timestampQueries) {
            timestamp_queries.reserve((int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT);
        }
    }
}

void HSR::QueryTimestamps(ID3D12GraphicsCommandList *pCommandList) {
    // Query timestamp value prior to resolving the reflection view
    if (m_isPerformanceCountersEnabled) {
        auto &timestamp_queries = m_timestampQueries[m_timestampFrameIndex];

        if (!timestamp_queries.empty()) {
            std::uint64_t *data;

            // Reset performance counters
            for (int i = 0; i < (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT; i++) m_GpuTicks[i] = 0;

            auto const start_index = m_timestampFrameIndex * (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT;

            D3D12_RANGE read_range = {};
            read_range.Begin       = start_index * sizeof(std::uint64_t);
            read_range.End         = (start_index + timestamp_queries.size()) * sizeof(std::uint64_t);

            m_pTimestampQueryBuffer->Map(0u, &read_range, reinterpret_cast<void **>(&data));

            for (auto i = 0u, j = 1u; j < timestamp_queries.size(); ++i, ++j) {
                auto const elapsed_time               = (data[j] - data[i]);
                m_GpuTicks[(int)timestamp_queries[j]] = elapsed_time;
            }

            m_pTimestampQueryBuffer->Unmap(0u, nullptr);
            m_TotalTime = data[timestamp_queries.size() - 1] - data[0];
        }

        timestamp_queries.clear();

        pCommandList->EndQuery(m_pTimestampQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, GetTimestampQueryIndex());

        timestamp_queries.push_back(HSRTimestampQuery::TIMESTAMP_QUERY_INIT);
    }
}

uint32_t HSR::GetTimestampQueryIndex() const {
    return m_timestampFrameIndex * (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT + static_cast<uint32_t>(m_timestampQueries[m_timestampFrameIndex].size());
}

} // namespace HSR_SAMPLE_DX12