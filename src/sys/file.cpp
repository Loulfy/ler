//
// Created by loulfy on 24/01/2024.
//

#include "file.hpp"
#include "log/log.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_NO_STATUS
#include <winternl.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ler::sys
{
ReadOnlyFile::ReadOnlyFile(const fs::path& path) : m_path(path)
{
    fs::path cleanPath = path;
    std::string str = cleanPath.make_preferred().string();
#ifdef _WIN32
    auto pFileName = reinterpret_cast<LPCSTR>(str.c_str());
    m_hFile =
        CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

    if (m_hFile == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        log::error("[IoRing] Failed to open {}", str);
        if (error == ERROR_FILE_NOT_FOUND)
            log::error("ERROR_FILE_NOT_FOUND");
        if (error == ERROR_SHARING_VIOLATION)
            log::error("ERROR_SHARING_VIOLATION");
    }

    LARGE_INTEGER lpFileSize;
    GetFileSizeEx(m_hFile, &lpFileSize);
    m_size = lpFileSize.QuadPart;
#else
    m_hFile = open(str.c_str(), O_RDONLY);
    if(m_hFile == -1)
    {
        log::error("[IoRing] Failed to open {}", str);
        if(errno == ENOENT)
            log::error("ERROR_FILE_NOT_FOUND");
        if(errno == EPERM)
            log::error("ERROR_NO_PERMISSION");
        if(errno == EISDIR)
            log::error("ERROR_IS_DIR");
    }
    struct stat st = {};
    stat(str.c_str(), &st);
    m_size = st.st_size;
#endif
}

#ifdef _WIN32
static constexpr void* const fd_null = nullptr;
#else
static constexpr int fd_null = 0;
#endif

ReadOnlyFile::ReadOnlyFile(ReadOnlyFile&& other) noexcept
    : m_path{ std::exchange(other.m_path, {}) }, m_hFile{ std::exchange(other.m_hFile, fd_null) },
      m_size{ std::exchange(other.m_size, 0) }
{
}

ReadOnlyFile::~ReadOnlyFile()
{
    if (m_hFile)
    {
#ifdef _WIN32
        CloseHandle(m_hFile);
#else
        close(m_hFile);
#endif
    }
}

FD ReadOnlyFile::getNativeHandle() const
{
    return m_hFile;
}

std::string ReadOnlyFile::getPath() const
{
    return m_path.filename().string();
}

std::vector<ReadOnlyFile*> ReadOnlyFile::openFiles(const fs::path& path, const fs::path& ext)
{
    std::vector<ReadOnlyFile*> files;
    for (auto& entry : fs::directory_iterator(path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ext)
            files.emplace_back(new ReadOnlyFile(entry.path()));
    }
    return files;
}
} // namespace ler::sys