//
// Created by loulfy on 24/07/2024.
//

#include "scene/scene.hpp"

using namespace ler;

int main()
{
    scene::SceneImporter::prepare(R"(C:\Users\loria\Documents\minimalRT\assets\sponza\Sponza.gltf)", "scene");
    return EXIT_SUCCESS;
}