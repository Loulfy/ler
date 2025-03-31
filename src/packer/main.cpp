//
// Created by Loulfy on 09/03/2025.
//

#include "importer.hpp"
#include "sys/utils.hpp"

#include <argparse/argparse.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <assimp/cimport.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace spdlog;
using namespace ler;

struct AssimpProgress : public Assimp::ProgressHandler
{
    bool Update(float percentage) override
    {
        float m = std::fmod(percentage, 0.25f);
        if (m == 0)
            log::info("Reading Scene '{}' ... {:3} %", sceneName.string(), percentage * 100);
        return true;
    }

    fs::path sceneName;
};

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("LerPak");
    set_default_logger(logger);

    argparse::ArgumentParser program("lerPak");
    program.add_argument("pack")
        .nargs(argparse::nargs_pattern::at_least_one)
        .metavar("INPUT")
        .help("scene files to import");
    program.add_argument("-o", "--output")
        .nargs(1)
        .metavar("FILE")
        .default_value(std::string{ "out.pak" })
        .help("output pak file");

    program.add_argument("--cook").default_value(true).implicit_value(false).help("compress textures");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err)
    {
        error(err.what());
        std::cerr << program;
        return EXIT_FAILURE;
    }

    info("LER Packer Init");
    info("CPU: {}, {} Threads", sys::getCpuName(), std::thread::hardware_concurrency());
    info("RAM: {} Go", std::ceil(static_cast<float>(sys::getRamCapacity()) / 1024.f));

    auto list = program.get<std::list<std::string>>("pack");
    bool cook = program.get<bool>("--cook");

    auto outPath = program.get<std::string>("-o");

    info("[{}]", fmt::join(list, "; "));

    std::vector<fs::path> paths;
    for (const std::string& p : list)
    {
        std::error_code ec;
        fs::file_status s = fs::status(p, ec);
        if (ec.value())
        {
            log::error("File Not Found: {}", p);
            return EXIT_FAILURE;
        }
        if (fs::is_regular_file(s))
        {
            fs::path path(p);
            path.make_preferred();

            std::string ext = path.extension().string();
            std::ranges::transform(ext, ext.begin(), ::tolower);

            if (ext == ".json")
            {
                std::ifstream bundle(path);
                std::stringstream buffer;
                buffer << bundle.rdbuf();
                json j = json::parse(buffer.str());
                if (j.contains("scenes") && j["scenes"].is_array())
                {
                    for (const std::string& scenePath : j["scenes"].get<std::vector<std::string>>())
                        paths.emplace_back(scenePath);
                }
                if(!program.is_used("-o") && j.contains("output") && j["output"].is_string())
                    outPath = j["output"];
            }
            else if (aiIsExtensionSupported(ext.c_str()))
            {
                paths.push_back(path);
            }
            else
            {
                log::error("Extension not supported: {}", p);
                return EXIT_FAILURE;
            }
        }
    }

    for(const fs::path& path : paths)
        log::info("Enqueue scene: {}", path.string());

    pak::PakPacker packer(outPath);

    auto* progress = new AssimpProgress;
    Assimp::Importer importer;
    importer.SetProgressHandler(progress);
    unsigned int postProcess = aiProcessPreset_TargetRealtime_Fast;
    postProcess |= aiProcess_FlipUVs;
    postProcess |= aiProcess_FlipWindingOrder;
    postProcess |= aiProcess_GenBoundingBoxes;

    for (const fs::path& scenePath : paths)
    {
        progress->sceneName = scenePath.filename();
        const aiScene* aiScene = importer.ReadFile(scenePath.string(), postProcess);

        if (aiScene == nullptr || aiScene->mNumMeshes == 0)
        {
            log::error(importer.GetErrorString());
            return EXIT_FAILURE;
        }

        packer.setParentDir(scenePath.parent_path());
        packer.processSceneNode(aiScene->mRootNode, aiScene->mMeshes);
        packer.processMaterial(aiScene);
        packer.processTextures(aiScene, cook);
        packer.processMeshes(aiScene);
        importer.FreeScene();
    }

    packer.finish();

    return EXIT_SUCCESS;
}