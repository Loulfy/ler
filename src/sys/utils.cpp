//
// Created by loulfy on 04/12/2023.
//

#include "utils.hpp"

#ifdef _WIN32
#include <ShlObj.h>
#include <Wbemidl.h>
#include <Windows.h>
#include <comdef.h>
#else
#include <pwd.h>
#include <iconv.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace ler::sys
{
std::string getHomeDir()
{
#ifdef _WIN32
    wchar_t wide_path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, wide_path)))
    {
        char path[MAX_PATH];
        if (WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, path, MAX_PATH, nullptr, nullptr) > 0)
        {
            return path;
        }
    }
#else
    struct passwd* pw = getpwuid(getuid());
    if (pw != nullptr)
        return pw->pw_dir;
#endif

    return {};
}

unsigned int getRamCapacity()
{
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
    {
        DWORDLONG ramCapacity = status.ullTotalPhys;
        return ramCapacity / (1024ull * 1024ull);
    }
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0)
    {
        long ramCapacity = info.totalram * info.mem_unit;
        return ramCapacity / (1024 * 1024);
    }
#endif
    return 0;
}

std::string getCpuName()
{
#ifdef _WIN32
    HRESULT hres;

    // Step 1: Initialize COM
    hres = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hres))
    {
        log::error("Failed to initialize COM library. Error code: {}", hres);
        return {};
    }

    // Step 2: Initialize security
    hres = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
                                nullptr, EOAC_NONE, nullptr);
    if (FAILED(hres))
    {
        log::error("Failed to initialize security. Error code: {}", hres);
        CoUninitialize();
        return {};
    }

    // Step 3: Obtain the initial locator to WMI
    IWbemLocator* pLoc = nullptr;
    hres = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                            reinterpret_cast<LPVOID*>(&pLoc));
    if (FAILED(hres))
    {
        log::error("Failed to create IWbemLocator object. Error code: {}", hres);
        CoUninitialize();
        return {};
    }

    // Step 4: Connect to WMI through the IWbemLocator interface
    IWbemServices* pSvc = nullptr;
    hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
    if (FAILED(hres))
    {
        log::error("Could not connect to WMI. Error code: ", hres);
        pLoc->Release();
        CoUninitialize();
        return {};
    }

    // Step 5: Set security levels on the proxy
    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                             RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hres))
    {
        log::error("Could not set proxy blanket. Error code: {}", hres);
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return {};
    }

    // Step 6: Query for the CPU name
    IEnumWbemClassObject* pEnumerator = nullptr;
    hres = pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT Name FROM Win32_Processor"),
                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnumerator);
    if (FAILED(hres))
    {
        log::error("Failed to execute WQL query. Error code: {}", hres);
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return {};
    }

    char cpuName[MAX_PATH];

    // Step 7: Retrieve CPU information from the query result
    IWbemClassObject* pclObj = nullptr;
    ULONG uReturn = 0;
    while (pEnumerator)
    {
        hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclObj, &uReturn);
        if (uReturn == 0)
            break;
        VARIANT vtProp;
        hres = pclObj->Get(L"Name", 0, &vtProp, nullptr, nullptr);
        if (SUCCEEDED(hres))
        {
            WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, cpuName, MAX_PATH, nullptr, nullptr);
            VariantClear(&vtProp);
        }
        pclObj->Release();
    }

    // Cleanup
    pSvc->Release();
    pLoc->Release();
    pEnumerator->Release();
    CoUninitialize();

    std::string trimmed(cpuName);
    trimmed.erase(trimmed.find_last_not_of(' ') + 1);
    return trimmed;
#else
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;

    if (cpuinfo.is_open())
    {
        while (std::getline(cpuinfo, line))
        {
            if (line.find("model name") != std::string::npos)
            {
                return line.substr(line.find(":") + 2);
            }
        }
    }
#endif
    return {};
}

uint32_t giveBestSize(uint32_t requiredSize)
{
    if (requiredSize >= C04Mio)
        return C04Mio;
    else if (requiredSize >= C08Mio)
        return C08Mio;
    else if (requiredSize >= C16Mio)
        return C16Mio;
    else if (requiredSize >= C32Mio)
        return C32Mio;
    else
        return C64Mio;
}

std::string toUtf8(const std::wstring& wide)
{
    std::string str(wide.size(), ' ');
#ifdef _WIN32
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, str.data(), MAX_PATH, nullptr, nullptr);
#else
    size_t size = wide.size();
    char* outbuf = str.data();
    auto inbuf = (char*)wide.data();
    iconv_t cd = iconv_open("utf-8", "utf-16");
    iconv(cd, &inbuf, &size, &outbuf, &size);
    iconv_close(cd);
#endif
    return str;
}

std::wstring toUtf16(const std::string& str)
{
    std::wstring wide(str.size(), ' ');
#ifdef _WIN32
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wide.data(), wide.size());
#else
    size_t len = mbstowcs(wide.data(), str.c_str(), str.size());
#endif
    return wide;
}

std::vector<char> readBlobFile(const fs::path& path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);

    // Obtain the size of the file.
    std::error_code ec;
    const auto sz = static_cast<std::streamsize>(fs::file_size(path, ec));
    if (ec.value())
        throw std::runtime_error("File Not Found: " + ec.message());

    // Create a buffer.
    std::vector<char> result(sz);
    f.read(result.data(), sz);
    return result;
}
} // namespace ler::sys