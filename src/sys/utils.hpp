//
// Created by loulfy on 01/12/2023.
//

#pragma once

#include "log/log.hpp"

#include <bitset>
#include <filesystem>
#include <fstream>
#include <map>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <variant>
#include <vector>
namespace fs = std::filesystem;

#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace ler
{
template <typename T, typename U> T checked_cast(U u)
{
    static_assert(!std::is_same<T, U>::value, "Redundant checked_cast");
#ifdef _DEBUG
    if (!u)
        return nullptr;
    T t = dynamic_cast<T>(u);
    if (!t)
        assert(!"Invalid type cast"); // NOLINT(clang-diagnostic-string-conversion)
    return t;
#else
    return static_cast<T>(u);
#endif
}
} // namespace ler

namespace ler::sys
{
std::string getHomeDir();
std::string getCpuName();
uint64_t getRamCapacity();

static const fs::path ASSETS_DIR = fs::path(PROJECT_DIR) / "assets";
static const fs::path CACHED_DIR = fs::path("cached");
static const fs::path PACKED_DIR = fs::path(".ler");

static constexpr uint32_t C04Mio = 4 * 1024 * 1024;
static constexpr uint32_t C08Mio = 8 * 1024 * 1024;
static constexpr uint32_t C16Mio = 16 * 1024 * 1024;
static constexpr uint32_t C32Mio = 32 * 1024 * 1024;
static constexpr uint32_t C64Mio = 64 * 1024 * 1024;
static constexpr uint32_t C128Mio = 128 * 1024 * 1024;

uint32_t giveBestSize(uint32_t requiredSize);

std::string toUtf8(const std::wstring& wide);
std::wstring toUtf16(const std::string& str);
std::vector<char> readBlobFile(const fs::path& path);

class Bitset
{
public:
    void set(const uint32_t index)
    {
        m_value |= 1 << index;
    }

    void clear(const uint32_t index)
    {
        m_value &= ~(1 << index);
    }
    [[nodiscard]] int findFirst() const
    {
        return std::countr_one(m_value);
    }
    [[nodiscard]] uint32_t size() const
    {
        return std::bit_width(m_value);
    }

private:
    uint32_t m_value = 0xffff;
};

} // namespace ler::sys
