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

#include "stdafx.h"

#include "Base/GBuffer.h"
#include "Base/ShaderCompilerHelper.h"
#include "GLTF/GltfHelpers.h"
#include "GltfPbrPass.h"
#include "Misc/ThreadPool.h"

#include <unordered_set>

namespace RTCAULDRON_DX12 {
//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::OnCreate(Device *pDevice, UploadHeap *pUploadHeap, ResourceViewHeaps *pHeaps, DynamicBufferRing *pDynamicBufferRing,
                             GLTFTexturesAndBuffers *pGLTFTexturesAndBuffers, StaticBufferPool *pStaticBufferPool, Texture *pSpecularLUT, Texture *pDiffuseLUT, bool bUseSSAOMask,
                             bool bUseShadowMask, GBufferRenderPass *pGBufferRenderPass, AsyncPool *pAsyncPool) {
    m_pDevice                 = pDevice;
    m_pGBufferRenderPass      = pGBufferRenderPass;
    m_sampleCount             = 1;
    m_pResourceViewHeaps      = pHeaps;
    m_pDynamicBufferRing      = pDynamicBufferRing;
    m_pGLTFTexturesAndBuffers = pGLTFTexturesAndBuffers;
    m_pStaticBufferPool       = pStaticBufferPool;
    m_doLighting              = true;
    m_infoTables.m_pParent    = this;

    HRESULT hr = pDevice->GetDevice()->QueryInterface(&m_pDevice5);
    if (!SUCCEEDED(hr)) throw 0;

    DefineList rtDefines;
    m_pGBufferRenderPass->GetCompilerDefinesAndGBufferFormats(rtDefines, m_outFormats, m_depthFormat);

    // Load BRDF look up table for the PBR shader
    //
    m_BrdfLut.InitFromFile(pDevice, pUploadHeap, "BrdfLut.dds", false); // LUT images are stored as linear

    // Create default material, this material will be used if none is assigned
    //
    {
        SetDefaultMaterialParamters(&m_defaultMaterial.m_pbrMaterialParameters);

        std::map<std::string, Texture *> texturesBase;
        CreateDescriptorTableForMaterialTextures(&m_defaultMaterial, texturesBase, pSpecularLUT, pDiffuseLUT, bUseShadowMask, bUseSSAOMask);
    }

    const json &j3 = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->j3;

    // Load PBR 2.0 Materials
    //
    const json &materials = j3["materials"];
    m_materialsData.resize(materials.size() + 1);
    for (uint32_t i = 0; i < materials.size(); i++) {
        PBRMaterial *tfmat = &m_materialsData[i];

        // Get PBR material parameters and texture IDs
        //
        std::map<std::string, int> textureIds;
        ProcessMaterials(materials[i], &tfmat->m_pbrMaterialParameters, textureIds);

        hlsl::Material_Info materialInfo{};
        memset(&materialInfo, -1, sizeof(materialInfo));

        materialInfo.albedo_factor_x = tfmat->m_pbrMaterialParameters.m_params.m_baseColorFactor.getX();
        materialInfo.albedo_factor_y = tfmat->m_pbrMaterialParameters.m_params.m_baseColorFactor.getY();
        materialInfo.albedo_factor_z = tfmat->m_pbrMaterialParameters.m_params.m_baseColorFactor.getZ();
        materialInfo.albedo_factor_w = tfmat->m_pbrMaterialParameters.m_params.m_baseColorFactor.getW();

        materialInfo.emission_factor_x = tfmat->m_pbrMaterialParameters.m_params.m_emissiveFactor.getX();
        materialInfo.emission_factor_y = tfmat->m_pbrMaterialParameters.m_params.m_emissiveFactor.getY();
        materialInfo.emission_factor_z = tfmat->m_pbrMaterialParameters.m_params.m_emissiveFactor.getZ();

        materialInfo.arm_factor_x = 1.0f; // tfmat->m_pbrMaterialParameters.m_params.m_metallicRoughnessValues.getX();
        materialInfo.arm_factor_y = tfmat->m_pbrMaterialParameters.m_params.m_metallicRoughnessValues.getY();
        materialInfo.arm_factor_z = tfmat->m_pbrMaterialParameters.m_params.m_metallicRoughnessValues.getX();

        materialInfo.is_opaque = !tfmat->m_pbrMaterialParameters.m_blending;

        for (const auto &t : textureIds) {
            Texture *texture    = m_pGLTFTexturesAndBuffers->GetTextureViewByID(t.second);
            int32_t  texture_id = int32_t(m_infoTables.m_cpuTextureTable.size());
            if (t.first == "baseColorTexture") {
                materialInfo.albedo_tex_id = texture_id;
            } else if (t.first == "normalTexture") {
                materialInfo.normal_tex_id = texture_id;
            } else if (t.first == "emissiveTexture") {
                materialInfo.emission_tex_id = texture_id;
            } else if (t.first == "metallicRoughnessTexture") {
                materialInfo.arm_tex_id = texture_id;
            } else {
                fprintf(stderr, "[WARNING] Unsupported Texture Channel: %s\n", t.first.c_str());
                continue;
            }
            m_infoTables.m_cpuTextureTable.push_back(texture->GetResource());
        }
        tfmat->m_materialID = (int32_t)m_infoTables.m_cpuMaterialBuffer.size();
        m_infoTables.m_cpuMaterialBuffer.push_back(materialInfo);
        // translate texture IDs into textureViews
        //
        std::map<std::string, Texture *> texturesBase;
        for (auto const &value : textureIds) texturesBase[value.first] = m_pGLTFTexturesAndBuffers->GetTextureViewByID(value.second);
        CreateDescriptorTableForMaterialTextures(tfmat, texturesBase, pSpecularLUT, pDiffuseLUT, bUseShadowMask, bUseSSAOMask);
    }

    m_infoTables.m_pSrcGeometryBufferResource = pStaticBufferPool->GetResource();

    // Load Meshes
    //
    std::vector<tfNode> *                    pNodes = &m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_nodes;
    std::unordered_map<PBRPrimitives *, int> surface_cache;
    if (j3.find("meshes") != j3.end()) {
        const json &nodes = j3["nodes"];

        const json &meshes = j3["meshes"];

        /////////////////////////////////////////////////////////
        // Find debris node and exclude all children from BVH  //
        /////////////////////////////////////////////////////////
        if (j3.find("meshes") != j3.end()) {
            std::function<void(int)> excludeNode = [&](int id) {
                m_infoTables.m_excludedNodes.insert(id);
                for (auto const &child : pNodes->at(id).m_children) excludeNode(child);
            };
            const json &nodes = j3["nodes"];
            for (uint32_t i = 0; i < nodes.size(); i++) {
                const json &node = nodes[i];
                std::string name = GetElementString(node, "name", "unnamed");

                if (name == "Debris") {
                    excludeNode(i);
                }
            }
        }

        m_meshes.resize(meshes.size());
        for (uint32_t i = 0; i < meshes.size(); i++) {
            const json &mesh       = meshes[i];
            const json &primitives = meshes[i]["primitives"];

            // Loop through all the primitives (sets of triangles with a same material) and
            // 1) create an input layout for the geometry
            // 2) then take its material and create a Root descriptor
            // 3) With all the above, create a pipeline
            //
            PBRMesh *tfmesh = &m_meshes[i];
            tfmesh->m_pPrimitives.resize(primitives.size());

            for (uint32_t p = 0; p < primitives.size(); p++) {
                const json &       primitive  = primitives[p];
                PBRPrimitives *    pPrimitive = &tfmesh->m_pPrimitives[p];
                hlsl::Surface_Info surface_info{};
                memset(&surface_info, -1, sizeof(surface_info));
                {

                    // Sets primitive's material, or set a default material if none was specified in the GLTF
                    //
                    auto mat = primitive.find("material");
                    if (mat != primitive.end()) {
                        pPrimitive->m_pMaterial = &m_materialsData[mat.value()];
                    } else {
                        pPrimitive->m_pMaterial = &m_defaultMaterial;
                    }
                    surface_info.material_id = pPrimitive->m_pMaterial->m_materialID;
                    // holds all the #defines from materials, geometry and texture IDs, the VS & PS shaders need this to get the bindings and code paths
                    //
                    DefineList defines = pPrimitive->m_pMaterial->m_pbrMaterialParameters.m_defines + rtDefines;

                    // make a list of all the attribute names our pass requires, in the case of PBR we need them all
                    //
                    std::vector<std::string> requiredAttributes;
                    for (auto const &it : primitive["attributes"].items()) requiredAttributes.push_back(it.key());

                    // create an input layout from the required attributes
                    // shader's can tell the slots from the #defines
                    //
                    std::vector<std::string>              semanticNames;
                    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
                    m_pGLTFTexturesAndBuffers->CreateGeometry(primitive, requiredAttributes, semanticNames, inputLayout, defines, &pPrimitive->m_geometry);

                    surface_info.num_indices  = (int32_t)pPrimitive->m_geometry.m_NumIndices;
                    surface_info.num_vertices = pPrimitive->m_geometry.m_VBV[0].StrideInBytes > 0
                                                    ? (int32_t)(pPrimitive->m_geometry.m_VBV[0].SizeInBytes / pPrimitive->m_geometry.m_VBV[0].StrideInBytes)
                                                    : 0;
                    if (pPrimitive->m_geometry.m_indexType == DXGI_FORMAT_R32_UINT) {
                        surface_info.index_type = SURFACE_INFO_INDEX_TYPE_U32;
                    } else if (pPrimitive->m_geometry.m_indexType == DXGI_FORMAT_R16_UINT) {
                        surface_info.index_type = SURFACE_INFO_INDEX_TYPE_U16;
                    } else {
                        assert(false && "[ERROR] Unsupported Index type");
                    }

                    uint32_t cnt = 0;
                    for (const auto &attribute : requiredAttributes) {
                        int32_t offset = (int32_t)(
                            ((ptrdiff_t)pPrimitive->m_geometry.m_VBV[cnt].BufferLocation - (ptrdiff_t)m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress()) / 4);
                        if (attribute == "POSITION") {
                            surface_info.position_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R32G32B32_FLOAT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 12);
                        } else if (attribute == "NORMAL") {
                            surface_info.normal_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R32G32B32_FLOAT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 12);
                        } else if (attribute == "TEXCOORD_0") {
                            surface_info.texcoord0_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R32G32_FLOAT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 8);
                        } else if (attribute == "TEXCOORD_1") {
                            surface_info.texcoord1_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R32G32_FLOAT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 8);
                        } else if (attribute == "TANGENT") {
                            surface_info.tangent_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R32G32B32A32_FLOAT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 16);
                        } else if (attribute == "WEIGHTS_0") {
                            surface_info.weight_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R32G32B32A32_FLOAT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 16);
                        } else if (attribute == "JOINTS_0") {
                            surface_info.joints_attribute_offset = offset;
                            assert(inputLayout[cnt].Format == DXGI_FORMAT_R8G8B8A8_UINT);
                            assert(pPrimitive->m_geometry.m_VBV[cnt].StrideInBytes == 4);
                        } else {
                            fprintf(stderr, "[WARNING] Unsupported Attribute: %s\n", attribute.c_str());
                        }

                        ++cnt;
                    }
                    surface_info.index_offset =
                        (int32_t)(((ptrdiff_t)pPrimitive->m_geometry.m_IBV.BufferLocation - (ptrdiff_t)m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress()) / 4);

                    // Create the descriptors, the root signature and the pipeline
                    //
                    bool bUsingSkinning = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->FindMeshSkinId(i) != -1;
                    CreateRootSignature(bUsingSkinning, defines, pPrimitive, bUseSSAOMask);
                    CreatePipeline(inputLayout, defines, pPrimitive);
                }
                int32_t surface_id = -1;
                if (surface_cache.find(pPrimitive) != surface_cache.end()) {
                    surface_id = surface_cache.find(pPrimitive)->second;
                } else {
                    surface_id = (int32_t)m_infoTables.m_cpuSurfaceBuffer.size();
                    m_infoTables.m_cpuSurfaceBuffer.push_back(surface_info);
                    surface_cache[pPrimitive] = surface_id;
                }
            }
        }
    }

    Matrix2 *pNodesMatrices = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_worldSpaceMats.data();

    for (uint32_t i = 0; i < pNodes->size(); i++) {
        tfNode *pNode = &pNodes->at(i);
        assert(!(pNode == NULL));

        hlsl::Instance_Info   instance_info{};
        std::vector<uint32_t> opaque_surfaces;
        std::vector<uint32_t> transparent_surfaces;
        if (pNode->meshIndex >= 0) {
            PBRMesh *pMesh = &m_meshes[pNode->meshIndex];
            for (uint32_t p = 0; p < pMesh->m_pPrimitives.size(); p++) {
                PBRPrimitives *     pPrimitive   = &pMesh->m_pPrimitives[p];
                uint32_t            surface_id   = surface_cache[pPrimitive];
                hlsl::Surface_Info &surface_info = m_infoTables.m_cpuSurfaceBuffer[surface_id];

                // If the mesh is skinned allocate space for skinned position and reference that instead

                if (surface_info.weight_attribute_offset >= 0 && surface_info.joints_attribute_offset >= 0) {
                    // Clone all attributes except for position, normals and tangents
                    hlsl::Surface_Info        new_surface_info = surface_info;
                    void *                    pData            = NULL;
                    D3D12_GPU_VIRTUAL_ADDRESS pBufferLocation  = {0};
                    uint32_t                  size             = 0;
                    uint32_t                  vertexSize       = 12;
                    if (new_surface_info.normal_attribute_offset >= 0) vertexSize += 12;
                    if (new_surface_info.tangent_attribute_offset >= 0) vertexSize += 16;
                    pStaticBufferPool->AllocBuffer(AlignUp(surface_info.num_vertices, 256), vertexSize, &pData, &pBufferLocation, &size);
                    new_surface_info.position_attribute_offset =
                        (int32_t)(((ptrdiff_t)pBufferLocation - (ptrdiff_t)m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress()) / 4);
                    pBufferLocation += surface_info.num_vertices * 12;
                    if (new_surface_info.normal_attribute_offset >= 0) {
                        new_surface_info.normal_attribute_offset =
                            (int32_t)(((ptrdiff_t)pBufferLocation - (ptrdiff_t)m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress()) / 4);
                        pBufferLocation += surface_info.num_vertices * 12;
                    }
                    if (new_surface_info.tangent_attribute_offset >= 0) {
                        new_surface_info.tangent_attribute_offset =
                            (int32_t)(((ptrdiff_t)pBufferLocation - (ptrdiff_t)m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress()) / 4);
                        pBufferLocation += surface_info.num_vertices * 16;
                    }
                    uint32_t new_surface_id = (uint32_t)m_infoTables.m_cpuSurfaceBuffer.size();
                    m_infoTables.m_cpuSurfaceBuffer.push_back(new_surface_info);
                    Skinned_Surface_Info skin_info{};
                    skin_info.src_surface_id = (int32_t)surface_id;
                    skin_info.dst_surface_id = (int32_t)new_surface_id;
                    skin_info.instance_id    = i;
                    m_infoTables.m_cpuSkinnedSurfaces.push_back(skin_info);
                    surface_id = new_surface_id;
                }

                if (pPrimitive->m_pMaterial->m_pbrMaterialParameters.m_blending)
                    transparent_surfaces.push_back(surface_id);
                else
                    opaque_surfaces.push_back(surface_id);
            }
        }
        instance_info.surface_id_table_offset = (uint32_t)m_infoTables.m_cpuSurfaceIDsBuffer.size();
        instance_info.num_surfaces            = (uint32_t)(transparent_surfaces.size() + opaque_surfaces.size());
        instance_info.num_opaque_surfaces     = (uint32_t)(opaque_surfaces.size());
        instance_info.node_id                 = i;
        // If not in the exclusion table
        if (m_infoTables.m_excludedNodes.find(i) == m_infoTables.m_excludedNodes.end()) {
            for (auto id : opaque_surfaces) m_infoTables.m_cpuSurfaceIDsBuffer.push_back(id);
            for (auto id : transparent_surfaces) m_infoTables.m_cpuSurfaceIDsBuffer.push_back(id);
            m_infoTables.m_cpuInstanceBuffer.push_back(instance_info);
        }
    }

    UploadHeap pLocalUploadHeap;
    pLocalUploadHeap.OnCreate(pDevice, m_infoTables.m_cpuInstanceBuffer.size() * sizeof(hlsl::Instance_Info) + m_infoTables.m_cpuSurfaceBuffer.size() * sizeof(hlsl::Surface_Info) +
                                           m_infoTables.m_cpuSurfaceIDsBuffer.size() * sizeof(uint32_t) + m_infoTables.m_cpuMaterialBuffer.size() * sizeof(hlsl::Material_Info) +
                                           0x1000);

    if (m_infoTables.m_cpuInstanceBuffer.size()) {
        m_infoTables.m_pInstanceBuffer = CreateGPULocalUAVBuffer(m_infoTables.m_cpuInstanceBuffer.size() * sizeof(hlsl::Instance_Info));
        pLocalUploadHeap.AddBufferCopy(&m_infoTables.m_cpuInstanceBuffer[0], (int)m_infoTables.m_cpuInstanceBuffer.size() * sizeof(hlsl::Instance_Info),
                                       m_infoTables.m_pInstanceBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (m_infoTables.m_cpuSurfaceBuffer.size()) {
        m_infoTables.m_pSurfaceBuffer = CreateGPULocalUAVBuffer(m_infoTables.m_cpuSurfaceBuffer.size() * sizeof(hlsl::Surface_Info));
        pLocalUploadHeap.AddBufferCopy(&m_infoTables.m_cpuSurfaceBuffer[0], (int)m_infoTables.m_cpuSurfaceBuffer.size() * sizeof(hlsl::Surface_Info), m_infoTables.m_pSurfaceBuffer,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (m_infoTables.m_cpuSurfaceIDsBuffer.size()) {
        m_infoTables.m_pSurfaceIDsBuffer = CreateGPULocalUAVBuffer(m_infoTables.m_cpuSurfaceIDsBuffer.size() * sizeof(uint32_t));
        pLocalUploadHeap.AddBufferCopy(&m_infoTables.m_cpuSurfaceIDsBuffer[0], (int)m_infoTables.m_cpuSurfaceIDsBuffer.size() * sizeof(uint32_t), m_infoTables.m_pSurfaceIDsBuffer,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (m_infoTables.m_cpuMaterialBuffer.size()) {
        m_infoTables.m_pMaterialBuffer = CreateGPULocalUAVBuffer(m_infoTables.m_cpuMaterialBuffer.size() * sizeof(hlsl::Material_Info));
        pLocalUploadHeap.AddBufferCopy(&m_infoTables.m_cpuMaterialBuffer[0], (int)m_infoTables.m_cpuMaterialBuffer.size() * sizeof(hlsl::Material_Info),
                                       m_infoTables.m_pMaterialBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    pLocalUploadHeap.FlushAndFinish();
    pLocalUploadHeap.OnDestroy();

    {
        std::vector<uint32_t> opaque_surface_ids;
        std::vector<uint32_t> transparent_surface_ids;
        std::vector<uint32_t> global_surface_ids;
        opaque_surface_ids.reserve(100);
        transparent_surface_ids.reserve(100);
        global_surface_ids.reserve(100);
        auto hasSkinning = [&](std::vector<uint32_t> const &surfaceIDs) {
            for (uint32_t i = 0; i < surfaceIDs.size(); i++) {
                D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
                geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

                hlsl::Surface_Info surface_info = m_infoTables.m_cpuSurfaceBuffer[surfaceIDs[i]];

                if (surface_info.joints_attribute_offset >= 0 || surface_info.weight_attribute_offset >= 0) return true;
            }
            return false;
        };

        for (uint32_t instance_id = 0; instance_id < m_infoTables.m_cpuInstanceBuffer.size(); instance_id++) {
            auto const &instance_info = m_infoTables.m_cpuInstanceBuffer[instance_id];
            opaque_surface_ids.resize(0);
            transparent_surface_ids.resize(0);
            global_surface_ids.resize(0);

            uint64_t opaque_surface_hash      = 0;
            uint64_t transparent_surface_hash = 0;
            uint64_t global_surface_hash      = 0;

            for (uint32_t surface_id_offset = 0; surface_id_offset < (uint32_t)instance_info.num_opaque_surfaces; surface_id_offset++) {
                uint32_t surface_id = m_infoTables.m_cpuSurfaceIDsBuffer[(int32_t)surface_id_offset + (int32_t)instance_info.surface_id_table_offset];
                opaque_surface_ids.push_back(surface_id);
                transparent_surface_ids.push_back(surface_id);
                global_surface_ids.push_back(surface_id);

                opaque_surface_hash ^= std::hash<uint32_t>()(surface_id);
                transparent_surface_hash ^= std::hash<uint32_t>()(surface_id);
                global_surface_hash ^= std::hash<uint32_t>()(surface_id);
            }
            uint32_t num_transparent_surfaces = instance_info.num_surfaces - instance_info.num_opaque_surfaces;
            for (uint32_t surface_id_offset = 0; surface_id_offset < num_transparent_surfaces; surface_id_offset++) {
                uint32_t surface_id =
                    m_infoTables.m_cpuSurfaceIDsBuffer[(int32_t)surface_id_offset + (int32_t)instance_info.surface_id_table_offset + (int32_t)instance_info.num_opaque_surfaces];
                transparent_surface_ids.push_back(surface_id);
                global_surface_ids.push_back(surface_id);

                transparent_surface_hash ^= std::hash<uint32_t>()(surface_id);
                global_surface_hash ^= std::hash<uint32_t>()(surface_id);
            }
            if (hasSkinning(transparent_surface_ids)) m_infoTables.m_skinned_surface_sets.insert(transparent_surface_hash);
            if (hasSkinning(global_surface_ids)) m_infoTables.m_skinned_surface_sets.insert(global_surface_hash);
            if (hasSkinning(opaque_surface_ids)) m_infoTables.m_skinned_surface_sets.insert(opaque_surface_hash);
        }
    }
}

//--------------------------------------------------------------------------------------
//
// CreateDescriptorTableForMaterialTextures
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::CreateDescriptorTableForMaterialTextures(PBRMaterial *tfmat, std::map<std::string, Texture *> &texturesBase, Texture *pSpecularLUT, Texture *pDiffuseLUT,
                                                             bool bUseShadowMask, bool bUseSSAOMask) {
    uint32_t cnt = 0;

    {
        // count the number of textures to init bindings and descriptor
        {
            tfmat->m_textureCount = (int)texturesBase.size();

            if (pSpecularLUT) {
                tfmat->m_textureCount += 3; // +3 because the skydome has a specular, diffusse and a BDRF LUT map
            }

            if (bUseSSAOMask) {
                tfmat->m_textureCount += 1;
            }

            // allocate descriptor table for the textures
            m_pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(tfmat->m_textureCount, &tfmat->m_texturesTable);
        }

        // Create SRV for the PBR materials
        //
        for (auto const &it : texturesBase) {
            tfmat->m_pbrMaterialParameters.m_defines[std::string("ID_") + it.first] = std::to_string(cnt);
            it.second->CreateSRV(cnt, &tfmat->m_texturesTable);
            CreateSamplerForPBR(cnt, &tfmat->m_samplers[cnt]);
            cnt++;
        }

        if (m_doLighting) {
            // 3 SRVs for the IBL probe
            //
            if (m_doLighting) {
                tfmat->m_pbrMaterialParameters.m_defines["ID_brdfTexture"] = std::to_string(cnt);
                CreateSamplerForBrdfLut(cnt, &tfmat->m_samplers[cnt]);

                m_BrdfLut.CreateSRV(cnt, &tfmat->m_texturesTable);
                cnt++;

                if (pDiffuseLUT) {
                    tfmat->m_pbrMaterialParameters.m_defines["ID_diffuseCube"] = std::to_string(cnt);
                    // pSkyDome->SetDescriptorDiff(cnt, &tfmat->m_texturesTable, cnt, &tfmat->m_samplers[cnt]);
                    pDiffuseLUT->CreateCubeSRV(cnt, &tfmat->m_texturesTable);

                    auto *pSamplerDesc = &tfmat->m_samplers[cnt];

                    // specular env map sampler
                    ZeroMemory(pSamplerDesc, sizeof(D3D12_STATIC_SAMPLER_DESC));
                    pSamplerDesc->Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    pSamplerDesc->AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    pSamplerDesc->AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    pSamplerDesc->AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                    pSamplerDesc->BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
                    pSamplerDesc->MinLOD           = 0.0f;
                    pSamplerDesc->MaxLOD           = D3D12_FLOAT32_MAX;
                    pSamplerDesc->MipLODBias       = 0;
                    pSamplerDesc->ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
                    pSamplerDesc->MaxAnisotropy    = 1;
                    pSamplerDesc->ShaderRegister   = cnt;
                    pSamplerDesc->RegisterSpace    = 0;
                    pSamplerDesc->ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                    cnt++;
                }

                if (pSpecularLUT) {
                    tfmat->m_pbrMaterialParameters.m_defines["ID_specularCube"] = std::to_string(cnt);
                    // pSkyDome->SetDescriptorSpec(cnt, &tfmat->m_texturesTable, cnt, &tfmat->m_samplers[cnt]);
                    pSpecularLUT->CreateCubeSRV(cnt, &tfmat->m_texturesTable);

                    auto *pSamplerDesc = &tfmat->m_samplers[cnt];

                    // specular env map sampler
                    ZeroMemory(pSamplerDesc, sizeof(D3D12_STATIC_SAMPLER_DESC));
                    pSamplerDesc->Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    pSamplerDesc->AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    pSamplerDesc->AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    pSamplerDesc->AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                    pSamplerDesc->BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
                    pSamplerDesc->MinLOD           = 0.0f;
                    pSamplerDesc->MaxLOD           = D3D12_FLOAT32_MAX;
                    pSamplerDesc->MipLODBias       = 0;
                    pSamplerDesc->ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
                    pSamplerDesc->MaxAnisotropy    = 1;
                    pSamplerDesc->ShaderRegister   = cnt;
                    pSamplerDesc->RegisterSpace    = 0;
                    pSamplerDesc->ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                    cnt++;
                }
                tfmat->m_pbrMaterialParameters.m_defines["USE_IBL"] = "1";
            }
        }

        // SSAO mask
        //
        if (bUseSSAOMask) {
            tfmat->m_pbrMaterialParameters.m_defines["ID_SSAO"] = std::to_string(cnt);
            CreateSamplerForPBR(cnt, &tfmat->m_samplers[cnt]);
            cnt++;
        }
    }

    // the SRVs for the shadows is provided externally, here we just create the #defines for the shader bindings
    //
    if (bUseShadowMask) {
        assert(cnt <= 9); // 10th slot is reserved for shadow buffer
        tfmat->m_pbrMaterialParameters.m_defines["ID_shadowBuffer"] = std::to_string(9);
        CreateSamplerForShadowBuffer(9, &tfmat->m_samplers[cnt]);
    } else {
        assert(cnt <= 9); // 10th slot is reserved for shadow buffer
        tfmat->m_pbrMaterialParameters.m_defines["ID_shadowMap"] = std::to_string(9);
        CreateSamplerForShadowMap(9, &tfmat->m_samplers[cnt]);
    }
}

//--------------------------------------------------------------------------------------
//
// OnUpdateWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::OnUpdateWindowSizeDependentResources(Texture *pSSAO) {
    for (uint32_t i = 0; i < m_materialsData.size(); i++) {
        PBRMaterial *tfmat = &m_materialsData[i];

        DefineList def = tfmat->m_pbrMaterialParameters.m_defines;

        auto id = def.find("ID_SSAO");
        if (id != def.end()) {
            int index = std::stoi(id->second);
            pSSAO->CreateSRV(index, &tfmat->m_texturesTable);
        }
    }
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::OnDestroy() {
    for (uint32_t m = 0; m < m_meshes.size(); m++) {
        PBRMesh *pMesh = &m_meshes[m];
        for (uint32_t p = 0; p < pMesh->m_pPrimitives.size(); p++) {
            PBRPrimitives *pPrimitive = &pMesh->m_pPrimitives[p];
            if (pPrimitive->m_PipelineRender) pPrimitive->m_PipelineRender->Release();
            if (pPrimitive->m_PipelineWireframeRender) pPrimitive->m_PipelineWireframeRender->Release();
            if (pPrimitive->m_RootSignature) pPrimitive->m_RootSignature->Release();
        }
    }

    m_BrdfLut.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// CreateDescriptors for a combination of material and geometry
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::CreateRootSignature(bool bUsingSkinning, DefineList &defines, PBRPrimitives *pPrimitive, bool bUseSSAOMask) {
    int                      rootParamCnt = 0;
    CD3DX12_ROOT_PARAMETER   rootParameter[6];
    int                      desccRangeCnt = 0;
    CD3DX12_DESCRIPTOR_RANGE descRange[3];

    // b0 <- Constant buffer 'per frame'
    {
        rootParameter[rootParamCnt].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParamCnt++;
    }

    // textures table
    if (pPrimitive->m_pMaterial->m_textureCount > 0) {
        descRange[desccRangeCnt].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, pPrimitive->m_pMaterial->m_textureCount, 0); // texture table
        rootParameter[rootParamCnt].InitAsDescriptorTable(1, &descRange[desccRangeCnt], D3D12_SHADER_VISIBILITY_PIXEL);
        desccRangeCnt++;
        rootParamCnt++;
    }

    // shadow buffer (only if we are doing lighting, for example in the forward pass)
    if (m_doLighting) {
        descRange[desccRangeCnt].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MaxShadowInstances, 9); // shadow buffer
        rootParameter[rootParamCnt].InitAsDescriptorTable(1, &descRange[desccRangeCnt], D3D12_SHADER_VISIBILITY_PIXEL);
        desccRangeCnt++;
        rootParamCnt++;
    }

    // b1 <- Constant buffer 'per object', these are mainly the material data
    {
        rootParameter[rootParamCnt].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParamCnt++;
    }

    // b2 <- Constant buffer holding the skinning matrices
    if (bUsingSkinning) {
        rootParameter[rootParamCnt].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        defines["ID_SKINNING_MATRICES"] = std::to_string(2);
        rootParamCnt++;
    }

    // the root signature contains up to 5 slots to be used
    CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
    descRootSignature.pParameters                 = rootParameter;
    descRootSignature.NumParameters               = rootParamCnt;
    descRootSignature.pStaticSamplers             = pPrimitive->m_pMaterial->m_samplers;
    descRootSignature.NumStaticSamplers           = pPrimitive->m_pMaterial->m_textureCount;
    descRootSignature.NumStaticSamplers += 1; // account for shadow sampler

    // deny uneccessary access to certain pipeline stages
    descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                              D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                              D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ID3DBlob *pOutBlob, *pErrorBlob = NULL;
    ThrowIfFailed(D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
    ThrowIfFailed(m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&pPrimitive->m_RootSignature)));
    SetName(pPrimitive->m_RootSignature, "GltfPbr::m_RootSignature");

    pOutBlob->Release();
    if (pErrorBlob) pErrorBlob->Release();
}

//--------------------------------------------------------------------------------------
//
// CreatePipeline
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::CreatePipeline(std::vector<D3D12_INPUT_ELEMENT_DESC> layout, const DefineList &defines, PBRPrimitives *pPrimitive) {
    // Compile and create shaders
    //
    D3D12_SHADER_BYTECODE shaderVert, shaderPixel;
    CompileShaderFromFile("GltfPbrPass-VS.hlsl", &defines, "mainVS", "-T vs_6_0 -Zi -Od", &shaderVert);
    CompileShaderFromFile("GltfPbrPass-PS.hlsl", &defines, "mainPS", "-T ps_6_0 -Zi -Od", &shaderPixel);

    // Set blending
    //
    CD3DX12_BLEND_DESC blendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blendState.RenderTarget[0]    = D3D12_RENDER_TARGET_BLEND_DESC{
        (defines.Has("DEF_alphaMode_BLEND")),
        FALSE,
        D3D12_BLEND_SRC_ALPHA,
        D3D12_BLEND_INV_SRC_ALPHA,
        D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE,
        D3D12_BLEND_ZERO,
        D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };

    // Create a PSO description
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPso = {};
    descPso.InputLayout                        = {layout.data(), (UINT)layout.size()};
    descPso.pRootSignature                     = pPrimitive->m_RootSignature;
    descPso.VS                                 = shaderVert;
    descPso.PS                                 = shaderPixel;
    descPso.RasterizerState                    = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPso.RasterizerState.CullMode           = (pPrimitive->m_pMaterial->m_pbrMaterialParameters.m_doubleSided) ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_FRONT;
    descPso.BlendState                         = blendState;
    descPso.DepthStencilState                  = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    descPso.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    descPso.SampleMask                         = UINT_MAX;
    descPso.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    descPso.NumRenderTargets                   = (UINT)m_outFormats.size();
    for (size_t i = 0; i < m_outFormats.size(); i++) {
        descPso.RTVFormats[i] = m_outFormats[i];
    }
    descPso.DSVFormat        = DXGI_FORMAT_D32_FLOAT;
    descPso.SampleDesc.Count = m_sampleCount;
    descPso.NodeMask         = 0;

    ThrowIfFailed(m_pDevice->GetDevice()->CreateGraphicsPipelineState(&descPso, IID_PPV_ARGS(&pPrimitive->m_PipelineRender)));
    SetName(pPrimitive->m_PipelineRender, "RTGltfPbrPass::m_PipelineRender");

    // create wireframe pipeline
    descPso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    descPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_pDevice->GetDevice()->CreateGraphicsPipelineState(&descPso, IID_PPV_ARGS(&pPrimitive->m_PipelineWireframeRender)));
    SetName(pPrimitive->m_PipelineWireframeRender, "RTGltfPbrPass::m_PipelineWireframeRender");
}

//--------------------------------------------------------------------------------------
//
// BuildLists
//
//--------------------------------------------------------------------------------------
void RTGltfPbrPass::BuildBatchLists(std::vector<BatchList> *pSolid, std::vector<BatchList> *pTransparent, bool bWireframe /*=false*/) {
    // loop through nodes
    //
    std::vector<tfNode> *pNodes         = &m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_nodes;
    Matrix2 *            pNodesMatrices = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_worldSpaceMats.data();

    for (uint32_t i = 0; i < pNodes->size(); i++) {
        tfNode *pNode = &pNodes->at(i);
        if ((pNode == NULL) || (pNode->meshIndex < 0)) continue;

        // skinning matrices constant buffer
        D3D12_GPU_VIRTUAL_ADDRESS pPerSkeleton = m_pGLTFTexturesAndBuffers->GetSkinningMatricesBuffer(pNode->skinIndex);

        math::Matrix4 mModelViewProj = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_perFrameData.mCameraCurrViewProj * pNodesMatrices[i].GetCurrent();

        // loop through primitives
        //
        PBRMesh *pMesh = &m_meshes[pNode->meshIndex];
        for (uint32_t p = 0; p < pMesh->m_pPrimitives.size(); p++) {
            PBRPrimitives *pPrimitive = &pMesh->m_pPrimitives[p];

            if ((bWireframe && pPrimitive->m_PipelineWireframeRender == NULL) || (!bWireframe && pPrimitive->m_PipelineRender == NULL)) continue;

            // do frustum culling
            //
            tfPrimitives boundingBox = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_meshes[pNode->meshIndex].m_pPrimitives[p];
            if (CameraFrustumToBoxCollision(mModelViewProj, boundingBox.m_center, boundingBox.m_radius)) continue;

            PBRMaterialParameters *pPbrParams = &pPrimitive->m_pMaterial->m_pbrMaterialParameters;

            // Set per Object constants from material
            //
            per_object cbPerObject;
            cbPerObject.mCurrentWorld               = pNodesMatrices[i].GetCurrent();
            cbPerObject.mPreviousWorld              = pNodesMatrices[i].GetPrevious();
            cbPerObject.m_pbrParams                 = pPbrParams->m_params;
            D3D12_GPU_VIRTUAL_ADDRESS perObjectDesc = m_pDynamicBufferRing->AllocConstantBuffer(sizeof(per_object), &cbPerObject);

            // compute depth for sorting
            //
            math::Vector4 v     = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_meshes[pNode->meshIndex].m_pPrimitives[p].m_center;
            float         depth = (mModelViewProj * v).getW();

            BatchList t;
            t.m_depth         = depth;
            t.m_pPrimitive    = pPrimitive;
            t.m_perFrameDesc  = m_pGLTFTexturesAndBuffers->GetPerFrameConstants();
            t.m_perObjectDesc = perObjectDesc;
            t.m_pPerSkeleton  = pPerSkeleton;

            // append primitive to list
            //
            if (pPbrParams->m_blending == false) {
                pSolid->push_back(t);
            } else {
                pTransparent->push_back(t);
            }
        }
    }
}

void RTGltfPbrPass::DrawBatchList(ID3D12GraphicsCommandList *pCommandList, CBV_SRV_UAV *pShadowBufferSRV, std::vector<BatchList> *pBatchList, bool bWireframe /*=false*/) {
    UserMarker marker(pCommandList, "RTGltfPbrPass::DrawBatchList");

    // Set descriptor heaps
    pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12DescriptorHeap *pDescriptorHeaps[] = {m_pResourceViewHeaps->GetCBV_SRV_UAVHeap(), m_pResourceViewHeaps->GetSamplerHeap()};
    pCommandList->SetDescriptorHeaps(2, pDescriptorHeaps);

    ID3D12PipelineState *pCurBoundPSO = NULL;
    for (auto &t : *pBatchList) {
        ID3D12PipelineState *pCurPSO = NULL;
        if (bWireframe)
            pCurPSO = (t.m_pPrimitive->m_PipelineWireframeRender);
        else
            pCurPSO = (t.m_pPrimitive->m_PipelineRender);
        if (pCurPSO != pCurBoundPSO) {
            pCommandList->SetPipelineState(pCurPSO);
            pCurBoundPSO = pCurPSO;
        }
        t.m_pPrimitive->DrawPrimitive(pCommandList, pShadowBufferSRV, t.m_perFrameDesc, t.m_perObjectDesc, t.m_pPerSkeleton, bWireframe);
    }
}

void PBRPrimitives::DrawPrimitive(ID3D12GraphicsCommandList *pCommandList, CBV_SRV_UAV *pShadowBufferSRV, D3D12_GPU_VIRTUAL_ADDRESS perFrameDesc,
                                  D3D12_GPU_VIRTUAL_ADDRESS perObjectDesc, D3D12_GPU_VIRTUAL_ADDRESS pPerSkeleton, bool bWireframe) {
    // Bind indices and vertices using the right offsets into the buffer
    //
    pCommandList->IASetIndexBuffer(&m_geometry.m_IBV);
    pCommandList->IASetVertexBuffers(0, (UINT)m_geometry.m_VBV.size(), m_geometry.m_VBV.data());

    // Bind Descriptor sets
    //
    pCommandList->SetGraphicsRootSignature(m_RootSignature);
    int paramIndex = 0;

    // bind the per scene constant buffer descriptor
    pCommandList->SetGraphicsRootConstantBufferView(paramIndex++, perFrameDesc);

    // bind the textures and samplers descriptors
    if (m_pMaterial->m_textureCount > 0) {
        pCommandList->SetGraphicsRootDescriptorTable(paramIndex++, m_pMaterial->m_texturesTable.GetGPU());
    }

    // bind the shadow buffer
    if (pShadowBufferSRV) {
        pCommandList->SetGraphicsRootDescriptorTable(paramIndex++, pShadowBufferSRV->GetGPU());
    }

    // bind the per object constant buffer descriptor
    pCommandList->SetGraphicsRootConstantBufferView(paramIndex++, perObjectDesc);

    // bind the skeleton bind matrices constant buffer descriptor
    if (pPerSkeleton != 0) pCommandList->SetGraphicsRootConstantBufferView(paramIndex++, pPerSkeleton);

    // Bind Pipeline
    //

    // Draw
    //
    pCommandList->DrawIndexedInstanced(m_geometry.m_NumIndices, 1, 0, 0, 0);
}

///////////////////
// Ray Tracing   //
///////////////////
ID3D12Resource *RTGltfPbrPass::CreateBLASForSurfaces(ID3D12GraphicsCommandList5 *pCommandList, std::vector<uint32_t> const &surfaceIDs) {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries{};
    bool                                        has_skeletal = false;
    for (uint32_t i = 0; i < surfaceIDs.size(); i++) {

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

        hlsl::Surface_Info surface_info = m_infoTables.m_cpuSurfaceBuffer[surfaceIDs[i]];

        if (surface_info.joints_attribute_offset >= 0 || surface_info.weight_attribute_offset >= 0) has_skeletal = true;

        geometryDesc.Triangles.VertexBuffer.StartAddress =
            (D3D12_GPU_VIRTUAL_ADDRESS)(surface_info.position_attribute_offset * 4) + m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = 12;
        geometryDesc.Triangles.VertexCount                = surface_info.num_vertices;
        geometryDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.IndexBuffer  = (D3D12_GPU_VIRTUAL_ADDRESS)(surface_info.index_offset * 4) + m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress();
        geometryDesc.Triangles.IndexFormat  = surface_info.index_type == SURFACE_INFO_INDEX_TYPE_U32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        geometryDesc.Triangles.IndexCount   = surface_info.num_indices;
        geometryDesc.Triangles.Transform3x4 = 0;

        bool opaque        = m_infoTables.m_cpuMaterialBuffer[surface_info.material_id < 0 ? 0 : surface_info.material_id].is_opaque;
        geometryDesc.Flags = !opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geometries.push_back(geometryDesc);
    }
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags =
        has_skeletal ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                     : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
        //| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION
        ;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS InputInfo   = {};
    InputInfo.Type                                                   = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    InputInfo.DescsLayout                                            = D3D12_ELEMENTS_LAYOUT_ARRAY;
    InputInfo.pGeometryDescs                                         = &geometries[0];
    InputInfo.NumDescs                                               = (uint32_t)geometries.size();
    InputInfo.Flags                                                  = buildFlags;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO BuildSizes = {};
    m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&InputInfo, &BuildSizes);
    // if (has_skeletal) {
    ID3D12Resource *pBlasBuffer = CreateGPULocalUAVBuffer(BuildSizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs                                             = InputInfo;
    ID3D12Resource *pScratch                                     = m_infoTables.getScratchBuffer(BuildSizes.ScratchDataSizeInBytes);
    buildDesc.ScratchAccelerationStructureData                   = pScratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData                      = pBlasBuffer->GetGPUVirtualAddress();

    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pBlasBuffer),
                           });
    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pBlasBuffer),
                           });

    return pBlasBuffer;
    //} else { // Compact
    //         // TODO: Use the actual compaction size
    //         // Currently try to minimize memory BW
    //    ID3D12Resource *pDstBlasBuffer = CreateGPULocalUAVBuffer(BuildSizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    //    ID3D12Resource *pTmpBlasBuffer = m_infoTables.getTMPBLASBuffer(BuildSizes.ResultDataMaxSizeInBytes);
    //    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    //    buildDesc.Inputs                                             = InputInfo;
    //    ID3D12Resource *pScratch                                     = m_infoTables.getScratchBuffer(BuildSizes.ScratchDataSizeInBytes);
    //    buildDesc.ScratchAccelerationStructureData                   = pScratch->GetGPUVirtualAddress();
    //    buildDesc.DestAccelerationStructureData                      = pTmpBlasBuffer->GetGPUVirtualAddress();

    //    Barriers(pCommandList, {
    //                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
    //                               CD3DX12_RESOURCE_BARRIER::UAV(pTmpBlasBuffer),
    //                           });
    //    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    //    Barriers(pCommandList, {
    //                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
    //                               CD3DX12_RESOURCE_BARRIER::UAV(pTmpBlasBuffer),
    //                           });
    //    pCommandList->CopyRaytracingAccelerationStructure(pDstBlasBuffer->GetGPUVirtualAddress(), pTmpBlasBuffer->GetGPUVirtualAddress(),
    //                                                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
    //    Barriers(pCommandList, {
    //                               CD3DX12_RESOURCE_BARRIER::UAV(pDstBlasBuffer),
    //                               CD3DX12_RESOURCE_BARRIER::UAV(pTmpBlasBuffer),
    //                           });
    //    return pDstBlasBuffer;
    //}
}
void RTGltfPbrPass::UpdateBLASForSurfaces(ID3D12GraphicsCommandList5 *pCommandList, std::vector<uint32_t> const &surfaceIDs, ID3D12Resource *pBLAS) {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries{};
    bool                                        has_skeletal = false;
    for (uint32_t i = 0; i < surfaceIDs.size(); i++) {
        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

        hlsl::Surface_Info surface_info = m_infoTables.m_cpuSurfaceBuffer[surfaceIDs[i]];

        if (surface_info.joints_attribute_offset >= 0 || surface_info.weight_attribute_offset >= 0) has_skeletal = true;

        geometryDesc.Triangles.VertexBuffer.StartAddress =
            (D3D12_GPU_VIRTUAL_ADDRESS)(surface_info.position_attribute_offset * 4) + m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = 12;
        geometryDesc.Triangles.VertexCount                = surface_info.num_vertices;
        geometryDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.IndexBuffer  = (D3D12_GPU_VIRTUAL_ADDRESS)(surface_info.index_offset * 4) + m_infoTables.m_pSrcGeometryBufferResource->GetGPUVirtualAddress();
        geometryDesc.Triangles.IndexFormat  = surface_info.index_type == SURFACE_INFO_INDEX_TYPE_U32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        geometryDesc.Triangles.IndexCount   = surface_info.num_indices;
        geometryDesc.Triangles.Transform3x4 = 0;

        bool opaque        = m_infoTables.m_cpuMaterialBuffer[surface_info.material_id < 0 ? 0 : surface_info.material_id].is_opaque;
        geometryDesc.Flags = !opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geometries.push_back(geometryDesc);
    }
    // Nothing to update
    if (!has_skeletal) return;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                                                                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE |
                                                                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS InputInfo   = {};
    InputInfo.Type                                                   = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    InputInfo.DescsLayout                                            = D3D12_ELEMENTS_LAYOUT_ARRAY;
    InputInfo.pGeometryDescs                                         = &geometries[0];
    InputInfo.NumDescs                                               = (uint32_t)geometries.size();
    InputInfo.Flags                                                  = buildFlags;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO BuildSizes = {};
    m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&InputInfo, &BuildSizes);

    auto desc = pBLAS->GetDesc();
    assert(desc.Width >= BuildSizes.ResultDataMaxSizeInBytes);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs                                             = InputInfo;
    ID3D12Resource *pScratch                                     = m_infoTables.getScratchBuffer(BuildSizes.ScratchDataSizeInBytes);
    buildDesc.ScratchAccelerationStructureData                   = pScratch->GetGPUVirtualAddress();
    buildDesc.SourceAccelerationStructureData                    = pBLAS->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData                      = pBLAS->GetGPUVirtualAddress();
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pBLAS),
                           });
    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pBLAS),
                           });
}
void RTGltfPbrPass::InitializeAccelerationStructures(CBV_SRV_UAV *pGlobalTable) {
    if (m_pDevice->IsRT11Supported() == false) return;
    // m_pGLTFTexturesAndBuffers->m_pGLTFCommon->TransformScene(0, Vectormath::Matrix4::identity());
    // if (m_pDevice->GetGraphicsAnalysis())
    //    m_pDevice->GetGraphicsAnalysis()->BeginCapture();
    // PIXBeginCapture(PIX_CAPTURE_GPU, );
    m_infoTables.ReleaseAccelerationStructuresOnly();
    m_infoTables.m_bakeSkinning.OnCreate(m_pDevice, m_pResourceViewHeaps);

    ID3D12GraphicsCommandList5 *pCommandList      = NULL;
    ID3D12CommandAllocator *    pCommandAllocator = NULL;
    m_pDevice->GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator));
    SetName(pCommandAllocator, "RTGltfPbrPass::InitializeBLASes::pCommandAllocator");
    m_pDevice->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCommandAllocator, nullptr, IID_PPV_ARGS(&pCommandList));
    SetName(pCommandList, "RTGltfPbrPass::InitializeBLASes::pCommandList");

    m_infoTables.m_scene_is_ready = true;

    UpdateAccelerationStructures(pCommandList, pGlobalTable);

    ThrowIfFailed(pCommandList->Close());
    m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CommandListCast(&pCommandList));
    m_pDevice->GPUFlush();
    pCommandAllocator->Reset();
    pCommandList->Reset(pCommandAllocator, nullptr);
    pCommandList->Release();
    pCommandAllocator->Release();

    m_infoTables.FlushScratchBuffers();
}
ID3D12Resource *RTGltfPbrPass::CreateTLASForInstances(ID3D12GraphicsCommandList5 *pCommandList, ID3D12Resource *pInstances, uint32_t numInstaces) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS InputInfo = {};
    InputInfo.Type                                                 = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    InputInfo.DescsLayout                                          = D3D12_ELEMENTS_LAYOUT_ARRAY;
    InputInfo.InstanceDescs                                        = pInstances->GetGPUVirtualAddress();
    InputInfo.NumDescs                                             = numInstaces;
    InputInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO BuildSizes = {};
    m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&InputInfo, &BuildSizes);

    ID3D12Resource *pTLASBuffer = CreateGPULocalUAVBuffer(BuildSizes.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    ID3D12Resource *pScratch    = m_infoTables.getScratchBuffer(BuildSizes.ScratchDataSizeInBytes);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs                                             = InputInfo;
    buildDesc.DestAccelerationStructureData                      = pTLASBuffer->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData                   = pScratch->GetGPUVirtualAddress();
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pInstances),
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pTLASBuffer),
                           });
    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pTLASBuffer),
                           });
    return pTLASBuffer;
}
void RTGltfPbrPass::UpdateTLASForInstances(ID3D12GraphicsCommandList5 *pCommandList, ID3D12Resource *pInstances, uint32_t numInstaces, ID3D12Resource *pTLAS) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS InputInfo = {};
    InputInfo.Type                                                 = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    InputInfo.DescsLayout                                          = D3D12_ELEMENTS_LAYOUT_ARRAY;
    InputInfo.InstanceDescs                                        = pInstances->GetGPUVirtualAddress();
    InputInfo.NumDescs                                             = numInstaces;
    InputInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO BuildSizes = {};
    m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&InputInfo, &BuildSizes);
    auto desc = pTLAS->GetDesc();
    assert(desc.Width >= BuildSizes.ResultDataMaxSizeInBytes);

    ID3D12Resource *pScratch = m_infoTables.getScratchBuffer(BuildSizes.ScratchDataSizeInBytes);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs                                             = InputInfo;
    buildDesc.DestAccelerationStructureData                      = pTLAS->GetGPUVirtualAddress();
    buildDesc.SourceAccelerationStructureData                    = pTLAS->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData                   = pScratch->GetGPUVirtualAddress();
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pInstances),
                               CD3DX12_RESOURCE_BARRIER::UAV(pTLAS),
                           });
    pCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::UAV(pScratch),
                               CD3DX12_RESOURCE_BARRIER::UAV(pTLAS),
                           });
}
void RTGltfPbrPass::UpdateAccelerationStructures(ID3D12GraphicsCommandList5 *pCommandList, CBV_SRV_UAV *pGlobalTable) {

    if (m_pDevice->IsRT11Supported() == false || m_infoTables.m_scene_is_ready == false) return;

    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::Transition(m_infoTables.m_pSrcGeometryBufferResource,
                                                                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                           });
    BakeSkeletalAnimation(pCommandList, pGlobalTable);
    Barriers(pCommandList, {
                               CD3DX12_RESOURCE_BARRIER::Transition(m_infoTables.m_pSrcGeometryBufferResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                           });
    {
        UserMarker marker(pCommandList, "RTGltfPbrPass::UpdateAccelerationStructures::UpdateBLASes");
        for (auto &item : m_infoTables.m_blas_table) {
            if (m_infoTables.m_skinned_surface_sets.find(item.second.second) == m_infoTables.m_skinned_surface_sets.end()) continue;
            UpdateBLASForSurfaces(pCommandList, item.first, item.second.first);
        }
    }
    {
        UserMarker marker(pCommandList, "RTGltfPbrPass::UpdateAccelerationStructures::BuildBLASes");

        m_infoTables.m_cpuOpaqueTLASInstances.resize(m_infoTables.m_cpuInstanceBuffer.size());
        m_infoTables.m_cpuTransparentTLASInstances.resize(m_infoTables.m_cpuInstanceBuffer.size());
        m_infoTables.m_cpuGlobalTLASInstances.resize(m_infoTables.m_cpuInstanceBuffer.size());

        std::vector<uint32_t> opaque_surface_ids;
        std::vector<uint32_t> transparent_surface_ids;
        std::vector<uint32_t> global_surface_ids;
        m_infoTables.m_cpuInstanceTransformBuffer.resize(m_infoTables.m_cpuInstanceBuffer.size());
        opaque_surface_ids.reserve(100);
        transparent_surface_ids.reserve(100);
        global_surface_ids.reserve(100);
        for (uint32_t instance_id = 0; instance_id < m_infoTables.m_cpuInstanceBuffer.size(); instance_id++) {
            auto const &        instance_info  = m_infoTables.m_cpuInstanceBuffer[instance_id];
            Vectormath::Matrix4 prevTransform  = m_infoTables.m_cpuInstanceTransformBuffer[instance_id];
            Matrix2             pNodesMatrices = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->m_worldSpaceMats[instance_info.node_id];
            Vectormath::Matrix4 transform      = pNodesMatrices.GetCurrent();

            m_infoTables.m_cpuInstanceTransformBuffer[instance_id] = transform;

            opaque_surface_ids.resize(0);
            transparent_surface_ids.resize(0);
            global_surface_ids.resize(0);

            uint64_t opaque_surface_hash      = 0;
            uint64_t transparent_surface_hash = 0;
            uint64_t global_surface_hash      = 0;

            for (uint32_t surface_id_offset = 0; surface_id_offset < (uint32_t)instance_info.num_opaque_surfaces; surface_id_offset++) {
                uint32_t surface_id = m_infoTables.m_cpuSurfaceIDsBuffer[(int32_t)surface_id_offset + (int32_t)instance_info.surface_id_table_offset];
                opaque_surface_ids.push_back(surface_id);
                transparent_surface_ids.push_back(surface_id);
                global_surface_ids.push_back(surface_id);

                opaque_surface_hash ^= std::hash<uint32_t>()(surface_id);
                transparent_surface_hash ^= std::hash<uint32_t>()(surface_id);
                global_surface_hash ^= std::hash<uint32_t>()(surface_id);
            }
            uint32_t num_transparent_surfaces = instance_info.num_surfaces - instance_info.num_opaque_surfaces;
            for (uint32_t surface_id_offset = 0; surface_id_offset < num_transparent_surfaces; surface_id_offset++) {
                uint32_t surface_id =
                    m_infoTables.m_cpuSurfaceIDsBuffer[(int32_t)surface_id_offset + (int32_t)instance_info.surface_id_table_offset + (int32_t)instance_info.num_opaque_surfaces];
                transparent_surface_ids.push_back(surface_id);
                global_surface_ids.push_back(surface_id);

                transparent_surface_hash ^= std::hash<uint32_t>()(surface_id);
                global_surface_hash ^= std::hash<uint32_t>()(surface_id);
            }

            float transform_diff = Vectormath::SSE::lengthSqr(prevTransform[0] - transform[0])   //
                                   + Vectormath::SSE::lengthSqr(prevTransform[1] - transform[1]) //
                                   + Vectormath::SSE::lengthSqr(prevTransform[2] - transform[2]) //
                                   + Vectormath::SSE::lengthSqr(prevTransform[3] - transform[3]) //
                ;
            bool needs_skin_update = false;
            if (!needs_skin_update) needs_skin_update = m_infoTables.m_skinned_surface_sets.find(transparent_surface_hash) != m_infoTables.m_skinned_surface_sets.end();
            if (!needs_skin_update) needs_skin_update = m_infoTables.m_skinned_surface_sets.find(opaque_surface_hash) != m_infoTables.m_skinned_surface_sets.end();
            if (!needs_skin_update) needs_skin_update = m_infoTables.m_skinned_surface_sets.find(global_surface_hash) != m_infoTables.m_skinned_surface_sets.end();

            if (!needs_skin_update) needs_skin_update = m_infoTables.m_hash_blas_table.find(transparent_surface_hash) == m_infoTables.m_hash_blas_table.end();
            if (!needs_skin_update) needs_skin_update = m_infoTables.m_hash_blas_table.find(opaque_surface_hash) == m_infoTables.m_hash_blas_table.end();
            if (!needs_skin_update) needs_skin_update = m_infoTables.m_hash_blas_table.find(global_surface_hash) == m_infoTables.m_hash_blas_table.end();

            if (!needs_skin_update && transform_diff < 1.0e-12f) continue;

            ID3D12Resource *pOpaqueBLAS      = NULL;
            ID3D12Resource *pTransparentBLAS = NULL;
            ID3D12Resource *pGlobalBLAS      = NULL;
            if (opaque_surface_ids.size()) {
                if (m_infoTables.m_blas_table.find(opaque_surface_ids) != m_infoTables.m_blas_table.end()) {
                    pOpaqueBLAS = m_infoTables.m_blas_table.find(opaque_surface_ids)->second.first;
                } else {
                    pOpaqueBLAS                                         = CreateBLASForSurfaces(pCommandList, opaque_surface_ids);
                    m_infoTables.m_blas_table[opaque_surface_ids]       = {pOpaqueBLAS, opaque_surface_hash};
                    m_infoTables.m_hash_blas_table[opaque_surface_hash] = pOpaqueBLAS;
                }
            }
            if (transparent_surface_ids.size()) {
                if (m_infoTables.m_blas_table.find(transparent_surface_ids) != m_infoTables.m_blas_table.end()) {
                    pTransparentBLAS = m_infoTables.m_blas_table.find(transparent_surface_ids)->second.first;
                } else {
                    pTransparentBLAS                                         = CreateBLASForSurfaces(pCommandList, transparent_surface_ids);
                    m_infoTables.m_blas_table[transparent_surface_ids]       = {pTransparentBLAS, transparent_surface_hash};
                    m_infoTables.m_hash_blas_table[transparent_surface_hash] = pTransparentBLAS;
                }
            }
            if (global_surface_ids.size()) {
                if (m_infoTables.m_blas_table.find(global_surface_ids) != m_infoTables.m_blas_table.end()) {
                    pGlobalBLAS = m_infoTables.m_blas_table.find(global_surface_ids)->second.first;
                } else {
                    pGlobalBLAS                                         = CreateBLASForSurfaces(pCommandList, global_surface_ids);
                    m_infoTables.m_blas_table[global_surface_ids]       = {pGlobalBLAS, global_surface_hash};
                    m_infoTables.m_hash_blas_table[global_surface_hash] = pGlobalBLAS;
                }
            }
            D3D12_RAYTRACING_INSTANCE_DESC opaqueInstance{};
            D3D12_RAYTRACING_INSTANCE_DESC transparentInstance{};
            D3D12_RAYTRACING_INSTANCE_DESC globalInstance{};

            opaqueInstance.Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            opaqueInstance.InstanceContributionToHitGroupIndex = 0;
            opaqueInstance.InstanceID                          = instance_id;
            opaqueInstance.InstanceMask                        = 0xffu;

            for (uint32_t j = 0; j < 4; j++) {
                opaqueInstance.Transform[0][j] = transform.getRow(0).getElem(j);
                opaqueInstance.Transform[1][j] = transform.getRow(1).getElem(j);
                opaqueInstance.Transform[2][j] = transform.getRow(2).getElem(j);
            }
            transparentInstance       = opaqueInstance;
            globalInstance            = opaqueInstance;
            opaqueInstance.Flags      = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
            globalInstance.Flags      = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
            transparentInstance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;

            if (pOpaqueBLAS) {
                opaqueInstance.AccelerationStructure               = pOpaqueBLAS->GetGPUVirtualAddress();
                m_infoTables.m_cpuOpaqueTLASInstances[instance_id] = opaqueInstance;
            }
            if (pTransparentBLAS) {
                transparentInstance.AccelerationStructure               = pTransparentBLAS->GetGPUVirtualAddress();
                m_infoTables.m_cpuTransparentTLASInstances[instance_id] = transparentInstance;
            }
            if (pGlobalBLAS) {
                globalInstance.AccelerationStructure               = pGlobalBLAS->GetGPUVirtualAddress();
                m_infoTables.m_cpuGlobalTLASInstances[instance_id] = globalInstance;
            }
        }
    }
    {
        UserMarker marker(pCommandList, "RTGltfPbrPass::UpdateAccelerationStructures::InstanceBuffers");
        auto       copy = [this](ID3D12GraphicsCommandList5 *pCommandList, ID3D12Resource *&pGpuBuffer, ID3D12Resource *&pCpuvizBuffer,
                           std::vector<D3D12_RAYTRACING_INSTANCE_DESC> const &cpuData) {
            if (cpuData.size()) {
                size_t neededSize = cpuData.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
                if (pCpuvizBuffer == NULL || pCpuvizBuffer->GetDesc().Width < neededSize) {
                    if (pCpuvizBuffer) pCpuvizBuffer->Release();
                    pCpuvizBuffer = CreateUploadBuffer(neededSize);
                }
                if (pGpuBuffer == NULL || pGpuBuffer->GetDesc().Width < neededSize) {
                    if (pGpuBuffer) pGpuBuffer->Release();
                    pGpuBuffer = CreateGPULocalUAVBuffer(neededSize, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                void *pData = NULL;
                pCpuvizBuffer->Map(0, NULL, &pData);
                memcpy(pData, &cpuData[0], neededSize);
                pCpuvizBuffer->Unmap(0, NULL);
                Barriers(pCommandList, {CD3DX12_RESOURCE_BARRIER::Transition(pGpuBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)});
                pCommandList->CopyBufferRegion(pGpuBuffer, 0, pCpuvizBuffer, 0, neededSize);
                Barriers(pCommandList, {CD3DX12_RESOURCE_BARRIER::Transition(pGpuBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)});
            }
        };

        copy(pCommandList, m_infoTables.m_pOpaqueTLASInstances, m_infoTables.m_pCpuvizOpaqueTLASInstances, m_infoTables.m_cpuOpaqueTLASInstances);
        copy(pCommandList, m_infoTables.m_pTransparentTLASInstances, m_infoTables.m_pCpuvizTransparentTLASInstances, m_infoTables.m_cpuTransparentTLASInstances);
        copy(pCommandList, m_infoTables.m_pGlobalTLASInstances, m_infoTables.m_pCpuvizGlobalTLASInstances, m_infoTables.m_cpuGlobalTLASInstances);
        if (m_infoTables.m_pOpaqueTLASInstances) SetName(m_infoTables.m_pOpaqueTLASInstances, "m_infoTables.m_pOpaqueTLASInstances");
        if (m_infoTables.m_pTransparentTLASInstances) SetName(m_infoTables.m_pTransparentTLASInstances, "m_infoTables.m_pTransparentTLASInstances");
        if (m_infoTables.m_pGlobalTLASInstances) SetName(m_infoTables.m_pGlobalTLASInstances, "m_infoTables.m_pGlobalTLASInstances");
    }

    {
        UserMarker marker(pCommandList, "RTGltfPbrPass::UpdateAccelerationStructures::TLASes");

        auto initTLAS = [this](ID3D12GraphicsCommandList5 *pCommandList, ID3D12Resource *&pTLAS, ID3D12Resource *pInstances, size_t cnt) {
            if (cnt) {
                if (pTLAS == NULL)
                    pTLAS = CreateTLASForInstances(pCommandList, pInstances, (uint32_t)cnt);
                else
                    UpdateTLASForInstances(pCommandList, pInstances, (uint32_t)cnt, pTLAS);
            }
        };
        initTLAS(pCommandList, m_infoTables.m_pOpaqueTlas, m_infoTables.m_pOpaqueTLASInstances, m_infoTables.m_cpuOpaqueTLASInstances.size());
        initTLAS(pCommandList, m_infoTables.m_pTranpsparentTlas, m_infoTables.m_pTransparentTLASInstances, m_infoTables.m_cpuTransparentTLASInstances.size());
        initTLAS(pCommandList, m_infoTables.m_pGlobalTlas, m_infoTables.m_pGlobalTLASInstances, m_infoTables.m_cpuGlobalTLASInstances.size());
    }

    m_infoTables.UpdateDescriptorTable(pGlobalTable);
}
ID3D12Resource *RTGltfPbrPass::CreateUploadBuffer(size_t size, D3D12_RESOURCE_STATES state) {
    ID3D12Resource *buf = NULL;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment        = 0;
    desc.DepthOrArraySize = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_NONE;
    desc.Flags |= D3D12_RESOURCE_FLAG_NONE;
    desc.Format             = DXGI_FORMAT_UNKNOWN;
    desc.Height             = 1;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.MipLevels          = 1;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Width              = size;
    desc.Width              = ((desc.Width + 0xffU) & ~0xffu);
    desc.Alignment          = max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    desc.Width              = ((desc.Width + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) / D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT) *
                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    {
        D3D12_HEAP_PROPERTIES prop{};
        prop.Type              = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
        HRESULT          hr    = m_pDevice->GetDevice()->CreateCommittedResource(&prop, flags, &desc, state, NULL, IID_PPV_ARGS(&buf));
        if (!SUCCEEDED(hr)) throw 0;
    }
    return buf;
}
ID3D12Resource *RTGltfPbrPass::CreateGPULocalUAVBuffer(size_t size, D3D12_RESOURCE_STATES state) {
    ID3D12Resource *buf = NULL;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment        = 0;
    desc.DepthOrArraySize = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_NONE;
    desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Format             = DXGI_FORMAT_UNKNOWN;
    desc.Height             = 1;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.MipLevels          = 1;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Width              = size;
    desc.Width              = ((desc.Width + 0xffU) & ~0xffu);
    desc.Alignment          = max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    desc.Width              = ((desc.Width + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1) / D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT) *
                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    {
        D3D12_HEAP_PROPERTIES prop{};
        prop.Type              = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
        HRESULT          hr    = m_pDevice->GetDevice()->CreateCommittedResource(&prop, flags, &desc, state, NULL, IID_PPV_ARGS(&buf));
        if (!SUCCEEDED(hr)) throw 0;
    }
    return buf;
}

void RTGltfPbrPass::RTInfoTables::UpdateDescriptorTable(CBV_SRV_UAV *pGlobalTable) {

    auto bindTlas = [=](ID3D12Resource *pTLAS, uint32_t slot) {
        if (pTLAS) {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format                                   = DXGI_FORMAT_UNKNOWN;
            desc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            desc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.RaytracingAccelerationStructure.Location = pTLAS->GetGPUVirtualAddress();
            m_pParent->m_pDevice->GetDevice()->CreateShaderResourceView(NULL, &desc, pGlobalTable->GetCPU(GDT_TLAS_HEAP_OFFSET + slot));
        }
    };

    bindTlas(m_pOpaqueTlas, GDT_TLAS_OPAQUE_SLOT);
    bindTlas(m_pTranpsparentTlas, GDT_TLAS_TRANSPARENT_SLOT);
    bindTlas(m_pGlobalTlas, GDT_TLAS_GLOBAL_SLOT);
}

///////////////////
///////////////////
///////////////////
} // namespace RTCAULDRON_DX12