//
// Created by loulfy on 16/02/2024.
//

#include "queue.hpp"

namespace ler::rhi
{
    Queue::Queue(QueueType queueID) : m_queueType(queueID)
    {

    }

    void Queue::retireCommandBuffers()
    {
        std::lock_guard lock(m_mutexPool);
        std::vector<CommandPtr> submissions = std::move(m_commandBuffersInFlight);

        uint64_t lastFinishedID = updateLastFinishedID();

        for (const CommandPtr& cmd : submissions)
        {
            if (cmd->submissionID <= lastFinishedID)
            {
                cmd->submissionID = 0;
                cmd->referencedResources.clear();
                m_commandBuffersPool.push_back(cmd);
            }
            else
            {
                m_commandBuffersInFlight.push_back(cmd);
            }
        }
    }

    rhi::CommandPtr Queue::getOrCreateCommandBuffer()
    {
        std::lock_guard lock(m_mutexPool);
        rhi::CommandPtr command;
        if (m_commandBuffersPool.empty())
        {
            command = createCommandBuffer();
        }
        else
        {
            command = m_commandBuffersPool.back();
            m_commandBuffersPool.pop_back();
        }
        return command;
    }

    bool Queue::pollCommandList(uint64_t submissionID)
    {
        if (submissionID > m_lastSubmittedID || submissionID == 0)
            return false;

        bool completed = m_lastFinishedID >= submissionID;
        if (completed)
            return true;

        completed = updateLastFinishedID() >= submissionID;
        return completed;
    }
}