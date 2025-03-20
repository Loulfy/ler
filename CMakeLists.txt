cmake_minimum_required(VERSION 3.25)
project(gdev)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan)
find_package(unofficial-imgui-node-editor CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(gli CONFIG REQUIRED)
find_package(Stb REQUIRED)

find_package(PkgConfig REQUIRED)
set(ENV{PKG_CONFIG_PATH} ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig)
pkg_check_modules(Coro REQUIRED IMPORTED_TARGET libcoro)

add_definitions(-DPROJECT_DIR=\"${PROJECT_SOURCE_DIR}\")

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
# Clang option
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fms-extensions>)
endif ()

set(SRC
    "src/log/log.hpp"
    "src/rhi/ref.hpp"
    "src/rhi/rhi.hpp"
    "src/rhi/format.cpp"
    "src/sys/platform.hpp"
    "src/sys/utils.hpp"
    "src/sys/utils.cpp"
    "src/sys/ioring.hpp"
    "src/sys/ioring.cpp"
    "src/sys/thread.hpp"
    "src/sys/mpsc.hpp"
    "src/sys/mpsc.inl"
    "src/sys/task.hpp"
    "src/sys/file.hpp"
    "src/sys/file.cpp"
    "src/sys/mem.hpp"
    "src/sys/mem.cpp"
    "src/img/tex.hpp"
    "src/img/ktx.hpp"
    "src/img/ktx.cpp"
    "src/img/dds.hpp"
    "src/img/dds.cpp"
    "src/img/loader.hpp"
    "src/img/loader.cpp"
    "src/img/gli_format.cpp"
    "src/rhi/vulkan.hpp"
    "src/rhi/queue.hpp"
    "src/rhi/queue.cpp"
    "src/rhi/bindless.hpp"
    "src/rhi/bindless.cpp"
    "src/rhi/storage.hpp"
    "src/rhi/storage.cpp"
    "src/app/desktop.hpp"
    "src/app/desktop.cpp"
    "src/rhi/common.cpp"
    "src/rhi/shader/compiler.cpp"
    "src/rhi/vulkan/vulkan_device.cpp"
    "src/rhi/vulkan/vulkan_format.cpp"
    "src/rhi/vulkan/vulkan_texture.cpp"
    "src/rhi/vulkan/vulkan_descriptor.cpp"
    "src/rhi/vulkan/vulkan_pipeline.cpp"
    "src/rhi/vulkan/vulkan_swapchain.cpp"
    "src/rhi/vulkan/vulkan_command.cpp"
    "src/rhi/vulkan/vulkan_storage.cpp"
    "src/rhi/vulkan/vulkan_library.cpp"
    "src/rhi/vulkan/vulkan_imgui.cpp"
    "src/render/resource_mgr.hpp"
    "src/render/resource_mgr.cpp"
    "src/render/mesh_list.hpp"
    "src/render/mesh_list.cpp"
    "src/camera/camera.hpp"
    "src/camera/camera.cpp"
    "src/render/pass.hpp"
    "src/render/draw.hpp"
    "src/render/mesh.hpp"
    "src/render/mesh.cpp"
    "src/render/graph.hpp"
    "src/render/graph.cpp"
    #"src/scene/scene.hpp"
    #"src/scene/scene.cpp"
    "src/pass/forward_indexed.hpp"
    "src/pass/culling_command.hpp"
    "src/pass/deferred_scene.hpp"
)

set(SRC_PACKER
    "src/scene/scene.hpp"
    "src/scene/scene.cpp"
    "src/sys/utils.cpp"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND SRC
        "src/rhi/d3d12.hpp"
        "src/rhi/d2d12/d3d12_device.cpp"
        "src/rhi/d2d12/d3d12_format.cpp"
        "src/rhi/d2d12/d3d12_texture.cpp"
        "src/rhi/d2d12/d3d12_descriptor.cpp"
        "src/rhi/d2d12/d3d12_pipeline.cpp"
        "src/rhi/d2d12/d3d12_swapchain.cpp"
        "src/rhi/d2d12/d3d12_command.cpp"
        "src/rhi/d2d12/d3d12_storage.cpp"
        "src/rhi/d2d12/d3d12_library.cpp"
        "src/rhi/d2d12/d3d12_imgui.cpp"
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    list(APPEND SRC
        "src/rhi/metal.hpp"
        "src/rhi/metal/metal_encoder.h"
        "src/rhi/metal/metal_device.cpp"
        "src/rhi/metal/metal_format.cpp"
        "src/rhi/metal/metal_runtime.cpp"
        "src/rhi/metal/metal_texture.cpp"
        "src/rhi/metal/metal_swapchain.cpp"
        "src/rhi/metal/metal_command.cpp"
        "src/rhi/metal/metal_storage.cpp"
        "src/rhi/metal/metal_glfw.mm"
        "src/rhi/metal/metal_pipeline.cpp"
        "src/rhi/metal/metal_imgui.cpp"
        "src/rhi/metal/metal_imgui.mm"
    )
endif()

include_directories(src)

set($ENV{MTL_DEBUG_LAYER} "1")
set($ENV{MTL_SHADER_VALIDATION_REPORT_TO_STDERR} 1)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(d3d12-memory-allocator CONFIG REQUIRED)
    find_package(dstorage CONFIG REQUIRED)

    find_path(D3DX12_INCLUDE_DIRS "d3dx12.h")

    set (WIN32_RESOURCES "res/ler.rc")
    FIND_LIBRARY(PIX_LIBRARIES NAMES WinPixEventRuntime PATHS "libs/winpixeventruntime/bin/x64" DOC "WinPix library")
    FIND_PATH(PIX_INCLUDE_DIRS pix3.h "libs/winpixeventruntime/Include/WinPixEventRuntime" DOC "WinPix includes")
endif()

flatbuffers_generate_headers(TARGET generated_cppmessages SCHEMAS "res/scene.fbs" FLAGS ${FLATC_CPP_FLAGS})
flatbuffers_generate_headers(TARGET generated_archive SCHEMAS "res/archive.fbs" FLAGS "--reflect-names")

add_executable(gdev main.cpp ${SRC} ${WIN32_RESOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
add_dependencies(gdev GENERATE_generated_cppmessages)
add_dependencies(gdev GENERATE_generated_archive)
target_link_libraries(gdev PRIVATE PkgConfig::Coro)
target_link_libraries(gdev PRIVATE spdlog::spdlog)
target_link_libraries(gdev PRIVATE assimp::assimp)
target_link_libraries(gdev PRIVATE unofficial::spirv-reflect::spirv-reflect)
target_link_libraries(gdev PRIVATE glslang::OSDependent glslang::glslang glslang::MachineIndependent glslang::GenericCodeGen glslang::glslang-default-resource-limits glslang::OGLCompiler glslang::SPVRemapper glslang::SPIRV glslang::HLSL)
target_link_libraries(gdev PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
target_link_libraries(gdev PRIVATE Microsoft::DirectXShaderCompiler)
target_include_directories(gdev PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(gdev PRIVATE generated_cppmessages)
target_link_libraries(gdev PRIVATE generated_archive)
target_link_libraries(gdev PRIVATE flatbuffers::flatbuffers)
target_link_libraries(gdev PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(gdev PRIVATE meshoptimizer::meshoptimizer)
target_link_libraries(gdev PRIVATE unofficial::imgui-node-editor::imgui-node-editor)
target_link_libraries(gdev PRIVATE imgui::imgui)
target_link_libraries(gdev PRIVATE xxHash::xxhash)
target_link_libraries(gdev PRIVATE glfw)
target_link_libraries(gdev PRIVATE gli)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(gdev PRIVATE Microsoft::DirectStorage)
    target_link_libraries(gdev PRIVATE unofficial::d3d12-memory-allocator)
    target_link_libraries(gdev PRIVATE d3d12 dxgi dxguid wbemuuid onecoreuap)
    target_include_directories(gdev PRIVATE ${D3DX12_INCLUDE_DIRS})
    target_include_directories(gdev PRIVATE ${PIX_INCLUDE_DIRS})
    target_link_libraries(gdev PRIVATE ${PIX_LIBRARIES})
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(Uring REQUIRED IMPORTED_TARGET liburing)
    target_link_libraries(gdev PRIVATE PkgConfig::Uring)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Link IRConverter
    find_library(IRConverter_Path NAMES "metalirconverter")
    add_library(IRConverter STATIC IMPORTED)
    set_target_properties(IRConverter PROPERTIES IMPORTED_LOCATION ${IRConverter_Path})

    #Link DXC
    find_library(DXC_Path NAMES "dxcompiler")
    add_library(DXC STATIC IMPORTED)
    set_target_properties(DXC PROPERTIES IMPORTED_LOCATION ${DXC_Path})

    # Metal cpp library (linker)
    target_link_libraries(gdev PRIVATE
            "-framework Metal"
            "-framework Foundation"
            "-framework QuartzCore"
            IRConverter
            DXC
    )
    # Metal cpp headers
    target_include_directories(gdev PRIVATE "libs/metal-cpp")
endif()

set(OPTION_BUILD_CMP_SDK ON)
set(OPTION_ENABLE_ALL_APPS OFF)
set(OPTION_BUILD_BROTLIG OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
#set(OPTION_CMP_DIRECTX ON)
add_subdirectory(libs/compressonator)

add_executable(packer pack.cpp ${SRC_PACKER} ${PROTO_SRCS} ${PROTO_HDRS})
add_dependencies(packer GENERATE_generated_cppmessages)
target_link_libraries(packer PRIVATE spdlog::spdlog)
target_link_libraries(packer PRIVATE assimp::assimp)
target_link_libraries(packer PRIVATE xxHash::xxhash)
target_link_libraries(packer PRIVATE basisu::basisu_encoder)
target_link_libraries(packer PRIVATE generated_cppmessages)
target_link_libraries(packer PRIVATE flatbuffers::flatbuffers)
target_link_libraries(packer PRIVATE meshoptimizer::meshoptimizer)
target_link_libraries(packer PRIVATE CMP_Framework)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(packer PRIVATE dxguid wbemuuid)
endif()

set(SRC_LER_PAK
    "src/packer/main.cpp"
    "src/packer/importer.hpp"
    "src/packer/material.cpp"
    "src/packer/texture.cpp"
    "src/packer/archive.cpp"
    "src/packer/mesh.cpp"
    "src/sys/utils.hpp"
    "src/sys/utils.cpp"
)

add_executable(lerPak ${SRC_LER_PAK})
target_link_libraries(lerPak PRIVATE spdlog::spdlog)
target_link_libraries(lerPak PRIVATE assimp::assimp)
target_link_libraries(lerPak PRIVATE xxHash::xxhash)
target_link_libraries(lerPak PRIVATE CMP_Core CMP_Framework CMP_Compressonator)
target_link_libraries(lerPak PRIVATE generated_archive)
target_link_libraries(lerPak PRIVATE flatbuffers::flatbuffers)
target_link_libraries(lerPak PRIVATE meshoptimizer::meshoptimizer)
target_include_directories(lerPak PRIVATE ${Stb_INCLUDE_DIR})
add_dependencies(lerPak GENERATE_generated_archive)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(lerPak PRIVATE dxguid wbemuuid)
endif()
