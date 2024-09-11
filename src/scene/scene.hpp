//
// Created by loulfy on 22/02/2024.
//

#pragma once

#include "sys/utils.hpp"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace ler::scene
{
class SceneImporter
{
  public:
    static void prepare(const fs::path &path, const std::string& output);
};
} // namespace ler::scene
