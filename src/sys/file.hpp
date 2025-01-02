//
// Created by loulfy on 24/01/2024.
//

#pragma once

#include <vector>
#include <filesystem>
namespace fs = std::filesystem;

#ifdef _WIN32
using FD = void*;
#else
using FD = int;
#endif

namespace ler::sys
{
class ReadOnlyFile
{
  public:
    explicit ReadOnlyFile(const fs::path& path);
    ReadOnlyFile(ReadOnlyFile&& other) noexcept;
    ~ReadOnlyFile();

    [[nodiscard]] FD getNativeHandle() const;
    [[nodiscard]] std::string getPath() const;

    static std::vector<ReadOnlyFile*> openFiles(const fs::path& path, const fs::path& ext);

    uint64_t m_size = 0;

  private:
    fs::path m_path;
    FD m_hFile = 0;
};
using ReadOnlyFilePtr = std::shared_ptr<ReadOnlyFile>;
} // namespace ler::sys