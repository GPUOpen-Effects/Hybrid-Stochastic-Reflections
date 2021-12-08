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

#include "HSRSample.h"
#include "base/ShaderCompilerCache.h"
#include <iomanip>
#include <sstream>

#ifdef _DEBUG
const bool CPU_BASED_VALIDATION_ENABLED = true;
const bool GPU_BASED_VALIDATION_ENABLED = false;
#else
const bool CPU_BASED_VALIDATION_ENABLED = false;
const bool GPU_BASED_VALIDATION_ENABLED = false;
#endif //  _DEBUG

HSRSample::HSRSample(LPCSTR name) : FrameworkWindows(name) {
    m_DeltaTime     = 0;
    m_Distance      = 0;
    m_Pitch         = 0;
    m_Yaw           = 0;
    m_selectedScene = 0;

    m_LastFrameTime = MillisecondsNow();
    m_Time          = 0;
    m_bPlay         = true;
    m_bShowUI       = true;

    m_CameraControlSelected = 1; // select WASD on start up

    m_pGltfLoader = NULL;

    ImGuiStyle &style                           = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg]             = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    style.Colors[ImGuiCol_FrameBg]              = ImVec4(0.01f, 0.01f, 0.01f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg]            = ImVec4(0.01f, 0.01f, 0.01f, 0.80f);
    style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.20f, 0.20f, 0.20f, 0.60f);
    style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}

static constexpr float MagnifierBorderColor_Locked[3] = {0.002f, 0.72f, 0.0f};
static constexpr float MagnifierBorderColor_Free[3]   = {0.72f, 0.002f, 0.0f};

static float weapon_offset_x     = 0.055f;
static float weapon_offset_y     = -0.08f;
static float weapon_offset_z     = -0.1f;
static float weapon_offset_scale = 0.1f;

static Vectormath::Vector3 weapon_pos     = {0.0f, 0.0f, 0.0f};
static Vectormath::Vector3 weapon_tip_pos = {0.0f, 0.0f, 0.0f};

void UpdateSun(State &m_State) {
    {
        float aspectRatio = 1.0f;
        float nearp       = -50.0f;
        float farp        = 100.0f;
        float OrthoWidth  = m_State.sunProjectionWidth;
        m_State.sunProj   = math::Matrix4::orthographic(-OrthoWidth / 1.f, OrthoWidth / 1.f, -OrthoWidth / 1.f, OrthoWidth / 1.f, nearp, farp);
    }
    {
        auto  at   = m_State.camera.GetPosition(); // Vectormath::Vector4(0, 0, 0, 0);
        float step = 2.0;
        for (int i = 0; i < 3; i++) {
            float e = at.getElem(i);
            at.setElem(i, e > 0.0 ? (int(e / step) * step + step / 2) : (int((e - 1.0f) / step) * step - step / 2));
        }
        auto eyePos          = at + 50.0f * PolarToVector(m_State.sunPhi, m_State.sunTheta);
        m_State.sunDirection = Vectormath::SSE::normalize(PolarToVector(m_State.sunPhi, m_State.sunTheta));
        float shiftz         = 0.0f;
        m_State.sunPosition  = eyePos + Vectormath::Vector4(0, 0, 2.0f, 0);
        m_State.sunLookAt    = at;
        auto view            = LookAtRH(eyePos, at);
        m_State.sunView      = view;
        m_State.sunViewProj  = view * m_State.sunProj;
    }
}

#define IMGUI_TOOLTIP(msg)                                                                                                                                                         \
    if (ImGui::IsItemHovered()) {                                                                                                                                                  \
        ImGui::BeginTooltip();                                                                                                                                                     \
        ImGui::TextUnformatted(msg);                                                                                                                                               \
        ImGui::EndTooltip();                                                                                                                                                       \
    }

template <typename T> class ImGuiWrapper {
    T           t;
    char const *title;
    char const *tooltip;

public:
    ImGuiWrapper(char const *title, char const *tooltip, T t) : t(t), title(title), tooltip(tooltip) {}
    ~ImGuiWrapper() {
        ImGui::Text(title);
        ImGui::PushID(title);
        t();
        IMGUI_TOOLTIP(tooltip);
        ImGui::PopID();
    }
};

template <typename F> static ImGuiWrapper<F> wrap_imgui(char const *title, char const *tooltip, F t) { return ImGuiWrapper<F>(title, tooltip, t); }

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void HSRSample::OnCreate() {
    // get the list of scenes
    for (const auto &scene : m_JsonConfigFile["scenes"]) m_SceneNames.push_back(scene["name"]);

    DWORD dwAttrib = GetFileAttributes("..\\Cauldron-Media\\");
    if ((dwAttrib == INVALID_FILE_ATTRIBUTES) || ((dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) == 0) {
        MessageBox(NULL, "Media files not found!\n\nPlease check the readme on how to get the media files.", "Cauldron Panic!", MB_ICONERROR);
        exit(0);
    }

    // Init the shader compiler
    InitDirectXCompiler();
    CreateShaderCache();

    // Create a instance of the renderer and initialize it, we need to do that for each GPU
    //
    m_Node = new SampleRenderer();
    m_Node->OnCreate(&m_device, &m_swapChain);

    // init GUI (non gfx stuff)
    //
    ImGUI_Init((void *)m_windowHwnd);

    // Init Camera, looking at the origin
    //

    // init GUI state
    // m_State.camera.LookAt(Vectormath::Vector4(-1.24344, 0.179643, -5.38784, 0), Vectormath::Vector4(0.386137, 0.128114, -5.49597, 0) + Vectormath::Vector4(1, 0., 0., 0));
    m_State.toneMapper                                 = 2;
    m_State.skyDomeType                                = 0;
    m_State.exposure                                   = 1.0f;
    m_State.emmisiveFactor                             = 1.0f;
    m_State.iblFactor                                  = 0.03f;
    m_State.frameInfo.reflection_factor                = 2.0f;
    m_State.mipBias                                    = -0.5f;
    m_State.bDrawBoundingBoxes                         = false;
    m_State.bDrawLightFrustum                          = false;
    m_State.bDrawBloom                                 = false;
    m_State.lightIntensity                             = 10.f;
    m_State.SunLightIntensity                          = 2.2f;
    m_State.FlashLightIntensity                        = 4.2f;
    m_State.lightColor                                 = XMFLOAT3(1.0f, 0.75f, 0.61f);
    m_State.targetFrametime                            = 0; // 16;
    m_State.frameInfo.temporal_stability_factor        = 0.985f;
    m_State.maxTraversalIterations                     = 128;
    m_State.mostDetailedDepthHierarchyMipLevel         = 0;
    m_State.depthBufferThickness                       = 0.1f;
    m_State.minTraversalOccupancy                      = 0;
    m_State.samplesPerQuad                             = 4;
    m_State.bEnableVarianceGuidedTracing               = false;
    m_State.roughnessThreshold                         = 0.22f;
    m_State.frameInfo.rt_roughness_threshold           = 0.22f;
    m_State.screenshotName                             = "";
    m_State.frameInfo.max_raytraced_distance           = 100.0f;
    m_State.frameInfo.ray_length_exp_factor            = 5.0f;
    m_State.frameInfo.depth_similarity_sigma           = 0.05f;
    m_State.bTAA                                       = true;
    m_State.bTAAJitter                                 = true;
    m_State.frameInfo.vrt_variance_threshold           = 0.02f;
    m_State.frameInfo.reflections_upscale_mode         = 3;
    m_State.frameInfo.fsr_roughness_threshold          = 0.03f;
    m_State.frameInfo.random_samples_per_pixel         = 32;
    m_State.frameInfo.ssr_confidence_threshold         = 0.998f;
    m_State.frameInfo.max_history_samples              = 32;
    m_State.frameInfo.history_clip_weight              = 0.5f;
    m_State.frameInfo.hybrid_miss_weight               = 0.5f;
    m_State.frameInfo.hybrid_spawn_rate                = 0.02f;
    m_State.frameInfo.ssr_thickness_length_factor      = 0.01f;
    m_State.frameInfo.reflections_backfacing_threshold = 1.00f;
    m_State.cameraFOV                                  = XM_PI / 3;
    // UpdateSun(m_State, -2.611, 0.055);
    // init magnifier params
    for (int ch = 0; ch < 3; ++ch) m_State.magnifierParams.fBorderColorRGB[ch] = MagnifierBorderColor_Free[ch]; // start at free state

    LoadScene(m_selectedScene);
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void HSRSample::OnDestroy() {
    ImGUI_Shutdown();

    m_device.GPUFlush();

    m_Node->UnloadScene();
    m_Node->OnDestroyWindowSizeDependentResources();
    m_Node->OnDestroy();

    delete m_Node;

    // shut down the shader compiler
    DestroyShaderCache(&m_device);

    if (m_pGltfLoader) {
        delete m_pGltfLoader;
        m_pGltfLoader = NULL;
    }
}

//--------------------------------------------------------------------------------------
//
// OnEvent, forward Win32 events to ImGUI
//
//--------------------------------------------------------------------------------------
bool HSRSample::OnEvent(MSG msg) {
    if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam)) return true;

    return true;
}

//--------------------------------------------------------------------------------------
//
// SetFullScreen
//
//--------------------------------------------------------------------------------------
void HSRSample::SetFullScreen(bool fullscreen) {
    m_device.GPUFlush();

    m_swapChain.SetFullScreen(fullscreen);
}

static void ToggleMagnifierLockedState(State &state, const ImGuiIO &io) {
    if (state.bUseMagnifier) {
        state.bLockMagnifierPositionHistory = state.bLockMagnifierPosition;  // record histroy
        state.bLockMagnifierPosition        = !state.bLockMagnifierPosition; // flip state
        const bool bLockSwitchedOn          = !state.bLockMagnifierPositionHistory && state.bLockMagnifierPosition;
        const bool bLockSwitchedOff         = state.bLockMagnifierPositionHistory && !state.bLockMagnifierPosition;
        if (bLockSwitchedOn) {
            const ImGuiIO &io                    = ImGui::GetIO();
            state.LockedMagnifiedScreenPositionX = static_cast<int>(io.MousePos.x);
            state.LockedMagnifiedScreenPositionY = static_cast<int>(io.MousePos.y);
            for (int ch = 0; ch < 3; ++ch) state.magnifierParams.fBorderColorRGB[ch] = MagnifierBorderColor_Locked[ch];
        } else if (bLockSwitchedOff) {
            for (int ch = 0; ch < 3; ++ch) state.magnifierParams.fBorderColorRGB[ch] = MagnifierBorderColor_Free[ch];
        }
    }
}

void HSRSample::UpdateReflectionResolution() {
    if (m_State.bOptimizedDownsample) m_State.m_ReflectionResolutionMultiplier = 0.5f;
    m_ReflectionWidth  = max(128u, uint32_t(m_Width * m_State.m_ReflectionResolutionMultiplier));
    m_ReflectionHeight = max(128u, uint32_t(m_Height * m_State.m_ReflectionResolutionMultiplier));
    if (m_Width > 0 && m_Height > 0) {
        if (m_Node != NULL) {
            m_Node->OnDestroyWindowSizeDependentResources();
            m_Node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, m_ReflectionWidth, m_ReflectionHeight, &m_State);
        }
    }
}

void HSRSample::BuildUI() {
    ImGuiStyle &style     = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;

    bool opened = true;

    ImGui::Begin("HSR Profiler", &opened);
    if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Resolution       : %ix%i", m_Width, m_Height);
        std::string device, vendor;
        m_device.GetDeviceInfo(&device, &vendor);
        ImGui::Text("Device           : %s", device.c_str());
        ImGui::Text("Total CPU time in us : %f", m_DeltaTime * 1000.0);
        const std::vector<TimeStamp> &timeStamps = m_Node->GetTimingValues();
        ImGui::Text("Total GPU time in us : %f", timeStamps[timeStamps.size() - 1].m_microseconds);

        if (ImGui::CollapsingHeader("Hybrid Reflections Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Value("Intersection Total",
                         (float)m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] +
                             (float)m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW],
                         "%.1f us");

            {
                // scrolling data and average computing
                static float values[128] = {};
                values[127]              = (float)m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] +
                              (float)m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW];
                for (uint32_t i = 0; i < 128 - 1; i++) {
                    values[i] = values[i + 1];
                }
                ImGui::PlotLines("", values, 128, 0, "Intersection time (us)", 0.0f, 5000.0f, ImVec2(0, 80));
            }

            {
                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                auto        wsize     = ImGui::GetWindowSize().x;
                {
                    const ImVec2 p       = ImGui::GetCursorScreenPos();
                    float        x       = p.x + 0.0f;
                    float        y       = p.y + 0.0f;
                    static float sz      = 12.0f;
                    float        spacing = 6.0f;
                    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), ImColor(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)));
                    y += sz + spacing;
                    ImGui::Dummy(ImVec2(sz, sz));
                    ImGui::SameLine();
                    ImGui::Text("- Screen Space rays");

                    draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), ImColor(ImVec4(0.0f, 0.0f, 1.0f, 1.0f)));
                    y += sz + spacing;
                    ImGui::Dummy(ImVec2(sz, sz));
                    ImGui::SameLine();
                    ImGui::Text("- DXR rays");
                }
                {

                    ImGui::Text("Rays proportion");
                    float hw_ratio = (float)(wsize * (m_State.m_numHWRays) / (m_State.m_numSWRays + m_State.m_numHWRays));
                    float sw_ratio = (float)(wsize * (m_State.m_numSWRays) / (m_State.m_numSWRays + m_State.m_numHWRays));
                    // float         hy_ratio = wsize * (m_State.m_numHYRays) / (m_State.m_numSWRays + m_State.m_numHWRays);
                    static float  sz  = 12.0f;
                    static ImVec4 col = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
                    {
                        const ImVec2 p       = ImGui::GetCursorScreenPos();
                        float        x       = p.x + 4.0f;
                        float        y       = p.y + 4.0f;
                        float        spacing = 8.0f;

                        draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sw_ratio, y + sz), ImColor(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)));
                        draw_list->AddRectFilled(ImVec2(x + sw_ratio, y), ImVec2(x + sw_ratio + hw_ratio, y + sz), ImColor(ImVec4(0.0f, 0.0f, 1.0f, 1.0f)));
                        y += sz + spacing;
                        ImGui::Dummy(ImVec2(1, sz + spacing));
                    }
                }
                {
                    ImGui::Text("Intersection time proportion");
                    float         hw_ratio = (float)(wsize * (m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW]) /
                                             (m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] +
                                              m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW]));
                    float         sw_ratio = (float)(wsize * (m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW]) /
                                             (m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] +
                                              m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW]));
                    static float  sz       = 12.0f;
                    static ImVec4 col      = ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
                    {
                        const ImVec2 p       = ImGui::GetCursorScreenPos();
                        float        x       = p.x + 4.0f;
                        float        y       = p.y + 4.0f;
                        float        spacing = 8.0f;

                        draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sw_ratio, y + sz), ImColor(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)));
                        draw_list->AddRectFilled(ImVec2(x + sw_ratio, y), ImVec2(x + sw_ratio + hw_ratio, y + sz), ImColor(ImVec4(0.0f, 0.0f, 1.0f, 1.0f)));
                        y += sz + spacing;
                        ImGui::Dummy(ImVec2(1, sz + spacing));
                    }
                }
                ImGui::Separator();
            }

            ImGui::Value("ns per HW Ray", (float)(1000.0 * m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW] / m_State.m_numHWRays), "%.1f ns");
            ImGui::Value("ns per SS Ray", (float)(1000.0 * m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] / m_State.m_numSWRays), "%.1f ns");
            ImGui::Value("ns per Ray Avg.",
                         (float)(1000.0 *
                                 (m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] +
                                  m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW]) /
                                 (m_State.m_numSWRays + m_State.m_numHWRays)),
                         "%.1f ns");

            ImGui::Value("Total # of SS Rays", (float)m_State.m_numSWRays);
            ImGui::Value("of which hybrid Rays", (float)m_State.m_numHYRays);
            ImGui::Value("Total # of HW Rays", (float)m_State.m_numHWRays);

            ImGui::Value("HW Rays %", (float)m_State.m_numHWRays / (float)(m_State.m_numHWRays + m_State.m_numSWRays) * 100.0f);
            ImGui::Value("HW Intersection Time %", (float)m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW] /
                                                       (float)(m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_SW] +
                                                               m_State.hsr_timestamps[(int)HSRTimestampQuery::TIMESTAMP_QUERY_INTERSECTION_HW]) *
                                                       100.0f);
            ImGui::Value("Total Rays", (float)(m_State.m_numHWRays + m_State.m_numSWRays));
            if (ImGui::CollapsingHeader("Detailed timings")) {
                for (int i = 0; i < (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT; i++) {
                    ImGui::Value(GetTimestampName(i), (float)m_State.hsr_timestamps[i], "%.1f us");
                }
            }

        }
    }
    if (ImGui::CollapsingHeader("Overview Profiler", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<TimeStamp> &timeStamps = m_Node->GetTimingValues();
        if (timeStamps.size() > 0) {
            static double moving_average[128] = {0};
            if (m_State.frameInfo.frame_index < 100)
                for (uint32_t i = 0; i < timeStamps.size(); i++) moving_average[i] = timeStamps[i].m_microseconds;

            for (uint32_t i = 0; i < timeStamps.size(); i++) {
                moving_average[i] += (timeStamps[i].m_microseconds - moving_average[i]) * 0.01;
            }
            for (uint32_t i = 0; i < timeStamps.size(); i++) {
                ImGui::Text("%-22s: %7.1f", timeStamps[i].m_label.c_str(), moving_average[i]);
            }

            // scrolling data and average computing
            static float values[128];
            values[127] = timeStamps.back().m_microseconds;
            for (uint32_t i = 0; i < 128 - 1; i++) {
                values[i] = values[i + 1];
            }
            ImGui::PlotLines("", values, 128, 0, "GPU frame time (us)", 0.0f, 30000.0f, ImVec2(0, 80));
        }
    }
    ImGui::SliderFloat("Target Frametime in ms", &m_State.targetFrametime, 0.0f, 50.0f);
    ImGui::End();
    ImGui::Begin("HSR Sample", &opened);

    std::vector<char const *> scene_names;

    for (int i = 0; i < m_SceneNames.size(); i++) {
        scene_names.push_back(m_SceneNames[i].c_str());
    }

    if (ImGui::Combo("Model", (int *)&m_selectedScene, &scene_names[0], (int)scene_names.size())) {
        LoadScene(m_selectedScene);

        // bail out as we need to reload everything
        ImGui::End();
        ImGui::EndFrame();
        ImGui::NewFrame();
        return;
    }


    if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto viz_flag = [](uint32_t &flags, uint32_t bit, char const *name, char const *tooltip = NULL) {
            bool val = flags & bit;
            bool mod = ImGui::Checkbox(name, &val);
            if (tooltip && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(tooltip);
                ImGui::EndTooltip();
            }
            flags &= ~bit;
            flags |= (val ? bit : 0);
            return mod;
        };

        auto viz_flag_radio = [](uint32_t &flags, uint32_t bit, char const *name, int id, char const *tooltip = NULL) {
            bool val     = flags & bit;
            bool new_val = ImGui::RadioButton(name, val);
            if (tooltip && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(tooltip);
                ImGui::EndTooltip();
            }
            flags &= ~bit;
            flags |= (new_val ? bit : 0);
            return new_val != val;
        };

        bool debug_toggled = viz_flag(m_State.frameInfo.hsr_mask, HSR_FLAGS_SHOW_DEBUG_TARGET, "[DEBUG] Show Debug Target");

        if (m_State.frameInfo.hsr_mask & HSR_FLAGS_SHOW_DEBUG_TARGET) {
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            auto        wsize     = ImGui::GetWindowSize().x;
            {
                const ImVec2 p       = ImGui::GetCursorScreenPos();
                float        x       = p.x + 0.0f;
                float        y       = p.y + 0.0f;
                static float sz      = 12.0f;
                float        spacing = 6.0f;
                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), ImColor(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)));
                y += sz + spacing;
                ImGui::Dummy(ImVec2(sz, sz));
                ImGui::SameLine();
                ImGui::Text("- Screen Space successful rays");

                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), ImColor(ImVec4(1.0f, 0.0f, 1.0f, 1.0f)));
                y += sz + spacing;
                ImGui::Dummy(ImVec2(sz, sz));
                ImGui::SameLine();
                ImGui::Text("- Screen Space failed rays(Hybrid)");

                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), ImColor(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)));
                y += sz + spacing;
                ImGui::Dummy(ImVec2(sz, sz));
                ImGui::SameLine();
                ImGui::Text("- Screen Space failed rays(Terminated)");

                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), ImColor(ImVec4(0.0f, 0.0f, 1.0f, 1.0f)));
                y += sz + spacing;
                ImGui::Dummy(ImVec2(sz, sz));
                ImGui::SameLine();
                ImGui::Text("- DXR rays");
            }
            ImGui::Separator();
            int flag_list[] = {
                HSR_FLAGS_VISUALIZE_HIT_COUNTER,  //
                HSR_FLAGS_SHOW_REFLECTION_TARGET, //
                HSR_FLAGS_SHOW_INTERSECTION,      //
                HSR_FLAGS_VISUALIZE_VARIANCE,     //
                HSR_FLAGS_VISUALIZE_AVG_RADIANCE, //
                HSR_FLAGS_VISUALIZE_NUM_SAMPLES,  //
                HSR_FLAGS_VISUALIZE_REPROJECTION, //
                HSR_FLAGS_VISUALIZE_PRIMARY_RAYS, //
                // HSR_FLAGS_VISUALIZE_TRANSPARENT_QUERY, //
            };
            char const *flag_name_list[] = {
                "Visualize Hit Counter",        //
                "Show Reflection Target",       //
                "Show Intersection Results",    //
                "Visualize Variance",           //
                "Visualize Average Radiance",   //
                "Visualize Number of Samples",  //
                "Visualize Reprojected Target", //
                "Visualize Primary Rays",       //
                //"Visualize Transparent Query",  //
            };

            static int menu_id = 0;
            ImGui::Combo("Visualizer", &menu_id, flag_name_list, ARRAYSIZE(flag_name_list));
            for (auto flag : flag_list) {
                m_State.frameInfo.hsr_mask &= ~flag;
            }
            m_State.frameInfo.hsr_mask |= flag_list[menu_id];

            ImGui::Separator();
        }

        // Disable taa for debug view
        if (debug_toggled) {
            m_State.bTAA       = !(m_State.frameInfo.hsr_mask & HSR_FLAGS_SHOW_DEBUG_TARGET);
            m_State.bTAAJitter = !(m_State.frameInfo.hsr_mask & HSR_FLAGS_SHOW_DEBUG_TARGET);
        }

        if (ImGui::CollapsingHeader("Classification Config", ImGuiTreeNodeFlags_DefaultOpen)) {
            wrap_imgui("Global Roughness Threshold:", "Used to cutoff ray tracing and SSR", [&] { ImGui::SliderFloat("", &m_State.roughnessThreshold, -0.001f, 1.f); });
            wrap_imgui("RT Roughness Threshold:", "Used to cutoff rough pixels for Ray tracing",
                       [&] { ImGui::SliderFloat("", &m_State.frameInfo.rt_roughness_threshold, -0.001f, 1.f); });

            viz_flag(m_State.frameInfo.hsr_mask, HSR_FLAGS_SHADING_USE_SCREEN, "Don't reshade", "Grab radiance from screen space shaded image with possible artifacts");

            viz_flag(m_State.frameInfo.hsr_mask, HSR_FLAGS_USE_SCREEN_SPACE, "Enable Hybrid Reflections", "Enable Screen Space Hybridization");
        }
        // Make sure it's not used on pre dxr1.1 devices
        if (m_device.IsRT11Supported() == false) {
            m_State.frameInfo.hsr_mask &= ~HSR_FLAGS_USE_RAY_TRACING;
        }
       
        if (ImGui::CollapsingHeader("Resolution/Upscaling Config", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool update_size = false;
            update_size |= ImGui::Checkbox("Half Resolution Downsampling(1/4 area)", &m_State.bOptimizedDownsample);
            if (update_size && !m_State.bOptimizedDownsample) m_State.m_ReflectionResolutionMultiplier = 1.0f;
            if (!m_State.bOptimizedDownsample) {
                wrap_imgui("Reflection Resolution(Each dimension):", "", [&] { update_size |= ImGui::SliderFloat("", &m_State.m_ReflectionResolutionMultiplier, 0.1f, 1.0f); });
            }
            if (update_size) {
                UpdateReflectionResolution();
            }
            char const *filter_items[] = {
                "POINT",
                "BILINEAR",
                "FSR",
                "FSR+Edge correction",
            };
            if (m_State.m_ReflectionResolutionMultiplier != 1.0f) {
                wrap_imgui("Reflection Upscale Mode:", "", [&] { ImGui::Combo("", (int *)&m_State.frameInfo.reflections_upscale_mode, filter_items, 4); });

                if (m_State.frameInfo.reflections_upscale_mode > 2) {

                    wrap_imgui(
                        "FSR Roughness threshold:", "FSR is used for upsampling of regions with roughness less that this threshold(makes more difference for glossy surfaces)",
                        [&] { ImGui::SliderFloat("", &m_State.frameInfo.fsr_roughness_threshold, -0.01f, 1.01f); });
                }
            }
        }
    }

    ImGui::Text("'X' to show/hide GUI");
    ImGui::Text("'Z' to make screenshot");
    if (ImGui::CollapsingHeader("Magnifier", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGuiIO &              io                       = ImGui::GetIO();
        static constexpr float MAGNIFICATION_AMOUNT_MIN = 1.0f;
        static constexpr float MAGNIFICATION_AMOUNT_MAX = 32.0f;
        static constexpr float MAGNIFIER_RADIUS_MIN     = 0.01f;
        static constexpr float MAGNIFIER_RADIUS_MAX     = 0.85f;
        auto                   fnDisableUIStateBegin    = [](const bool &bEnable) {
            if (!bEnable) {
                // ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }
        };
        auto fnDisableUIStateEnd = [](const bool &bEnable) {
            if (!bEnable) {
                // ImGui::PopItemFlag();
                ImGui::PopStyleVar();
            }
        };
        // read in Magnifier pass parameters from the UI & app state
        MagnifierPS::PassParameters &params = m_State.magnifierParams;
        params.uImageHeight                 = m_Height;
        params.uImageWidth                  = m_Width;
        params.iMousePos[0]                 = m_State.bLockMagnifierPosition ? m_State.LockedMagnifiedScreenPositionX : static_cast<int>(io.MousePos.x);
        params.iMousePos[1]                 = m_State.bLockMagnifierPosition ? m_State.LockedMagnifiedScreenPositionY : static_cast<int>(io.MousePos.y);

        ImGui::Checkbox("Show Magnifier (M)", &m_State.bUseMagnifier);

        fnDisableUIStateBegin(m_State.bUseMagnifier);
        {
            // use a local bool state here to track locked state through the UI widget,
            // and then call ToggleMagnifierLockedState() to update the persistent state (m_State).
            // the keyboard input for toggling lock directly operates on the persistent state.
            const bool bIsMagnifierCurrentlyLocked = m_State.bLockMagnifierPosition;
            ImGui::Checkbox("Lock Position (L)", &m_State.bLockMagnifierPosition);
            const bool bWeJustLockedPosition = m_State.bLockMagnifierPosition && !bIsMagnifierCurrentlyLocked;
            if (bWeJustLockedPosition) {
                ToggleMagnifierLockedState(m_State, io);
            }
            ImGui::SliderFloat("Screen Size", &params.fMagnifierScreenRadius, MAGNIFIER_RADIUS_MIN, MAGNIFIER_RADIUS_MAX);
            ImGui::SliderFloat("Magnification", &params.fMagnificationAmount, MAGNIFICATION_AMOUNT_MIN, MAGNIFICATION_AMOUNT_MAX);
            ImGui::SliderInt("OffsetX", &params.iMagnifierOffset[0], -(int)m_Width, m_Width);
            ImGui::SliderInt("OffsetY", &params.iMagnifierOffset[1], -(int)m_Height, m_Height);
        }
        fnDisableUIStateEnd(m_State.bUseMagnifier);
    }
    ImGui::End();
}

static void ToggleBool(bool &b) { b = !b; }

void HSRSample::HandleInput() {
    // If the mouse was not used by the GUI then it's for the camera
    //

    ImGuiIO &io = ImGui::GetIO();

    auto fnIsKeyTriggered = [&io](char key) { return io.KeysDown[key] && io.KeysDownDuration[key] == 0.0f; };

    // Handle Keyboard/Mouse input here

    /* MAGNIFIER CONTROLS */
    if (fnIsKeyTriggered('L')) ToggleMagnifierLockedState(m_State, io);
    if (fnIsKeyTriggered('M') || io.MouseClicked[2]) ToggleBool(m_State.bUseMagnifier); // middle mouse / M key toggles magnifier

    if (io.MouseClicked[1] && m_State.bUseMagnifier) // right mouse click
    {
        ToggleMagnifierLockedState(m_State, io);
    }

    static std::chrono::system_clock::time_point last = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point        now  = std::chrono::system_clock::now();
    std::chrono::duration<double>                diff = now - last;
    last                                              = now;

    io.DeltaTime = static_cast<float>(diff.count());

    if (ImGui::IsKeyPressed('X')) {
        m_bShowUI = !m_bShowUI;
        ShowCursor(m_bShowUI);
    }

    if (ImGui::IsKeyPressed('Y')) {
        m_Node->Recompile();
    }

    if (ImGui::IsKeyPressed('Z')) {
        static int g_screenshot_cnt = 0;
        m_State.screenshotName      = std::string("screenshot_") + std::to_string(g_screenshot_cnt++) + std::string(".png");
    }

    if (io.WantCaptureMouse == false || !m_bShowUI) {
        if ((io.KeyCtrl == false) && (io.MouseDown[0] == true)) {
            m_Yaw -= io.MouseDelta.x / 100.f;
            m_Pitch += io.MouseDelta.y / 100.f;
        }
    }
    // Choose camera movement depending on setting
    //
    if (m_State.isBenchmarking) {

    } else if (m_CameraControlSelected == 0) {
        //  WASD
        //
        if (m_State.bAnimateCamera) {
            io.KeysDown['D'] = std::sin(m_Time * 18.6f) > 0.0f;
            io.KeysDown['A'] = std::sin(m_Time * 18.6f) < 0.0f;
        }
        m_State.camera.UpdateCameraWASD(m_Yaw, m_Pitch, io.KeysDown, io.DeltaTime);
    } else if (m_CameraControlSelected == 1) {
        //  Orbiting
        //
        if (m_State.bAnimateCamera) {
            m_Yaw += (float)((m_DeltaTime / 1000.0f) * std::sin(m_Time * 2.5f));
            m_Pitch += (float)((m_DeltaTime / 1000.0f) * std::cos(m_Time * 2.5f));
        }

        m_Distance -= (float)io.MouseWheel / 3.0f;
        m_Distance = std::max<float>(m_Distance, 0.1f);

        bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);
        m_State.camera.UpdateCameraPolar(m_Yaw, m_Pitch, panning ? -io.MouseDelta.x / 100.0f : 0.0f, panning ? io.MouseDelta.y / 100.0f : 0.0f, m_Distance, io.KeysDown,
                                         io.DeltaTime);
    } else {
        // Use a camera from the GLTF
        //
        m_pGltfLoader->GetCamera(m_CameraControlSelected - 2, &m_State.camera);
        m_Yaw   = m_State.camera.GetYaw();
        m_Pitch = m_State.camera.GetPitch();
    }
    if (m_State.bTAAJitter) {
        static uint32_t seed = 0;
        m_State.camera.SetProjectionJitter(m_Width, m_Height, seed);
    }
}

void HSRSample::LoadScene(int sceneIndex) {
    json scene = m_JsonConfigFile["scenes"][sceneIndex];
    if (m_pGltfLoader != NULL) {
        // free resources, unload the current scene, and load new scene...
        m_device.GPUFlush();

        m_Node->UnloadScene();
        m_Node->OnDestroyWindowSizeDependentResources();
        m_Node->OnDestroy();
        m_pGltfLoader->Unload();
        m_Node->OnCreate(&m_device, &m_swapChain);
        m_Node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, m_ReflectionWidth, m_ReflectionHeight, &m_State);
    }

    delete (m_pGltfLoader);
    m_pGltfLoader = new GLTFCommon();

    if (m_pGltfLoader->Load(scene["directory"], scene["filename"]) == false) {
        MessageBox(NULL, "The selected model couldn't be found, please check the documentation", "Cauldron Panic!", MB_ICONERROR);
        exit(0);
    }

    // Load the UI settings, and also some defaults cameras and lights, in case the GLTF has none
    {
#define LOAD(j, key, val) val = j.value(key, val)

        // global settings
        LOAD(scene, "toneMapper", m_State.toneMapper);
        LOAD(scene, "skyDomeType", m_State.skyDomeType);
        LOAD(scene, "exposure", m_State.exposure);
        LOAD(scene, "emmisiveFactor", m_State.emmisiveFactor);
        LOAD(scene, "skyDomeType", m_State.skyDomeType);

        // default light
        m_State.lightIntensity                     = scene.value("intensity", 1.0f);
        m_State.SunLightIntensity                  = scene.value("SunLightIntensity", m_State.SunLightIntensity);
        m_State.FlashLightIntensity                = scene.value("FlashLightIntensity", m_State.FlashLightIntensity);
        m_State.depthBufferThickness               = scene.value("depthBufferThickness", m_State.depthBufferThickness);
        m_State.roughnessThreshold                 = scene.value("roughnessThreshold", m_State.roughnessThreshold);
        m_State.frameInfo.rt_roughness_threshold   = scene.value("rt_roughness_threshold", m_State.frameInfo.rt_roughness_threshold);
        m_State.frameInfo.ssr_confidence_threshold = scene.value("ssr_confidence_threshold", m_State.frameInfo.ssr_confidence_threshold);
        m_State.frameInfo.history_clip_weight      = scene.value("history_clip_weight", m_State.frameInfo.history_clip_weight);
        m_State.cameraFOV                          = scene.value("cameraFOV", m_State.cameraFOV);
        m_CameraControlSelected                    = scene.value("CameraControlSelected", m_CameraControlSelected);
        m_State.bFlashLight                        = scene.value("enableFlashlight", m_State.bFlashLight);
        m_State.iblFactor                          = scene.value("iblFactor", m_State.iblFactor);
        m_State.bAttachFlashLight                  = scene.value("attachFlashLight", m_State.bAttachFlashLight);
        m_State.frameInfo.reflection_factor        = scene.value("reflectionFactor", m_State.frameInfo.reflection_factor);
        m_State.sunProjectionWidth                 = scene.value("sunProjectionWidth", m_State.sunProjectionWidth);
        m_State.sunPhi                             = (float)scene.value("sunPhi", -2.611);
        m_State.sunTheta                           = (float)scene.value("sunTheta", 0.055);
        bool reshade                               = scene.value("reshade", true);

        m_State.frameInfo.hsr_mask &= ~HSR_FLAGS_SHADING_USE_SCREEN;
        if (!reshade) m_State.frameInfo.hsr_mask |= HSR_FLAGS_SHADING_USE_SCREEN;

        UpdateSun(m_State);

        math::Vector3 default_from = math::Vector3(6.12422, 0.892025, -7.41218);
        math::Vector3 default_to   = default_from - 10.0 * math::Vector3(0.7092, -0.0799147, -0.700464);
        math::Vector4 from = GetVector(GetElementJsonArray(scene, "cameraFrom", {(float)default_from.getX(), (float)default_from.getY(), (float)default_from.getZ()}));
        math::Vector4 to   = GetVector(GetElementJsonArray(scene, "cameraTo", {(float)default_to.getX(), (float)default_to.getY(), (float)default_to.getZ()}));

        if (m_CameraControlSelected == 0) {
            m_State.camera.LookAt(from, to);
            m_Yaw   = m_State.camera.GetYaw();
            m_Pitch = m_State.camera.GetPitch();
        } else {
            // Set initial look at
            m_State.camera.LookAt(from, to);

            m_Yaw      = -2.4f;
            m_Pitch    = 0.3f;
            m_Distance = 1.3f;

            m_Yaw      = scene.value("cameraYaw", m_Yaw);
            m_Pitch    = scene.value("cameraPitch", m_Pitch);
            m_Distance = scene.value("cameraDistance", m_Distance);
        }

        m_State.camera.SetFov(m_State.cameraFOV, m_Width, m_Height, 0.01f, 100.0f);

        math::Vector4 flashfrom = GetVector(GetElementJsonArray(scene, "flashFrom", {0.614431, 0.194285, -1.12908}));
        math::Vector4 flashto   = GetVector(GetElementJsonArray(scene, "flashTo", {0.472659, 0.149438, -0.868483}));

        m_State.flashlightCamera = m_State.camera;
        m_State.flashlightCamera.LookAt(flashfrom, flashto);

        // indicate the mainloop we started loading a GLTF and it needs to load the rest (textures and geometry)
        m_bLoadingScene = true;
    }
}

//--------------------------------------------------------------------------------------
//
// OnUpdateDisplay
//
//--------------------------------------------------------------------------------------
void HSRSample::OnUpdateDisplay() {}

//--------------------------------------------------------------------------------------
//
// OnResize
//
//--------------------------------------------------------------------------------------
void HSRSample::OnResize(bool resizeRender) {
    // Flush GPU
    //
    m_device.GPUFlush();
    m_ReflectionWidth  = max(128u, uint32_t(m_Width * m_State.m_ReflectionResolutionMultiplier));
    m_ReflectionHeight = max(128u, uint32_t(m_Height * m_State.m_ReflectionResolutionMultiplier));
    // If resizing but no minimizing
    //
    if (m_Width > 0 && m_Height > 0) {
        if (m_Node != NULL) {
            m_Node->OnDestroyWindowSizeDependentResources();
        }
        m_swapChain.OnDestroyWindowSizeDependentResources();
    }

    // if resizing but not minimizing the recreate it with the new size
    //
    if (m_Width > 0 && m_Height > 0) {
        m_swapChain.OnCreateWindowSizeDependentResources(m_Width, m_Height, false, DISPLAYMODE_SDR);
        if (m_Node != NULL) {
            m_Node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height, m_ReflectionWidth, m_ReflectionHeight, &m_State);
        }
    }

    m_State.camera.SetFov(m_State.cameraFOV, m_Width, m_Height, 0.01f, 100.0f);
}

void HSRSample::OnParseCommandLine(LPSTR lpCmdLine, uint32_t *pWidth, uint32_t *pHeight) {
    // First load configuration
    std::ifstream f("config.json");
    if (!f) {
        MessageBox(NULL, "Config file not found!\n", "Cauldron Panic!", MB_ICONERROR);
        exit(-1);
    }
    f >> m_JsonConfigFile;

    // Parse command line and override the config file
    try {
        if (strlen(lpCmdLine) > 0) {
            auto j3 = json::parse(lpCmdLine);
            m_JsonConfigFile.merge_patch(j3);
        }
    } catch (json::parse_error) {
        Trace("Error parsing commandline\n");
        exit(0);
    }

    // Set values
    *pWidth               = m_JsonConfigFile.value("width", 1920);
    *pHeight              = m_JsonConfigFile.value("height", 1080);
    m_deferredBenchEnable = m_JsonConfigFile.value("benchmark", false);

    m_State.frameInfo.hsr_mask = HSR_FLAGS_USE_HIT_COUNTER |   //
                                 HSR_FLAGS_APPLY_REFLECTIONS | //
                                 HSR_FLAGS_USE_RAY_TRACING |   //
                                 // HSR_FLAGS_RESOLVE_TRANSPARENT |   //
                                 HSR_FLAGS_VISUALIZE_HIT_COUNTER | //
                                 // HSR_FLAGS_RAY_TRACING_USE_SCREEN | //
                                 HSR_FLAGS_USE_SCREEN_SPACE | //
                                 0;
    if (m_JsonConfigFile.value("no_sw", false)) {
        m_State.frameInfo.hsr_mask &= ~HSR_FLAGS_USE_SCREEN_SPACE;
    }
    if (m_JsonConfigFile.value("no_hw", false)) {
        m_State.frameInfo.hsr_mask &= ~HSR_FLAGS_USE_RAY_TRACING;
    }
    m_BenchNumLoops                          = m_JsonConfigFile.value("benchmark_num_loops", 2);
    m_selectedScene                          = m_JsonConfigFile.value("scene", 0);
    m_State.m_ReflectionResolutionMultiplier = (float)m_JsonConfigFile.value("reflection_resolution_multiplier", 1.0);
    m_State.bOptimizedDownsample             = m_JsonConfigFile.value("reflection_optimized_half_resolution", false);
    m_State.bWeapon                          = m_JsonConfigFile.value("spoon", false);
    m_State.bFlashLight                      = m_JsonConfigFile.value("flashlight", true);
    UpdateReflectionResolution();
}

//--------------------------------------------------------------------------------------
//
// OnRender, updates the state from the UI, animates, transforms and renders the scene
//
//--------------------------------------------------------------------------------------
void HSRSample::OnRender() {
    // Get timings
    //
    double timeNow  = MillisecondsNow();
    m_DeltaTime     = timeNow - m_LastFrameTime;
    m_LastFrameTime = timeNow;

    // Build UI and set the scene state. Note that the rendering of the UI happens later.
    //
    ImGUI_UpdateIO();
    ImGui::NewFrame();

    if (m_bLoadingScene) {
        static int loadingStage = 0;
        // LoadScene needs to be called a number of times, the scene is not fully loaded until it returns 0
        // This is done so we can display a progress bar when the scene is loading
        loadingStage = m_Node->LoadScene(m_pGltfLoader, loadingStage);
        if (loadingStage == 0) {
            m_Time          = 0;
            m_frame_index   = 0;
            m_bLoadingScene = false;
            const json &j3  = m_pGltfLoader->j3;
            if (j3.find("meshes") != j3.end()) {
                const json &nodes = j3["nodes"];
                for (uint32_t i = 0; i < nodes.size(); i++) {
                    const json &node = nodes[i];
                    std::string name = GetElementString(node, "name", "unnamed");

                    if (name == "weapon") {
                        weapon_id = i;
                    }
                }
            }
        }
    } else {
        if (m_bShowUI) {
            BuildUI();
        }

        if (!m_bLoadingScene) {
            HandleInput();
        }
        if (m_deferredBenchEnable && m_frame_index > 10) {
            m_State.isBenchmarking = true;
            m_deferredBenchEnable  = false;
        }
    }

    // Update sun position
    UpdateSun(m_State);

    // Set animation time
    //
    if (m_bPlay) {
        m_Time += (float)m_DeltaTime / 1000.0f;
    }

    if (!m_bLoadingScene && m_State.isBenchmarking && m_Node->IsReady()) {
        if (m_BenchFileDump.is_open() == false) {
            m_Time             = 0.0f;
            m_frame_index      = 0;
            m_BenchLoopCounter = 0;
            m_BenchLoopPercent = 0;
            m_BenchLoopTime    = 0;
            {
                std::stringstream ss;
                ss << "HSR_Bench_";
                std::string deviceName, driverVersion;
                m_device.GetDeviceInfo(&deviceName, &driverVersion);
                ss << m_Node->getWidth() << "_" << m_Node->getHeight() << "_";
                if (m_State.frameInfo.hsr_mask & HSR_FLAGS_USE_RAY_TRACING) {
                    ss << "hw_";
                }
                if (m_State.frameInfo.hsr_mask & HSR_FLAGS_USE_SCREEN_SPACE) {
                    ss << "sw_";
                }
                ss << int(m_State.m_ReflectionResolutionMultiplier * 100.0f) << "%_";
                ss << deviceName << ".csv";
                m_benchName = ss.str();
            }
            m_BenchFileDump.open(m_benchName, std::ofstream::out);
            if (!m_BenchFileDump.is_open()) {
                static wchar_t msg[0x100];
                std::wstring   wstr(m_benchName.c_str(), m_benchName.c_str() + m_benchName.size());
                wsprintfW(msg, L"Couldn't open dump file(%s) to save benchmark results", wstr.c_str());
                ShowErrorMessageBox(msg);
                exit(1);
            }
            assert(m_BenchFileDump.is_open());
            std::stringstream ss;
            ss << "Frame Index"
               << "; ";
            ss << "Animation Percent"
               << "; ";
            ss << "Animation Time"
               << "; ";
            for (int i = 1; i < (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT; i++) {
                ss << GetTimestampName(i) << "; ";
            }
            ss << "Number of SW Rays"
               << "; ";
            ss << "Number of HW Rays"
               << "; ";
            ss << "Number of Hybrid Rays"
               << "; ";
            ss << "\n";
            m_BenchFileDump << ss.str();
        }
        // Animate camera
        m_BenchCameraTime                     = m_Time;
        static double s_bench_camera_states[] = {
            -0.61501,     3.49278,    -1.83146,   -0.945744,   0.17903,     -0.271138,  -0.366512, 3.43335,     -1.45999,   -0.971418, 0.159318,    -0.17597,   0.536377,
            3.3815,       -0.700243,  -0.999908,  -0.00999981, -0.00920395, 1.34733,    3.43615,   -0.291151,   -0.990216,  -0.139543, 0.000787652, 2.22732,    3.5694,
            -0.105629,    -0.987045,  -0.159318,  -0.0189581,  2.68974,     3.65475,    0.326454,  -0.979855,   -0.198669,  0.0203794, 3.30232,     3.74163,    0.956051,
            -0.991379,    -0.129634,  -0.0190413, 4.53209,     3.80273,     2.00967,    -0.999118, -0.00999982, 0.040782,   6.38764,   3.68364,     2.2196,     -0.945312,
            0.179029,     0.272643,   8.06598,    3.4788,      2.40337,     -0.22542,   0.149438,  0.962733,    9.86858,    3.46339,   1.71658,     0.763716,   -0.0399894,
            0.644313,     10.0783,    3.45569,    1.62152,     0.983258,    -0.0299956, 0.179735,  10.5847,     3.46215,    0.53695,   0.983189,    0.139543,   -0.117755,
            10.3324,      3.45851,    -0.564236,  0.980737,    0.188859,    0.0498638,  9.18135,   2.95263,     -1.56681,   0.292327,  0.522687,    0.800839,   8.95334,
            2.32781,      -2.41488,   -0.559828,  0.659384,    0.501802,    9.00976,    2.14132,   -2.86396,    -0.868315,  0.488177,  0.0878194,   9.90266,    1.79922,
            -2.88696,     -0.958074,  0.227978,   -0.173553,   9.84712,     1.78656,    -2.99189,  -0.87457,    0.169182,   -0.454427, 8.83349,     2.00996,    -3.26717,
            -0.914024,    0.198669,   -0.35368,   7.55797,     2.14737,     -3.65629,   -0.92349,  0.139543,    -0.357343,  6.16599,   2.38609,     -4.1975,    -0.916997,
            0.21823,      -0.333904,  4.65209,    2.95359,     -4.38887,    -0.909287,  0.398609,  0.119613,    4.03584,    3.30187,   -4.09329,    -0.470963,  0.488177,
            0.734764,     4.92252,    2.87831,    -4.48996,    -0.321894,   0.49688,    0.805912,  4.96962,     2.33481,    -5.62967,  -0.355576,   0.40776,    0.84101,
            4.86523,      1.8264,     -7.02659,   -0.574146,   0.40776,     0.709992,   5.8171,    1.26159,     -8.45272,   -0.596973, 0.227977,    0.769188,   6.22904,
            1.10287,      -8.99722,   -0.577403,  0.198669,    0.791919,    6.65415,    1.02648,   -9.78676,    -0.960301,  0.17903,   -0.21394,    7.6406,     0.966826,
            -9.83864,     0.280325,   0.169182,   -0.944878,   6.67937,     0.843547,   -9.30629,  0.546663,    0.00999972, -0.837293, 6.48158,     0.840352,   -9.016,
            0.486689,     -0.0199988, -0.873347,  5.87092,     0.900982,    -7.81815,   0.579013,  -0.0499793,  -0.813785,  5.35436,   0.920755,    -6.37948,   0.975375,
            -9.17005e-08, -0.220552,  4.29538,    0.896606,    -4.83539,    0.936307,   0.0299954, 0.349898,    3.96143,    0.894815,  -4.16748,    0.978474,   0.00999971,
            0.206129,     3.24283,    1.01444,    -3.91378,    0.652186,    -0.247404,  -0.716551, 1.74549,     1.19733,    -3.00822,  -0.716704,   -0.0699429, -0.693861,
            0.0850243,    1.17471,    -1.973,     -0.967961,   -0.109778,   -0.225831,  -0.415511, 1.12537,     -1.63756,   -0.966844, -0.119712,   -0.22557,   0.372078,
            1.50253,      -1.84572,   -0.750903,  -0.597195,   -0.281961,   1.12339,    2.10404,   -1.54045,    -0.76646,   -0.597195, -0.236427,   1.86501,    2.70353,
            -0.276091,    -0.928096,  -0.352274,  -0.120587,   1.9705,      2.79296,    -0.433438, -0.983949,   -0.0998335, -0.14791,  1.32905,     2.7194,     -1.09846,
            -0.983949,    -0.0998336, -0.14791,   -0.29935,    2.55265,     -1.52865,   -0.964267, -0.0998336,  -0.245402,  -1.2094,   2.45927,     -1.72634,   -0.964267,
            -0.0998336,   -0.245402,
        };
        int num_pnts       = ARRAYSIZE(s_bench_camera_states) / 6;
        m_BenchLoopCounter = int(m_BenchCameraTime) / num_pnts;
        if (m_BenchLoopCounter >= m_BenchNumLoops) {
            if (m_BenchFileDump.is_open()) m_BenchFileDump.close();
            exit(0);
        }
        int p0          = (int(m_BenchCameraTime) % num_pnts) * 6;
        int p1          = (int(m_BenchCameraTime + 1.0f) % num_pnts) * 6;
        m_BenchLoopTime = m_Time;
        while (m_BenchLoopTime > num_pnts) m_BenchLoopTime -= float(num_pnts);
        m_BenchLoopPercent = int(100.0f * m_BenchLoopTime / float(num_pnts));

        // Simple linear interpolation of position and direction.
        float               t0 = m_BenchCameraTime - int(m_BenchCameraTime);
        Vectormath::Vector4 pos0{(float)s_bench_camera_states[p0 + 0], (float)s_bench_camera_states[p0 + 1], (float)s_bench_camera_states[p0 + 2], 0.0f};
        Vectormath::Vector4 dir0{(float)s_bench_camera_states[p0 + 3], (float)s_bench_camera_states[p0 + 4], (float)s_bench_camera_states[p0 + 5], 0.0f};
        Vectormath::Vector4 pos1{(float)s_bench_camera_states[p1 + 0], (float)s_bench_camera_states[p1 + 1], (float)s_bench_camera_states[p1 + 2], 0.0f};
        Vectormath::Vector4 dir1{(float)s_bench_camera_states[p1 + 3], (float)s_bench_camera_states[p1 + 4], (float)s_bench_camera_states[p1 + 5], 0.0f};
        Vectormath::Vector4 pos = Vectormath::SSE::lerp(t0, pos0, pos1);
        Vectormath::Vector4 dir = Vectormath::SSE::normalize(Vectormath::SSE::lerp(t0, dir0, dir1));
        m_State.camera.LookAt(pos, pos - dir * 10.0f);

        std::stringstream ss;
        ss << m_frame_index << "; ";
        ss << m_BenchLoopPercent << "; ";
        ss << m_BenchLoopTime << "; ";
        for (int i = 1; i < (int)HSRTimestampQuery::TIMESTAMP_QUERY_COUNT; i++) {
            ss << std::fixed << std::setprecision(2) << m_State.hsr_timestamps[i] << "; ";
        }
        ss << std::fixed << std::setprecision(2) << m_State.m_numSWRays << "; ";
        ss << std::fixed << std::setprecision(2) << m_State.m_numHWRays << "; ";
        ss << std::fixed << std::setprecision(2) << m_State.m_numHYRays << "; ";
        ss << "\n";
        std::string str = ss.str();
        for (auto &c : str)
            if (c == '.') c = ',';
        m_BenchFileDump << str;
    }

    // Animate and transform the scene
    //
    if (m_pGltfLoader) {
        for (uint32_t anim_id = 0; anim_id < 32; anim_id++) m_pGltfLoader->SetAnimationTime(anim_id, m_Time);
        // Animate mellee weapon
        if (weapon_id >= 0 && m_State.bWeapon) {
            std::vector<tfNode> &pNodes         = m_pGltfLoader->m_nodes;
            math::Matrix4 *      pNodesMatrices = m_pGltfLoader->m_animatedMats.data();
            Vectormath::Vector3  eye            = m_State.camera.GetPosition().getXYZ() + m_State.camera.GetDirection().getXYZ() * weapon_offset_z;
            Vectormath::Vector3  look           = m_State.camera.GetPosition().getXYZ() + m_State.camera.GetDirection().getXYZ() * -2.0f;
            weapon_pos                          = weapon_pos + (eye - weapon_pos) * (1.0f - std::expf(50.0f * -(float)m_DeltaTime / 1000.0f));
            weapon_tip_pos                      = weapon_tip_pos + (look - weapon_tip_pos) * (1.0f - std::expf(50.0f * -(float)m_DeltaTime / 1000.0f));
            pNodesMatrices[weapon_id] =
                Vectormath::inverse(Vectormath::Matrix4::lookAt(Vectormath::Point3(weapon_pos), Vectormath::Point3(weapon_tip_pos), Vectormath::Vector3(0.0f, 1.0f, 0.0f))) *
                Vectormath::Matrix4::translation(Vectormath::Vector3(weapon_offset_x, weapon_offset_y, 0.0f)) * //
                Vectormath::Matrix4::rotation(-3.141592f / 2.0f, Vectormath::Vector3(1.0f, 0.0f, 0.0f)) *       //
                Vectormath::Matrix4::rotation(-3.141592f / 2.0f, Vectormath::Vector3(0.0f, 0.0f, 1.0f)) *       //
                Vectormath::Matrix4::scale(Vectormath::Vector3(weapon_offset_scale, weapon_offset_scale, weapon_offset_scale));
        }
        m_pGltfLoader->TransformScene(0, Vectormath::Matrix4::identity());
    }

    m_State.frameInfo.base_width        = m_Width;
    m_State.frameInfo.base_height       = m_Height;
    m_State.frameInfo.reflection_width  = m_ReflectionWidth;
    m_State.frameInfo.reflection_height = m_ReflectionHeight;

    m_State.time      = m_Time;
    m_State.deltaTime = (float)m_DeltaTime / 1000.0f;

    // Do Render frame using AFR
    //
    m_Node->OnRender(&m_State, &m_swapChain);

#ifdef _DEBUG
    // workaround for hang in device debug layer.
    m_device.GPUFlush();
#endif
    m_swapChain.Present();
    m_frame_index++;
}

//--------------------------------------------------------------------------------------
//
// WinMain
//
//--------------------------------------------------------------------------------------
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    LPCSTR Name = "Hybrid Stochastic Reflections Sample DX12 v1.0";
    // create new sample
    return RunFramework(hInstance, lpCmdLine, nCmdShow, new HSRSample(Name));
}