//
// Created by loulfy on 07/01/2024.
//

#include "ioring.hpp"

#include <cstring>

namespace ler::sys
{
#ifdef _WIN32
static std::string getErrorMsg(HRESULT hr)
{
    LPSTR messageBuffer = nullptr;
    size_t size =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&messageBuffer, 0, nullptr);
    // Copy the error message into a std::string.
    std::string str(messageBuffer, size);
    LocalFree(messageBuffer);
    return str;
}
#endif

IoService::IoService(std::shared_ptr<coro::thread_pool>& tp) : m_threadPool(tp)
{
    constexpr uint32_t num_threads = kWorkerCount;
    m_threads = std::make_unique<std::jthread[]>(num_threads);
    for (unsigned int i = 0; i < num_threads; ++i)
        m_threads[i] = std::jthread(std::bind_front(&IoService::worker, this));
}

IoService::Awaiter IoService::submit(FileLoadRequest& request)
{
    Awaiter operation;
    operation.service = this;
    operation.requests.emplace_back(request);
    return operation;
}

IoService::Awaiter IoService::submit(std::vector<FileLoadRequest>& request)
{
    Awaiter operation;
    operation.service = this;
    operation.requests = request; //std::move(request);
    return operation;
}

void IoService::enqueue(IoBatchRequest& batch)
{
    {
        const std::scoped_lock tasks_lock(m_tasks_mutex);
        m_tasks.emplace(std::move(batch));
    }
    m_task_available_cv.notify_one();
}

void IoService::IoBatchRequest::await_suspend(std::coroutine_handle<> handle) noexcept
{
    continuation = handle;
    service->enqueue(*this);
}

void IoService::registerBuffers(std::vector<BufferInfo>& buffers, bool enabled)
{
    m_useFixedBuffer = enabled;
    for(const BufferInfo& b : buffers)
        m_buffers.emplace_back(b.address, b.length);
    if(enabled)
    {
#ifdef _WIN32
        const int res = BuildIoRingRegisterBuffers(m_ring, m_buffers.size(), m_buffers.data(), 0);
#else
        const int res = io_uring_register_buffers(&m_ring, m_buffers.data(), m_buffers.size());
#endif
        assert(res == 0);
    }
}

void IoService::worker(const std::stop_token& stoken)
{
#ifdef _WIN32
    IORING_CQE cqe;
    IORING_CAPABILITIES capabilities;
    HRESULT res = QueryIoRingCapabilities(&capabilities);
    assert(res == S_OK);

    IORING_CREATE_FLAGS flags;
    flags.Required = IORING_CREATE_REQUIRED_FLAGS_NONE;
    flags.Advisory = IORING_CREATE_ADVISORY_FLAGS_NONE;
    res = CreateIoRing(capabilities.MaxVersion, flags, 0x10000, 0x20000, &m_ring);
    assert(res == S_OK);

    IoBatchRequest batch;
    uint32_t submittedEntries;

    for (;;)
    {
        {
            std::unique_lock tasks_lock(m_tasks_mutex);
            m_task_available_cv.wait(tasks_lock, stoken, [&] { return !m_tasks.empty(); });

            if (stoken.stop_requested())
            {
                CloseIoRing(m_ring);
                return;
            }

            batch = std::move(m_tasks.front());
            m_tasks.pop();
        }

        m_handles.clear();
        for (const FileLoadRequest& req : batch.requests)
            m_handles.emplace_back(req.file->getNativeHandle());

        res = BuildIoRingRegisterFileHandles(m_ring, m_handles.size(), m_handles.data(), 0);

        for (uint32_t index = 0; index < batch.requests.size(); ++index)
        {
            FileLoadRequest& req = batch.requests[index];

            IORING_HANDLE_REF handleRef(index);
            IORING_BUFFER_REF bufferRef(nullptr);
            if(m_useFixedBuffer)
                bufferRef = IORING_BUFFER_REF(req.buffIndex, req.buffOffset);
            else
            {
                auto* data = static_cast<std::byte*>(m_buffers[req.buffIndex].Address);
                bufferRef = IORING_BUFFER_REF(data + req.buffOffset);
            }

            res = BuildIoRingReadFile(m_ring, handleRef, bufferRef, req.fileLength, req.fileOffset, 0, IOSQE_FLAGS_NONE);
            if (FAILED(res))
            {
                throw std::runtime_error("[IoRing] Failed building IO ring read file structure: " + getErrorMsg(res));
            }
        }

        res = SubmitIoRing(m_ring, m_handles.size(), INFINITE, &submittedEntries);
        assert(res == S_OK);

        while (PopIoRingCompletion(m_ring, &cqe) == S_OK)
        {
            if (FAILED(cqe.ResultCode))
            {
                std::string msg = getErrorMsg(cqe.ResultCode);
                throw std::runtime_error("[IoRing] " + msg);
            }
        }

        m_threadPool->resume(batch.continuation);
    }
#else
    int res = io_uring_queue_init(1024, &m_ring, 0);
    assert(res == 0);

    IoBatchRequest batch;
    uint32_t submittedEntries;

    ::rlimit limit = { };
    getrlimit(RLIMIT_MEMLOCK, &limit);

    constexpr static size_t MaxStreamBufferSize = 64u << 20;
    constexpr static size_t MinStreamBufferSize =  8u << 20;
    size_t streamBufferSize = std::min<rlim_t>(limit.rlim_cur / 4, MaxStreamBufferSize);
    bool m_useFixed = streamBufferSize >= MinStreamBufferSize;

    for (;;)
    {
        {
            std::unique_lock tasks_lock(m_tasks_mutex);
            m_task_available_cv.wait(tasks_lock, stoken, [&] { return !m_tasks.empty(); });

            if (stoken.stop_requested())
            {
                io_uring_queue_exit(&m_ring);
                return;
            }

            batch = std::move(m_tasks.front());
            m_tasks.pop();
        }

        m_handles.clear();
        for (const FileLoadRequest& req : batch.requests)
            m_handles.emplace_back(req.file->getNativeHandle());

        res = io_uring_register_files(&m_ring, m_handles.data(), m_handles.size());
        assert(res == 0);

        for (int index = 0; index < batch.requests.size(); ++index)
        {
            const FileLoadRequest& req = batch.requests[index];

            auto* data = static_cast<std::byte*>(m_buffers[req.buffIndex].iov_base);
            io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
            sqe->flags = IOSQE_FIXED_FILE;

            if(m_useFixedBuffer)
                io_uring_prep_read_fixed(sqe, index, data + req.buffOffset, req.fileLength, req.fileOffset, req.buffIndex);
            else
                io_uring_prep_read(sqe, index, data + req.buffOffset, req.fileLength, req.fileOffset);
        }

        submittedEntries = io_uring_submit_and_wait(&m_ring, m_handles.size());

        io_uring_cqe* cqe;
        for(int i = 0; i < submittedEntries && io_uring_peek_cqe(&m_ring, &cqe) == 0; ++i)
        {
            if(cqe->res < 0)
            {
                std::string msg = strerror(-cqe->res);
                throw std::runtime_error("[IoRing] Read request failed: " + msg);
            }
            io_uring_cqe_seen(&m_ring, cqe);
        }

        res = io_uring_unregister_files(&m_ring);
        assert(res == 0);

        m_threadPool->resume(batch.continuation);
    }
#endif
}
} // namespace ler::sys