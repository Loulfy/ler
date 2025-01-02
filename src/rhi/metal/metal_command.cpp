//
// Created by Loulfy on 22/11/2024.
//

#include "rhi/metal.hpp"

namespace ler::rhi::metal
{
Queue::Queue(const MetalContext& context, QueueType queueID) : rhi::Queue(queueID), m_context(context)
{
    // Describe and create the command queue.
    m_queue = context.device->newCommandQueue();
    m_eventTracking = context.device->newSharedEvent();
}

rhi::CommandPtr Queue::createCommandBuffer()
{
    auto command = std::make_shared<Command>(m_context);
    command->queueType = m_queueType;
    command->cmdBuf = m_queue->commandBuffer();
    return command;
}

uint64_t Queue::updateLastFinishedID()
{
    m_lastFinishedID = m_eventTracking->signaledValue();
    return m_lastFinishedID;
}

uint64_t Queue::submit(const std::span<CommandPtr>& ppCmd)
{
    m_lastSubmittedID++;

    ppCmd.front()->submissionID = m_lastSubmittedID;
    ppCmd.front()->cmdBuf->encodeSignalEvent(m_eventTracking, m_lastSubmittedID);
    ppCmd.front()->cmdBuf->commit();

    return m_lastSubmittedID;
}

void Queue::submitAndWait(const rhi::CommandPtr& command)
{
    auto* native = checked_cast<Command*>(command.get());
    native->cmdBuf->commit();
    native->cmdBuf->waitUntilCompleted();
}

void Command::reset()
{
    cmdBuf = m_context.queue->commandBuffer();
}
} // namespace ler::rhi::metal