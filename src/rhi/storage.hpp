//
// Created by loulfy on 18/01/2024.
//

#pragma once

#include "img/dds.hpp"
#include "img/ktx.hpp"
#include "rhi.hpp"
#include "sys/ioring.hpp"
#include "sys/mpsc.hpp"

#include <semaphore>
#include <memory_resource>

namespace ler::rhi
{
class CommonStorage : public IStorage
{
  public:
    CommonStorage(IDevice* device, std::shared_ptr<coro::thread_pool>& tp);
    void update() override;
    std::vector<ReadOnlyFilePtr> openFiles(const fs::path& path, const fs::path& ext) override;

    void requestLoadTexture(coro::latch& latch, BindlessTablePtr& table, const std::span<ReadOnlyFilePtr>& files) override;
    void requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint64_t fileLength, uint64_t fileOffset) override;
    void requestOpenTexture(coro::latch& latch, BindlessTablePtr& table, const std::span<fs::path>& paths) override;

    img::ITexture* factoryTexture(const ReadOnlyFilePtr& file, std::byte* metadata);
    const BufferPtr& getStaging(int index) const { return m_stagings[index]; }

    coro::task<int> acquireStaging();
    void releaseStaging(uint32_t index);

    static constexpr int kStagingCount = 8;
    static constexpr uint32_t kStagingSize = sys::C64Mio;

    class ReSchedule : public std::suspend_always
    {
        friend class CommonStorage;
        CommonStorage& m_storage;

    public:

        explicit ReSchedule(CommonStorage& storage);
        void await_suspend(std::coroutine_handle<> handle) const noexcept;
    };

  protected:
    IDevice* m_device = nullptr;
    std::vector<BufferPtr> m_stagings;
    sys::MpscQueue<int> m_dispatcher;

  private:
    virtual coro::task<> makeSingleTextureTask(coro::latch& latch, BindlessTablePtr table, ReadOnlyFilePtr file) = 0;
    virtual coro::task<> makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table, std::vector<ReadOnlyFilePtr> files) = 0;
    virtual coro::task<> makeBufferTask(coro::latch& latch, ReadOnlyFilePtr file, BufferPtr buffer, uint64_t fileLength, uint64_t fileOffset) = 0;

    coro::task<> makeOpenTextureTask(coro::latch& latch, BindlessTablePtr& table, std::vector<fs::path> paths);

    using task_container = coro::thread_pool&;
    task_container m_scheduler;

    sys::Bitset m_bitset;
    mutable std::mutex m_mutex;
    std::counting_semaphore<> m_semaphore;

    std::unique_ptr<std::byte[]> m_buffer = std::make_unique<std::byte[]>(sys::C04Mio);
    std::pmr::monotonic_buffer_resource m_memory;
};
} // namespace ler::rhi
