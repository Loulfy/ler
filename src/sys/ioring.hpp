//
// Created by loulfy on 07/01/2024.
//

#pragma once

#include "platform.hpp"

#ifdef PLATFORM_WIN
#define NOMINMAX
#define WIN32_NO_STATUS
#include <ntstatus.h>
#include <windows.h>

#include <intrin.h>
#include <ioringapi.h>
#include <winternl.h>
#elif PLATFORM_LINUX
#include <liburing.h>
#include <sys/resource.h>
#include <sys/mman.h>
using HANDLE = int;
#elif PLATFORM_MACOS
#include <aio.h>
#include <dispatch/dispatch.h>
using HANDLE = int;
#endif

#include <cassert>
#include <thread>
#include <stop_token>
#include <coro/coro.hpp>
#include <coroutine>
#include <queue>

#include "log/log.hpp"
#include "file.hpp"

namespace ler::sys
{
class IoService
{
  public:
    struct FileLoadRequest
    {
        ReadOnlyFile* file = nullptr;
        uint64_t fileLength = 0u;
        uint64_t fileOffset = 0u;
        uint32_t buffOffset = 0u;
        int32_t buffIndex = 0;
    };

    struct BufferInfo
    {
        void* address = nullptr;
        uint32_t length = 0;
    };

    class IoBatchRequest : public std::suspend_always
    {
      public:
        friend class IoService;
        void await_suspend(std::coroutine_handle<> handle) noexcept;

      private:
        IoService* service = nullptr;
        std::coroutine_handle<> continuation;
        std::vector<FileLoadRequest> requests;
    };

    using Awaiter = IoBatchRequest;

    explicit IoService(std::shared_ptr<coro::thread_pool>& tp);

    Awaiter submit(FileLoadRequest& request);
    Awaiter submit(std::vector<FileLoadRequest>& request);

    void registerBuffers(std::vector<BufferInfo>& buffers, bool enabled);
#ifdef PLATFORM_WIN
    [[nodiscard]] std::byte* getMemPtr(int id) const { return static_cast<std::byte*>(m_buffers[id].Address); }
#elif PLATFORM_LINUX
    [[nodiscard]] std::byte* getMemPtr(int id) const { return static_cast<std::byte*>(m_buffers[id].iov_base); }
#elif PLATFORM_MACOS
    [[nodiscard]] std::byte* getMemPtr(int id) const { return static_cast<std::byte*>(m_buffers[id].address); }
#endif

  private:
    void worker(const std::stop_token& stoken);
    void enqueue(IoBatchRequest& batch);

    static constexpr uint32_t kWorkerCount = 1;

    bool m_useFixedBuffer = false;
    std::vector<HANDLE> m_handles;
#ifdef PLATFORM_WIN
    HIORING m_ring = nullptr;
    std::vector<IORING_BUFFER_INFO> m_buffers;
#elif PLATFORM_LINUX
    io_uring m_ring = {};
    std::vector<iovec> m_buffers;
#elif PLATFORM_MACOS
    std::vector<BufferInfo> m_buffers;
#endif
    std::condition_variable_any m_task_available_cv = {};
    std::unique_ptr<std::jthread[]> m_threads = nullptr;
    std::queue<IoBatchRequest> m_tasks = {};
    static std::mutex m_tasks_mutex;
    std::shared_ptr<coro::thread_pool> m_threadPool;
};
} // namespace ler::sys
