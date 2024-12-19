//
// Created by loulfy on 03/03/2024.
//

#pragma once

#include <glm/glm.hpp>

namespace ler::render
{
struct alignas(16) DrawConstant
{
    glm::mat4 proj = glm::mat4(1.f);
    glm::mat4 view = glm::mat4(1.f);
};

struct alignas(16) Frustum
{
    glm::vec4 planes[6];
    glm::vec4 corners[8];
    glm::uint num = 0;
};

struct alignas(16) DrawMesh
{
    glm::vec4 bbMin = glm::vec4(0.f);
    glm::vec4 bbMax = glm::vec4(0.f);
    glm::uint countIndex = 0u;
    glm::uint firstIndex = 0u;
    glm::int32 firstVertex = 0u;
    glm::uint countVertex = 0u;
};

struct alignas(16) DrawSkin
{
    // x = diffuse index, y = roughness index, z = normal index, w = occlusion index.
    glm::uvec4 textures = glm::uvec4(0.f);
};

struct alignas(16) DrawInstance
{
    glm::mat4 model = glm::mat4(1.f);
    glm::u32 meshIndex = 0u;
    glm::u32 skinIndex = 0u;
};

struct DrawCommand
{
    glm::uint drawId = 0u;
    glm::uint countIndex = 0u;
    glm::uint instanceCount = 0u;
    glm::uint firstIndex = 0u;
    glm::int32 baseVertex = 0u;
    glm::uint baseInstance = 0u;
    glm::uint instId = 0u;
};
} // namespace ler::render
