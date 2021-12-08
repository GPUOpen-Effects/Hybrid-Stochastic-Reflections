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

#include "Base/DynamicBufferRing.h"
#include "Base/Texture.h"
#include "BlueNoiseSampler.h"
#include "BufferDX12.h"
#include "GltfPbrPass.h"
#include "PostProc/MagnifierPS.h"

namespace hlsl {

#include "../../Shaders/Declarations.h"

}

/**
        Performs a rounded division.

        \param value The value to be divided.
        \param divisor The divisor to be used.
        \return The rounded divided value.
*/
template <typename TYPE> static inline TYPE RoundedDivide(TYPE value, TYPE divisor) { return (value + divisor - 1) / divisor; }

using namespace CAULDRON_DX12;
using namespace RTCAULDRON_DX12;

enum class HSRTimestampQuery {
    TIMESTAMP_QUERY_INIT,
    TIMESTAMP_QUERY_DOWNSAMPLE_GBUFFER,
    TIMESTAMP_QUERY_TILE_CLASSIFICATION,
    TIMESTAMP_QUERY_INTERSECTION_SW,
    TIMESTAMP_QUERY_INTERSECTION_HW,
    TIMESTAMP_QUERY_DENOISING_REPROJECT,
    TIMESTAMP_QUERY_DENOISING_NEIGHBOR_X16,
    TIMESTAMP_QUERY_DENOISING_TEMPORAL,
    TIMESTAMP_QUERY_DENOISING_APPLY,
    TIMESTAMP_QUERY_COUNT
};

static char const *GetTimestampName(int slot) {
    static char const *const TimestampQueryNames[] = {
        "FFX_HSR_INIT",
        "FFX_HSR_DOWNSAMPLE_GBUFFER",
        "FFX_HSR_TILE_CLASSIFICATION",
        "FFX_HSR_INTERSECTION_SW",
        "FFX_HSR_INTERSECTION_HW",
        "FFX_HSR_DENOISING_REPROJECT",
        "FFX_HSR_DENOISING_NEIGHBOR_X16",
        "FFX_HSR_DENOISING_TEMPORAL",
        "FFX_HSR_DENOISING_APPLY",
    };
    return TimestampQueryNames[slot];
}

struct State {
    float  time;
    float  deltaTime;
    float  cameraFOV;
    Camera camera;

    float               exposure;
    float               emmisiveFactor;
    float               iblFactor;
    float               mipBias;
    float               lightIntensity;
    XMFLOAT3            lightColor;
    float               sunPhi;
    float               sunTheta;
    Vectormath::Vector4 sunPosition;
    Vectormath::Vector4 sunLookAt;
    Vectormath::Vector4 sunDirection;
    Vectormath::Matrix4 sunView;
    Vectormath::Matrix4 sunProj;
    Vectormath::Matrix4 sunViewProj;

    float sunProjectionWidth;

    int  toneMapper;
    int  skyDomeType;
    bool bDrawBoundingBoxes;
    bool bDrawLightFrustum;
    bool bDrawBloom;
    bool bUpdateSimulation = true;

    float targetFrametime;

    int   maxTraversalIterations;
    int   mostDetailedDepthHierarchyMipLevel;
    float depthBufferThickness;
    int   minTraversalOccupancy;
    int   samplesPerQuad;
    bool  bEnableVarianceGuidedTracing;
    float roughnessThreshold;

    bool   bClearAccumulator = false;
    bool   bAnimateCamera    = false;
    Camera flashlightCamera;
    bool   bFlashLight       = true;
    bool   bAttachFlashLight = false;

    Vectormath::Vector4 SpotLightPositions[0x10] = {};
    Vectormath::Vector4 SpotLightLookAt[0x10]    = {};
    int                 NumSpotLights            = 0;
    float               SpotLightSpread          = 2;

    float           FlashLightIntensity = 20.0f;
    bool            bWeapon             = false;
    bool            isBenchmarking      = false;
    std::string     screenshotName;
    hlsl::FrameInfo frameInfo            = {};
    bool            bTAA                 = false;
    bool            bTAAJitter           = false;
    bool            bOptimizedDownsample = false;
    bool            bRenderDecals        = true;
    float           SunLightIntensity    = 10.0f;
    // In microseconds
    double hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT] = {};

    float m_ReflectionResolutionMultiplier = 0.5f;

    double m_numSWRays = 0.0;
    double m_numHWRays = 0.0;
    double m_numHYRays = 0.0;

    bool                        bUseMagnifier = false;
    bool                        bLockMagnifierPosition;
    bool                        bLockMagnifierPositionHistory;
    int                         LockedMagnifiedScreenPositionX;
    int                         LockedMagnifiedScreenPositionY;
    MagnifierPS::PassParameters magnifierParams;
};

namespace HSR_SAMPLE_DX12 {
struct HSRCreationInfo {
    uint32_t inputWidth;
    uint32_t inputHeight;
    uint32_t outputWidth;
    uint32_t outputHeight;
};

class HSR {
public:
    struct ReflectionGBuffer {
        Texture *pDepth;
        Texture *pAlbedo;
        Texture *pNormals;
        Texture *pSpecularRoughness;
        Texture *pMotionVectors;
    };
    HSR();
    void OnCreate(Device *pDevice, ID3D12RootSignature *pGlobalRootSignature, StaticResourceViewHeap &cpuVisibleHeap, ResourceViewHeaps &resourceHeap, UploadHeap &uploadHeap,
                  DynamicBufferRing &constantBufferRing, uint32_t frameCountBeforeReuse, bool enablePerformanceCounters);
    void OnCreateWindowSizeDependentResources(const HSRCreationInfo &input);

    void OnDestroy();
    void OnDestroyWindowSizeDependentResources();

    void     Draw(ID3D12GraphicsCommandList *pCommandList, Texture *pHDROut, ReflectionGBuffer *pLowResGbuffer, CBV_SRV_UAV *pGlobalTable, SAMPLER *pGlobalSamplers, State *pState);
    Texture *GetDebugTexture() { return &m_debugImage; }

    std::uint64_t GetTimestamp(int slot) const { return m_GpuTicks[slot]; }
    std::uint64_t GetTotalTime() const { return m_TotalTime; }
    void          Recompile();

private:
    void CreateResources();
    void CreateWindowSizeDependentResources();

    void     SetupPSOTable(int mask);
    void     SetupPerformanceCounters();
    void     QueryTimestamps(ID3D12GraphicsCommandList *pCommandList);
    uint32_t GetTimestampQueryIndex() const;

    Device *                m_pDevice;
    DynamicBufferRing *     m_pConstantBufferRing;
    StaticResourceViewHeap *m_pCpuVisibleHeap;
    ResourceViewHeaps *     m_pResourceViewHeaps;
    UploadHeap *            m_pUploadHeap;
    UploadHeapBuffersDX12   m_uploadHeapBuffers;

    HSRCreationInfo m_input;

    // Containing SW rays that need to be traced.
    Texture m_rayList;
    // List of HW rays
    Texture m_hwRayList;
    // Buffer for deferred ray traced shading
    Texture m_GBufferList;
    // List of tiles for denoiser
    Texture m_denoiseTileList;
    // Contains the number of rays that we trace and tiles for denoiser.
    Texture m_rayCounter;
    Texture m_intersectionPassIndirectArgs;

    /////////////////////////
    // Metrics boilerplate //
    /////////////////////////
    // GPU Visible buffer
    Texture m_metricsUAVBuffer;
    // CPU Visible buffer
    ID3D12Resource *m_pMetricsUploadBuffer = NULL;
    // CPU Visible pointer
    int32_t *m_pMetricsMap = NULL;
    /////////////////////////

    // For visualization purposes
    Texture m_debugImage;
    // Extracted roughness values, also double buffered to keep the history.
    Texture m_roughnessTexture[2];

    // 0, 1 - ping-pong, 2 - for reprojection
    Texture m_radianceBuffer[3]; // rgba16 or r11g11b10
    Texture m_radianceAux[4];    // r16

    ///////////////////////////////////////////
    // Per Tile Images : 1/8th of resolution //
    ///////////////////////////////////////////
    // 1/8 resolution uint32 Hit/miss counter image per tile for current and previous frames.
    Texture m_counterImage[2];
    // 1/8 resolution image to keep avg radiance per tile from the previous frame
    Texture m_radianceAvg[2];

    Texture m_randomNumberImage;

    ////////////////////
    ////////////////////
    ////////////////////

    // Hold the blue noise buffers.
    BlueNoiseSamplerD3D12 m_blueNoiseSampler;

    ID3D12RootSignature *m_pGlobalRootSignature  = nullptr;
    ID3D12PipelineState *m_pPrimaryRayTracingPSO = nullptr;
    struct PSOTable {
        ID3D12PipelineState *m_pAccumulate        = nullptr;
        ID3D12PipelineState *m_pDeferredShadeRays = nullptr;
        ID3D12PipelineState *m_pDownsampleGbuffer = nullptr;
        ID3D12PipelineState *m_pPrepareIndirectSW = nullptr;
        ID3D12PipelineState *m_pClassifyTiles     = nullptr;
        ID3D12PipelineState *m_pRTPSODeferred     = nullptr;
        ID3D12PipelineState *m_pHybridPSODeferred = nullptr;
        ID3D12PipelineState *m_pPrepareIndirect   = nullptr;

        ID3D12PipelineState *m_pResetDownsampleCounter = nullptr;
        ID3D12PipelineState *m_pApplyReflections       = nullptr;

        ID3D12PipelineState *m_pReproject       = nullptr;
        ID3D12PipelineState *m_pPrefilter       = nullptr;
        ID3D12PipelineState *m_pResolveTemporal = nullptr;

        void OnDestroy() {
            if (m_pAccumulate) m_pAccumulate->Release();
            if (m_pDeferredShadeRays) m_pDeferredShadeRays->Release();
            if (m_pDownsampleGbuffer) m_pDownsampleGbuffer->Release();
            if (m_pReproject) m_pReproject->Release();
            if (m_pPrefilter) m_pPrefilter->Release();
            if (m_pPrepareIndirectSW) m_pPrepareIndirectSW->Release();
            if (m_pClassifyTiles) m_pClassifyTiles->Release();
            if (m_pRTPSODeferred) m_pRTPSODeferred->Release();
            if (m_pHybridPSODeferred) m_pHybridPSODeferred->Release();
            if (m_pPrepareIndirect) m_pPrepareIndirect->Release();
            if (m_pResolveTemporal) m_pResolveTemporal->Release();
            if (m_pResetDownsampleCounter) m_pResetDownsampleCounter->Release();
            if (m_pApplyReflections) m_pApplyReflections->Release();
            *this = {};
        }
    };

    // Flags -> pso
    std::unordered_map<uint32_t, PSOTable> m_psoTables;

    // The command signature for the indirect dispatches.
    ID3D12CommandSignature *m_pCommandSignature;

    uint32_t m_frameCountBeforeReuse;
    uint32_t m_bufferIndex;

    // The type definition for an array of timestamp queries.
    using TimestampQueries = std::vector<HSRTimestampQuery>;

    // The query heap for the recorded timestamps.
    ID3D12QueryHeap *m_pTimestampQueryHeap = NULL;
    // The buffer for reading the timestamp queries.
    ID3D12Resource *m_pTimestampQueryBuffer = NULL;
    // The number of GPU ticks spent in the tile classification pass.
    std::uint64_t m_GpuTicks[(int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT]{};
    std::uint64_t m_TotalTime = 0;
    // The array of timestamp that were queried.
    std::vector<TimestampQueries> m_timestampQueries;
    // The index of the active set of timestamp queries.
    uint32_t m_timestampFrameIndex;
    bool     m_isPerformanceCountersEnabled;
};
} // namespace HSR_SAMPLE_DX12