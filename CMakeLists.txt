cmake_minimum_required(VERSION 3.25)
project(gdev)

set(CMAKE_CXX_STANDARD 23)

find_package(Vulkan)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
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

set(SRC
    "src/log/log.hpp"
    "src/rhi/rhi.hpp"
    "src/rhi/format.cpp"
    "src/sys/utils.hpp"
    "src/sys/utils.cpp"
    "src/sys/ioring.hpp"
    "src/sys/ioring.cpp"
    "src/sys/thread.hpp"
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
    "src/rhi/storage.hpp"
    "src/rhi/storage.cpp"
    "src/app/desktop.hpp"
    "src/app/desktop.cpp"
    "src/rhi/shader/compiler.cpp"
    "src/rhi/vulkan/vulkan_device.cpp"
    "src/rhi/vulkan/vulkan_format.cpp"
    "src/rhi/vulkan/vulkan_texture.cpp"
    "src/rhi/vulkan/vulkan_pipeline.cpp"
    "src/rhi/vulkan/vulkan_swapchain.cpp"
    "src/rhi/vulkan/vulkan_command.cpp"
    "src/rhi/vulkan/vulkan_storage.cpp"
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
    "src/scene/scene.hpp"
    "src/scene/scene.cpp"
    "src/pass/forward_indexed.hpp"
    "src/pass/culling_command.hpp"
)

set(SRC_PACKER
    "src/scene/scene.hpp"
    "src/scene/scene.cpp"
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
    )
endif()

include_directories(src)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(d3d12-memory-allocator CONFIG REQUIRED)
    find_package(dstorage CONFIG REQUIRED)

    find_path(D3DX12_INCLUDE_DIRS "d3dx12.h")

    set (WIN32_RESOURCES "res/ler.rc")
    FIND_LIBRARY(PIX_LIBRARIES NAMES WinPixEventRuntime PATHS "libs/winpixeventruntime/bin/x64" DOC "WinPix library")
    FIND_PATH(PIX_INCLUDE_DIRS pix3.h "libs/winpixeventruntime/Include/WinPixEventRuntime" DOC "WinPix includes")
endif()

flatbuffers_generate_headers(TARGET generated_cppmessages SCHEMAS "res/scene.fbs" FLAGS ${FLATC_CPP_FLAGS})

add_executable(gdev main.cpp ${SRC} ${WIN32_RESOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(gdev PRIVATE PkgConfig::Coro)
target_link_libraries(gdev PRIVATE spdlog::spdlog)
target_link_libraries(gdev PRIVATE assimp::assimp)
target_link_libraries(gdev PRIVATE unofficial::spirv-reflect::spirv-reflect)
target_link_libraries(gdev PRIVATE glslang::OSDependent glslang::glslang glslang::MachineIndependent glslang::GenericCodeGen glslang::glslang-default-resource-limits glslang::OGLCompiler glslang::SPVRemapper glslang::SPIRV glslang::HLSL)
target_link_libraries(gdev PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
target_link_libraries(gdev PRIVATE Microsoft::DirectXShaderCompiler)
target_include_directories(gdev PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(gdev INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated_cppmessages)
target_link_libraries(gdev PRIVATE generated_cppmessages)
target_link_libraries(gdev PRIVATE flatbuffers::flatbuffers)
target_link_libraries(gdev PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(gdev PRIVATE meshoptimizer::meshoptimizer)
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
endif()

add_executable(packer pack.cpp ${SRC_PACKER} ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(packer INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/generated_cppmessages)
target_link_libraries(packer PRIVATE spdlog::spdlog)
target_link_libraries(packer PRIVATE assimp::assimp)
target_link_libraries(packer PRIVATE generated_cppmessages)
target_link_libraries(packer PRIVATE flatbuffers::flatbuffers)
target_link_libraries(packer PRIVATE meshoptimizer::meshoptimizer)
