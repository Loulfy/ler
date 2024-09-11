//
// Created by loulfy on 16/02/2024.
//

#pragma once

#include "rhi.hpp"

namespace ler::rhi
{
    class Queue
    {
    public:
        virtual ~Queue() = default;
        explicit Queue(QueueType queueID);

        // creates a command buffer and its synchronization resources
        rhi::CommandPtr getOrCreateCommandBuffer();

        virtual uint64_t updateLastFinishedID() = 0;
        bool pollCommandList(uint64_t submissionID);

        [[nodiscard]] QueueType getType() const { return m_queueType; }
        [[nodiscard]] uint64_t getLastSubmittedID() const { return m_lastSubmittedID; }

    protected:

        virtual rhi::CommandPtr createCommandBuffer() = 0;
        void retireCommandBuffers();

        std::mutex m_mutex;
        QueueType m_queueType = QueueType::Graphics;

        uint64_t m_lastSubmittedID = 0;
        uint64_t m_lastFinishedID = 0;

        std::vector<CommandPtr> m_commandBuffersInFlight;
        std::vector<CommandPtr> m_commandBuffersPool;
    };
}