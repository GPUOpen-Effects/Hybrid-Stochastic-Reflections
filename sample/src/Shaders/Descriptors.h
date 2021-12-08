
//////////////////////////////////////////////////////////////
// Automatically generated file, please see genbindtable.py //
//////////////////////////////////////////////////////////////

#define DX12_PUSH_CONSTANTS_REGISTER 512
#define DX12_PUSH_CONSTANTS register(b512, space0)

#define DX12REGISTER_INNER(T, N) T##N
#define DX12REGISTER(T, N) DX12REGISTER_INNER(T, N)

#define HLSL_INIT_GLOBAL_BINDING_TABLE(SPACE_ID) \
[[vk::binding(0, SPACE_ID)]]  RaytracingAccelerationStructure g_TLAS[3] : register(DX12REGISTER(t, 0), space##SPACE_ID); \
[[vk::binding(1, SPACE_ID)]]  Texture2D<float4> g_textures[623] : register(DX12REGISTER(t, 3), space##SPACE_ID); \
[[vk::binding(2, SPACE_ID)]]  Texture2D<min16float> g_texturesfp16[4] : register(DX12REGISTER(t, 626), space##SPACE_ID); \
[[vk::binding(3, SPACE_ID)]]  Texture2D<min16float3> g_texturesfp16x3[3] : register(DX12REGISTER(t, 630), space##SPACE_ID); \
[[vk::binding(4, SPACE_ID)]]  TextureCube<float4> g_ctextures[5] : register(DX12REGISTER(t, 633), space##SPACE_ID); \
[[vk::binding(5, SPACE_ID)]]  Texture2D<uint> g_utextures[1] : register(DX12REGISTER(t, 638), space##SPACE_ID); \
[[vk::binding(6, SPACE_ID)]]  RWTexture2D<float4> g_rw_textures[10] : register(DX12REGISTER(u, 0), space##SPACE_ID); \
[[vk::binding(7, SPACE_ID)]]  RWTexture2D<min16float> g_rw_texturesfp16[4] : register(DX12REGISTER(u, 10), space##SPACE_ID); \
[[vk::binding(8, SPACE_ID)]]  RWTexture2D<min16float3> g_rw_texturesfp16x3[3] : register(DX12REGISTER(u, 14), space##SPACE_ID); \
[[vk::binding(9, SPACE_ID)]]  RWTexture2DArray<float4> g_rw_atextures[384] : register(DX12REGISTER(u, 17), space##SPACE_ID); \
[[vk::binding(10, SPACE_ID)]]  RWTexture2D<uint> g_rw_utextures[1] : register(DX12REGISTER(u, 401), space##SPACE_ID); \
[[vk::binding(11, SPACE_ID)]]  RWByteAddressBuffer g_rw_buffers[23] : register(DX12REGISTER(u, 402), space##SPACE_ID); \
[[vk::binding(12, SPACE_ID)]]  SamplerState g_samplers[3] : register(DX12REGISTER(s, 0), space##SPACE_ID); \
[[vk::binding(13, SPACE_ID)]]  SamplerComparisonState g_cmp_samplers[1] : register(DX12REGISTER(s, 3), space##SPACE_ID); \
[[vk::binding(14, SPACE_ID)]]  ConstantBuffer<FrameInfo> g_frame_info_cb[1] : register(DX12REGISTER(b, 0), space##SPACE_ID); \

#define HLSL_INIT_GLOBAL_BINDING_TABLE_COHERENT(SPACE_ID) \
[[vk::binding(0, SPACE_ID)]]  RaytracingAccelerationStructure g_TLAS[3] : register(DX12REGISTER(t, 0), space##SPACE_ID); \
[[vk::binding(1, SPACE_ID)]]  Texture2D<float4> g_textures[623] : register(DX12REGISTER(t, 3), space##SPACE_ID); \
[[vk::binding(2, SPACE_ID)]]  Texture2D<min16float> g_texturesfp16[4] : register(DX12REGISTER(t, 626), space##SPACE_ID); \
[[vk::binding(3, SPACE_ID)]]  Texture2D<min16float3> g_texturesfp16x3[3] : register(DX12REGISTER(t, 630), space##SPACE_ID); \
[[vk::binding(4, SPACE_ID)]]  TextureCube<float4> g_ctextures[5] : register(DX12REGISTER(t, 633), space##SPACE_ID); \
[[vk::binding(5, SPACE_ID)]]  Texture2D<uint> g_utextures[1] : register(DX12REGISTER(t, 638), space##SPACE_ID); \
[[vk::binding(6, SPACE_ID)]]  globallycoherent RWTexture2D<float4> g_rw_textures[10] : register(DX12REGISTER(u, 0), space##SPACE_ID); \
[[vk::binding(7, SPACE_ID)]]  globallycoherent RWTexture2D<min16float> g_rw_texturesfp16[4] : register(DX12REGISTER(u, 10), space##SPACE_ID); \
[[vk::binding(8, SPACE_ID)]]  globallycoherent RWTexture2D<min16float3> g_rw_texturesfp16x3[3] : register(DX12REGISTER(u, 14), space##SPACE_ID); \
[[vk::binding(9, SPACE_ID)]]  globallycoherent RWTexture2DArray<float4> g_rw_atextures[384] : register(DX12REGISTER(u, 17), space##SPACE_ID); \
[[vk::binding(10, SPACE_ID)]]  globallycoherent RWTexture2D<uint> g_rw_utextures[1] : register(DX12REGISTER(u, 401), space##SPACE_ID); \
[[vk::binding(11, SPACE_ID)]]  globallycoherent RWByteAddressBuffer g_rw_buffers[23] : register(DX12REGISTER(u, 402), space##SPACE_ID); \
[[vk::binding(12, SPACE_ID)]]  SamplerState g_samplers[3] : register(DX12REGISTER(s, 0), space##SPACE_ID); \
[[vk::binding(13, SPACE_ID)]]  SamplerComparisonState g_cmp_samplers[1] : register(DX12REGISTER(s, 3), space##SPACE_ID); \
[[vk::binding(14, SPACE_ID)]]  ConstantBuffer<FrameInfo> g_frame_info_cb[1] : register(DX12REGISTER(b, 0), space##SPACE_ID); \

#define GDT_TLAS_OPAQUE_SLOT 0
// RaytracingAccelerationStructure g_opaque; // Acceleration structure that contains only opaque surfaces 
#define g_opaque g_TLAS[GDT_TLAS_OPAQUE_SLOT]
#define GDT_TLAS_TRANSPARENT_SLOT 1
// RaytracingAccelerationStructure g_transparent; // Acceleration structure that contains all surfaces with FORCE_TRANSPARENT 
#define g_transparent g_TLAS[GDT_TLAS_TRANSPARENT_SLOT]
#define GDT_TLAS_GLOBAL_SLOT 2
// RaytracingAccelerationStructure g_global; // Acceleration structure that contains all surfaces with FORCE_OPAQUE 
#define g_global g_TLAS[GDT_TLAS_GLOBAL_SLOT]
#define GDT_TEXTURES_HIZ_SLOT 0
// Texture2D<float4> g_hiz; // Current HIZ pyramid in reflection target resolution 
#define g_hiz g_textures[GDT_TEXTURES_HIZ_SLOT]
// Current GBuffer in reflection target resolution.
#define GDT_TEXTURES_DECAL_ALBEDO_SLOT 1
// Texture2D<float4> g_decal_albedo; // Current GBuffer/albedo in reflection target resolution 
#define g_decal_albedo g_textures[GDT_TEXTURES_DECAL_ALBEDO_SLOT]
#define GDT_TEXTURES_GBUFFER_ALBEDO_SLOT 2
// Texture2D<float4> g_gbuffer_albedo; // Current GBuffer/normal in reflection target resolution 
#define g_gbuffer_albedo g_textures[GDT_TEXTURES_GBUFFER_ALBEDO_SLOT]
#define GDT_TEXTURES_GBUFFER_NORMAL_SLOT 3
// Texture2D<float4> g_gbuffer_normal; // Current GBuffer/normal in reflection target resolution 
#define g_gbuffer_normal g_textures[GDT_TEXTURES_GBUFFER_NORMAL_SLOT]
#define GDT_TEXTURES_GBUFFER_ROUGHNESS_SLOT 4
// Texture2D<float4> g_gbuffer_roughness; // Current GBuffer/specular_roughness in reflection target resolution 
#define g_gbuffer_roughness g_textures[GDT_TEXTURES_GBUFFER_ROUGHNESS_SLOT]
#define GDT_TEXTURES_GBUFFER_DEPTH_SLOT 5
// Texture2D<float4> g_gbuffer_depth; // Current GBuffer/depth in reflection target resolution 
#define g_gbuffer_depth g_textures[GDT_TEXTURES_GBUFFER_DEPTH_SLOT]
#define GDT_TEXTURES_MOTION_VECTOR_SLOT 6
// Texture2D<float4> g_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
#define g_motion_vector g_textures[GDT_TEXTURES_MOTION_VECTOR_SLOT]
#define GDT_TEXTURES_EXTRACTED_ROUGHNESS_SLOT 7
// Texture2D<float4> g_extracted_roughness; // Current extracted GBuffer/roughness in reflection target resolution 
#define g_extracted_roughness g_textures[GDT_TEXTURES_EXTRACTED_ROUGHNESS_SLOT]
#define GDT_RW_TEXTURES_EXTRACTED_ROUGHNESS_SLOT 0
// RWTexture2D<float4> g_rw_extracted_roughness; // Current extracted GBuffer/roughness in reflection target resolution 
#define g_rw_extracted_roughness g_rw_textures[GDT_RW_TEXTURES_EXTRACTED_ROUGHNESS_SLOT]
#define GDT_RW_TEXTURES_GBUFFER_ALBEDO_SLOT 1
// RWTexture2D<float4> g_rw_gbuffer_albedo; // Current GBuffer/normal in reflection target resolution 
#define g_rw_gbuffer_albedo g_rw_textures[GDT_RW_TEXTURES_GBUFFER_ALBEDO_SLOT]
#define GDT_RW_TEXTURES_GBUFFER_NORMAL_SLOT 2
// RWTexture2D<float4> g_rw_gbuffer_normal; // Current GBuffer/normal in reflection target resolution 
#define g_rw_gbuffer_normal g_rw_textures[GDT_RW_TEXTURES_GBUFFER_NORMAL_SLOT]
#define GDT_RW_TEXTURES_GBUFFER_ROUGHNESS_SLOT 3
// RWTexture2D<float4> g_rw_gbuffer_roughness; // Current GBuffer/specular_roughness in reflection target resolution 
#define g_rw_gbuffer_roughness g_rw_textures[GDT_RW_TEXTURES_GBUFFER_ROUGHNESS_SLOT]
#define GDT_RW_TEXTURES_GBUFFER_DEPTH_SLOT 4
// RWTexture2D<float4> g_rw_gbuffer_depth; // Current GBuffer/depth in reflection target resolution 
#define g_rw_gbuffer_depth g_rw_textures[GDT_RW_TEXTURES_GBUFFER_DEPTH_SLOT]
#define GDT_RW_TEXTURES_MOTION_VECTOR_SLOT 5
// RWTexture2D<float4> g_rw_motion_vector; // Current GBuffer/motion_vectors in reflection target resolution 
#define g_rw_motion_vector g_rw_textures[GDT_RW_TEXTURES_MOTION_VECTOR_SLOT]
#define GDT_TEXTURES_GBUFFER_DEPTH_HISTORY_SLOT 8
// Texture2D<float4> g_gbuffer_depth_history; // Previous GBuffer/depth in reflection target resolution 
#define g_gbuffer_depth_history g_textures[GDT_TEXTURES_GBUFFER_DEPTH_HISTORY_SLOT]
#define GDT_TEXTURES_GBUFFER_NORMAL_HISTORY_SLOT 9
// Texture2D<float4> g_gbuffer_normal_history; // Previous GBuffer/normal in reflection target resolution 
#define g_gbuffer_normal_history g_textures[GDT_TEXTURES_GBUFFER_NORMAL_HISTORY_SLOT]
#define GDT_TEXTURES_EXTRACTED_ROUGHNESS_HISTORY_SLOT 10
// Texture2D<float4> g_extracted_roughness_history; // Previous extracted GBuffer/roughness in reflection target resolution 
#define g_extracted_roughness_history g_textures[GDT_TEXTURES_EXTRACTED_ROUGHNESS_HISTORY_SLOT]
#define GDT_TEXTURES_GBUFFER_FULL_ALBEDO_SLOT 11
// Texture2D<float4> g_gbuffer_full_albedo; // Current GBuffer/albedo in reflection target resolution 
#define g_gbuffer_full_albedo g_textures[GDT_TEXTURES_GBUFFER_FULL_ALBEDO_SLOT]
#define GDT_TEXTURES_GBUFFER_FULL_NORMAL_SLOT 12
// Texture2D<float4> g_gbuffer_full_normal; // Current GBuffer/normal in render resolution 
#define g_gbuffer_full_normal g_textures[GDT_TEXTURES_GBUFFER_FULL_NORMAL_SLOT]
#define GDT_TEXTURES_GBUFFER_FULL_ROUGHNESS_SLOT 13
// Texture2D<float4> g_gbuffer_full_roughness; // Current GBuffer/specular_roughness in render resolution 
#define g_gbuffer_full_roughness g_textures[GDT_TEXTURES_GBUFFER_FULL_ROUGHNESS_SLOT]
#define GDT_TEXTURES_GBUFFER_FULL_DEPTH_SLOT 14
// Texture2D<float4> g_gbuffer_full_depth; // Current GBuffer/depth in render resolution 
#define g_gbuffer_full_depth g_textures[GDT_TEXTURES_GBUFFER_FULL_DEPTH_SLOT]
#define GDT_TEXTURES_FULL_MOTION_VECTOR_SLOT 15
// Texture2D<float4> g_full_motion_vector; // Current GBuffer/motion_vectors in render resolution 
#define g_full_motion_vector g_textures[GDT_TEXTURES_FULL_MOTION_VECTOR_SLOT]
#define GDT_TEXTURES_LIT_SCREEN_SLOT 16
// Texture2D<float4> g_lit_screen; // Current render resolution color target 
#define g_lit_screen g_textures[GDT_TEXTURES_LIT_SCREEN_SLOT]
#define GDT_RW_TEXTURES_FULL_LIT_SCENE_SLOT 6
// RWTexture2D<float4> g_rw_full_lit_scene; // Current render resolution color target 
#define g_rw_full_lit_scene g_rw_textures[GDT_RW_TEXTURES_FULL_LIT_SCENE_SLOT]
#define GDT_TEXTURES_LIT_SCENE_HISTORY_SLOT 17
// Texture2D<float4> g_lit_scene_history; // Previous render resolution color target 
#define g_lit_scene_history g_textures[GDT_TEXTURES_LIT_SCENE_HISTORY_SLOT]
#define GDT_TEXTURES_BRDF_LUT_SLOT 18
// Texture2D<float4> g_brdf_lut; // BRDF Look up table for image based lightning 
#define g_brdf_lut g_textures[GDT_TEXTURES_BRDF_LUT_SLOT]
#define GDT_TEXTURES_DEBUG_SLOT 19
// Texture2D<float4> g_debug; // Debug target 
#define g_debug g_textures[GDT_TEXTURES_DEBUG_SLOT]
#define GDT_RW_TEXTURES_DEBUG_SLOT 7
// RWTexture2D<float4> g_rw_debug; // Debug target 
#define g_rw_debug g_rw_textures[GDT_RW_TEXTURES_DEBUG_SLOT]
// Buffers for hybrid reflections.
#define GDT_RW_UTEXTURES_HIT_COUNTER_SLOT 0
// RWTexture2D<uint> g_rw_hit_counter; 
#define g_rw_hit_counter g_rw_utextures[GDT_RW_UTEXTURES_HIT_COUNTER_SLOT]
#define GDT_UTEXTURES_HIT_COUNTER_HISTORY_SLOT 0
// Texture2D<uint> g_hit_counter_history; 
#define g_hit_counter_history g_utextures[GDT_UTEXTURES_HIT_COUNTER_HISTORY_SLOT]
#define GDT_RW_TEXTURES_RADIANCE_AVG_SLOT 8
// RWTexture2D<float4> g_rw_radiance_avg; // 8x8 average radiance 
#define g_rw_radiance_avg g_rw_textures[GDT_RW_TEXTURES_RADIANCE_AVG_SLOT]
#define GDT_RW_TEXTURES_RANDOM_NUMBER_IMAGE_SLOT 9
// RWTexture2D<float4> g_rw_random_number_image; // Baked random numbers 128x128 for the current frame 
#define g_rw_random_number_image g_rw_textures[GDT_RW_TEXTURES_RANDOM_NUMBER_IMAGE_SLOT]
#define GDT_TEXTURES_RANDOM_NUMBER_IMAGE_SLOT 20
// Texture2D<float4> g_random_number_image; // Baked random numbers 128x128 for the current frame 
#define g_random_number_image g_textures[GDT_TEXTURES_RANDOM_NUMBER_IMAGE_SLOT]
#define GDT_TEXTURESFP16X3_RADIANCE_0_SLOT 0
// Texture2D<min16float3> g_radiance_0; // Radiance target 0 - intersection results 
#define g_radiance_0 g_texturesfp16x3[GDT_TEXTURESFP16X3_RADIANCE_0_SLOT]
#define GDT_TEXTURESFP16X3_RADIANCE_1_SLOT 1
// Texture2D<min16float3> g_radiance_1; // Radiance target 1 - history radiance 
#define g_radiance_1 g_texturesfp16x3[GDT_TEXTURESFP16X3_RADIANCE_1_SLOT]
#define GDT_TEXTURESFP16X3_RADIANCE_REPROJECTED_SLOT 2
// Texture2D<min16float3> g_radiance_reprojected; // Radiance target 2 - reprojected results 
#define g_radiance_reprojected g_texturesfp16x3[GDT_TEXTURESFP16X3_RADIANCE_REPROJECTED_SLOT]
#define GDT_RW_TEXTURESFP16X3_RADIANCE_0_SLOT 0
// RWTexture2D<min16float3> g_rw_radiance_0; // Radiance target 0 - intersection results 
#define g_rw_radiance_0 g_rw_texturesfp16x3[GDT_RW_TEXTURESFP16X3_RADIANCE_0_SLOT]
#define GDT_RW_TEXTURESFP16X3_RADIANCE_1_SLOT 1
// RWTexture2D<min16float3> g_rw_radiance_1; // Radiance target 1 - history radiance 
#define g_rw_radiance_1 g_rw_texturesfp16x3[GDT_RW_TEXTURESFP16X3_RADIANCE_1_SLOT]
#define GDT_RW_TEXTURESFP16X3_RADIANCE_REPROJECTED_SLOT 2
// RWTexture2D<min16float3> g_rw_radiance_reprojected; // Radiance target 2 - reprojected results 
#define g_rw_radiance_reprojected g_rw_texturesfp16x3[GDT_RW_TEXTURESFP16X3_RADIANCE_REPROJECTED_SLOT]
#define GDT_TEXTURES_RADIANCE_MIP_SLOT 21
// Texture2D<float4> g_radiance_mip; // 8x8 average radiance 
#define g_radiance_mip g_textures[GDT_TEXTURES_RADIANCE_MIP_SLOT]
#define GDT_TEXTURES_RADIANCE_MIP_PREV_SLOT 22
// Texture2D<float4> g_radiance_mip_prev; // 8x8 average radiance history 
#define g_radiance_mip_prev g_textures[GDT_TEXTURES_RADIANCE_MIP_PREV_SLOT]
#define GDT_TEXTURESFP16_RADIANCE_VARIANCE_0_SLOT 0
// Texture2D<min16float> g_radiance_variance_0; // Variance target 0 - current variance/ray length 
#define g_radiance_variance_0 g_texturesfp16[GDT_TEXTURESFP16_RADIANCE_VARIANCE_0_SLOT]
#define GDT_TEXTURESFP16_RADIANCE_VARIANCE_1_SLOT 1
// Texture2D<min16float> g_radiance_variance_1; // Variance target 1 - history 
#define g_radiance_variance_1 g_texturesfp16[GDT_TEXTURESFP16_RADIANCE_VARIANCE_1_SLOT]
#define GDT_TEXTURESFP16_RADIANCE_NUM_SAMPLES_0_SLOT 2
// Texture2D<min16float> g_radiance_num_samples_0; // Sample counter target 0 - current 
#define g_radiance_num_samples_0 g_texturesfp16[GDT_TEXTURESFP16_RADIANCE_NUM_SAMPLES_0_SLOT]
#define GDT_TEXTURESFP16_RADIANCE_NUM_SAMPLES_1_SLOT 3
// Texture2D<min16float> g_radiance_num_samples_1; // Sample count target 1 - history 
#define g_radiance_num_samples_1 g_texturesfp16[GDT_TEXTURESFP16_RADIANCE_NUM_SAMPLES_1_SLOT]
#define GDT_RW_TEXTURESFP16_RADIANCE_VARIANCE_0_SLOT 0
// RWTexture2D<min16float> g_rw_radiance_variance_0; // Variance target 0 - current variance/ray length 
#define g_rw_radiance_variance_0 g_rw_texturesfp16[GDT_RW_TEXTURESFP16_RADIANCE_VARIANCE_0_SLOT]
#define GDT_RW_TEXTURESFP16_RADIANCE_VARIANCE_1_SLOT 1
// RWTexture2D<min16float> g_rw_radiance_variance_1; // Variance target 1 - history 
#define g_rw_radiance_variance_1 g_rw_texturesfp16[GDT_RW_TEXTURESFP16_RADIANCE_VARIANCE_1_SLOT]
#define GDT_RW_TEXTURESFP16_RADIANCE_NUM_SAMPLES_0_SLOT 2
// RWTexture2D<min16float> g_rw_radiance_num_samples_0; // Sample counter target 0 - current 
#define g_rw_radiance_num_samples_0 g_rw_texturesfp16[GDT_RW_TEXTURESFP16_RADIANCE_NUM_SAMPLES_0_SLOT]
#define GDT_RW_TEXTURESFP16_RADIANCE_NUM_SAMPLES_1_SLOT 3
// RWTexture2D<min16float> g_rw_radiance_num_samples_1; // Sample count target 1 - history 
#define g_rw_radiance_num_samples_1 g_rw_texturesfp16[GDT_RW_TEXTURESFP16_RADIANCE_NUM_SAMPLES_1_SLOT]
// Ranges
#define GDT_TEXTURES_SHADOW_MAP_BEGIN_SLOT 23
// Texture2D<float4> g_shadow_map_begin; 
#define g_shadow_map_begin g_textures[GDT_TEXTURES_SHADOW_MAP_BEGIN_SLOT]
#define GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT 123
// Texture2D<float4> g_pbr_textures_begin; 
#define g_pbr_textures_begin g_textures[GDT_TEXTURES_PBR_TEXTURES_BEGIN_SLOT]
// Atmosphere textures
#define GDT_CTEXTURES_DIFFUSE_SLOT 0
// TextureCube<float4> g_diffuse; 
#define g_diffuse g_ctextures[GDT_CTEXTURES_DIFFUSE_SLOT]
#define GDT_CTEXTURES_SPECULAR_SLOT 1
// TextureCube<float4> g_specular; 
#define g_specular g_ctextures[GDT_CTEXTURES_SPECULAR_SLOT]
#define GDT_CTEXTURES_ATMOSPHERE_LUT_SLOT 2
// TextureCube<float4> g_atmosphere_lut; // Environment specular LUT 
#define g_atmosphere_lut g_ctextures[GDT_CTEXTURES_ATMOSPHERE_LUT_SLOT]
#define GDT_CTEXTURES_ATMOSPHERE_MIP_SLOT 3
// TextureCube<float4> g_atmosphere_mip; // Environment Mip chain 
#define g_atmosphere_mip g_ctextures[GDT_CTEXTURES_ATMOSPHERE_MIP_SLOT]
#define GDT_CTEXTURES_ATMOSPHERE_DIF_SLOT 4
// TextureCube<float4> g_atmosphere_dif; // Environment Diffuse LUT 
#define g_atmosphere_dif g_ctextures[GDT_CTEXTURES_ATMOSPHERE_DIF_SLOT]
#define GDT_RW_ATEXTURES_ATMOSPHERE_LUT_BEGIN_SLOT 0
// RWTexture2DArray<float4> g_rw_atmosphere_lut_begin; 
#define g_rw_atmosphere_lut_begin g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_LUT_BEGIN_SLOT]
#define GDT_RW_ATEXTURES_ATMOSPHERE_MIP_BEGIN_SLOT 128
// RWTexture2DArray<float4> g_rw_atmosphere_mip_begin; 
#define g_rw_atmosphere_mip_begin g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_MIP_BEGIN_SLOT]
#define GDT_RW_ATEXTURES_ATMOSPHERE_DIF_BEGIN_SLOT 256
// RWTexture2DArray<float4> g_rw_atmosphere_dif_begin; 
#define g_rw_atmosphere_dif_begin g_rw_atextures[GDT_RW_ATEXTURES_ATMOSPHERE_DIF_BEGIN_SLOT]
// Buffers bindless material system.
#define GDT_BUFFERS_INSTANCE_INFO_SLOT 0
// RWByteAddressBuffer g_rw_instance_info; // Array of Instance_Info 
#define g_rw_instance_info g_rw_buffers[GDT_BUFFERS_INSTANCE_INFO_SLOT]
#define GDT_BUFFERS_SURFACE_ID_SLOT 1
// RWByteAddressBuffer g_rw_surface_id; // Array of uint 
#define g_rw_surface_id g_rw_buffers[GDT_BUFFERS_SURFACE_ID_SLOT]
#define GDT_BUFFERS_SURFACE_INFO_SLOT 2
// RWByteAddressBuffer g_rw_surface_info; // Array of Surface_Info 
#define g_rw_surface_info g_rw_buffers[GDT_BUFFERS_SURFACE_INFO_SLOT]
#define GDT_BUFFERS_MATERIAL_INFO_SLOT 3
// RWByteAddressBuffer g_rw_material_info; // Array of Material_Info 
#define g_rw_material_info g_rw_buffers[GDT_BUFFERS_MATERIAL_INFO_SLOT]
#define GDT_BUFFERS_GEOMETRY_SLOT 4
// RWByteAddressBuffer g_rw_geometry; // Vertex/Index buffers for all the geometry in the TLAS 
#define g_rw_geometry g_rw_buffers[GDT_BUFFERS_GEOMETRY_SLOT]
#define GDT_BUFFERS_SOBOL_SLOT 5
// RWByteAddressBuffer g_rw_sobol; 
#define g_rw_sobol g_rw_buffers[GDT_BUFFERS_SOBOL_SLOT]
#define GDT_BUFFERS_RANKING_TILE_SLOT 6
// RWByteAddressBuffer g_rw_ranking_tile; 
#define g_rw_ranking_tile g_rw_buffers[GDT_BUFFERS_RANKING_TILE_SLOT]
#define GDT_BUFFERS_SCRAMBLING_TILE_SLOT 7
// RWByteAddressBuffer g_rw_scrambling_tile; 
#define g_rw_scrambling_tile g_rw_buffers[GDT_BUFFERS_SCRAMBLING_TILE_SLOT]
#define GDT_BUFFERS_RAY_LIST_SLOT 8
// RWByteAddressBuffer g_rw_ray_list; 
#define g_rw_ray_list g_rw_buffers[GDT_BUFFERS_RAY_LIST_SLOT]
#define GDT_BUFFERS_RAY_COUNTER_SLOT 9
// RWByteAddressBuffer g_rw_ray_counter; 
#define g_rw_ray_counter g_rw_buffers[GDT_BUFFERS_RAY_COUNTER_SLOT]
#define GDT_BUFFERS_INDIRECT_ARGS_SLOT 10
// RWByteAddressBuffer g_rw_indirect_args; 
#define g_rw_indirect_args g_rw_buffers[GDT_BUFFERS_INDIRECT_ARGS_SLOT]
#define GDT_BUFFERS_DOWNSAMPLE_COUNTER_SLOT 11
// RWByteAddressBuffer g_rw_downsample_counter; 
#define g_rw_downsample_counter g_rw_buffers[GDT_BUFFERS_DOWNSAMPLE_COUNTER_SLOT]
#define GDT_BUFFERS_DENOISE_TILE_LIST_SLOT 12
// RWByteAddressBuffer g_rw_denoise_tile_list; 
#define g_rw_denoise_tile_list g_rw_buffers[GDT_BUFFERS_DENOISE_TILE_LIST_SLOT]
#define GDT_BUFFERS_METRICS_SLOT 13
// RWByteAddressBuffer g_rw_metrics; 
#define g_rw_metrics g_rw_buffers[GDT_BUFFERS_METRICS_SLOT]
#define GDT_BUFFERS_HW_RAY_LIST_SLOT 14
// RWByteAddressBuffer g_rw_hw_ray_list; 
#define g_rw_hw_ray_list g_rw_buffers[GDT_BUFFERS_HW_RAY_LIST_SLOT]
#define GDT_BUFFERS_RAY_GBUFFER_LIST_SLOT 22
// RWByteAddressBuffer g_rw_ray_gbuffer_list; // Array of RayGBuffer for deferred shading of ray traced results 
#define g_rw_ray_gbuffer_list g_rw_buffers[GDT_BUFFERS_RAY_GBUFFER_LIST_SLOT]
#define GDT_SAMPLERS_LINEAR_SAMPLER_SLOT 0
// SamplerState g_linear_sampler; 
#define g_linear_sampler g_samplers[GDT_SAMPLERS_LINEAR_SAMPLER_SLOT]
#define GDT_SAMPLERS_BORDER_LINEAR_SAMPLER_SLOT 1
// SamplerState g_border_linear_sampler; 
#define g_border_linear_sampler g_samplers[GDT_SAMPLERS_BORDER_LINEAR_SAMPLER_SLOT]
#define GDT_SAMPLERS_WRAP_LINEAR_SAMPLER_SLOT 2
// SamplerState g_wrap_linear_sampler; 
#define g_wrap_linear_sampler g_samplers[GDT_SAMPLERS_WRAP_LINEAR_SAMPLER_SLOT]
#define GDT_CMP_SAMPLERS_CMP_SAMPLER_SLOT 0
// SamplerComparisonState g_cmp_sampler; 
#define g_cmp_sampler g_cmp_samplers[GDT_CMP_SAMPLERS_CMP_SAMPLER_SLOT]
#define GDT_FRAME_INFO_FRAME_INFO_SLOT 0
// ConstantBuffer<FrameInfo> g_frame_info; 
#define g_frame_info g_frame_info_cb[GDT_FRAME_INFO_FRAME_INFO_SLOT]

// ADD_***_RANGE(Num of registers, Dx12 Register offset, Space index, Dx12 Heap offset);
#define INIT_GLOBAL_RANGES(SPACE_ID)     do { \
 ADD_TLAS_RANGE(3, 0, SPACE_ID, 0); \
 ADD_TEXTURE_RANGE(623, 3, SPACE_ID, 3); \
 ADD_TEXTURE_RANGE(4, 626, SPACE_ID, 626); \
 ADD_TEXTURE_RANGE(3, 630, SPACE_ID, 630); \
 ADD_TEXTURE_RANGE(5, 633, SPACE_ID, 633); \
 ADD_TEXTURE_RANGE(1, 638, SPACE_ID, 638); \
 ADD_UAV_TEXTURE_RANGE(10, 0, SPACE_ID, 639); \
 ADD_UAV_TEXTURE_RANGE(4, 10, SPACE_ID, 649); \
 ADD_UAV_TEXTURE_RANGE(3, 14, SPACE_ID, 653); \
 ADD_UAV_TEXTURE_RANGE(384, 17, SPACE_ID, 656); \
 ADD_UAV_TEXTURE_RANGE(1, 401, SPACE_ID, 1040); \
 ADD_BUFFER_RANGE(23, 402, SPACE_ID, 1041); \
 ADD_SAMPLER_RANGE(3, 0, SPACE_ID, 0); \
 ADD_SAMPLER_RANGE(1, 3, SPACE_ID, 3); \
 ADD_UNIFORM_BUFFER_RANGE(1, 0, SPACE_ID, 1064); \
} while (0)

#define GDT_CBV_SRV_UAV_NUM_RANGES 13
#define GDT_CBV_SRV_UAV_SIZE 1065
#define GDT_SAMPLERS_SIZE 4
#define GDT_SAMPLERS_NUM_RANGES 2
#define GDT_TLAS_REGISTER_OFFSET 0
#define GDT_TEXTURES_REGISTER_OFFSET 3
#define GDT_TEXTURESFP16_REGISTER_OFFSET 626
#define GDT_TEXTURESFP16X3_REGISTER_OFFSET 630
#define GDT_CTEXTURES_REGISTER_OFFSET 633
#define GDT_UTEXTURES_REGISTER_OFFSET 638
#define GDT_RW_TEXTURES_REGISTER_OFFSET 0
#define GDT_RW_TEXTURESFP16_REGISTER_OFFSET 10
#define GDT_RW_TEXTURESFP16X3_REGISTER_OFFSET 14
#define GDT_RW_ATEXTURES_REGISTER_OFFSET 17
#define GDT_RW_UTEXTURES_REGISTER_OFFSET 401
#define GDT_BUFFERS_REGISTER_OFFSET 402
#define GDT_SAMPLERS_REGISTER_OFFSET 0
#define GDT_CMP_SAMPLERS_REGISTER_OFFSET 3
#define GDT_FRAME_INFO_REGISTER_OFFSET 0
#define GDT_TLAS_HEAP_OFFSET 0
#define GDT_TEXTURES_HEAP_OFFSET 3
#define GDT_TEXTURESFP16_HEAP_OFFSET 626
#define GDT_TEXTURESFP16X3_HEAP_OFFSET 630
#define GDT_CTEXTURES_HEAP_OFFSET 633
#define GDT_UTEXTURES_HEAP_OFFSET 638
#define GDT_RW_TEXTURES_HEAP_OFFSET 639
#define GDT_RW_TEXTURESFP16_HEAP_OFFSET 649
#define GDT_RW_TEXTURESFP16X3_HEAP_OFFSET 653
#define GDT_RW_ATEXTURES_HEAP_OFFSET 656
#define GDT_RW_UTEXTURES_HEAP_OFFSET 1040
#define GDT_BUFFERS_HEAP_OFFSET 1041
#define GDT_SAMPLERS_HEAP_OFFSET 0
#define GDT_CMP_SAMPLERS_HEAP_OFFSET 3
#define GDT_FRAME_INFO_HEAP_OFFSET 1064
#define GDT_TLAS_LOCATION 0
#define GDT_TEXTURES_LOCATION 1
#define GDT_TEXTURESFP16_LOCATION 2
#define GDT_TEXTURESFP16X3_LOCATION 3
#define GDT_CTEXTURES_LOCATION 4
#define GDT_UTEXTURES_LOCATION 5
#define GDT_RW_TEXTURES_LOCATION 6
#define GDT_RW_TEXTURESFP16_LOCATION 7
#define GDT_RW_TEXTURESFP16X3_LOCATION 8
#define GDT_RW_ATEXTURES_LOCATION 9
#define GDT_RW_UTEXTURES_LOCATION 10
#define GDT_BUFFERS_LOCATION 11
#define GDT_SAMPLERS_LOCATION 12
#define GDT_CMP_SAMPLERS_LOCATION 13
#define GDT_FRAME_INFO_LOCATION 14
