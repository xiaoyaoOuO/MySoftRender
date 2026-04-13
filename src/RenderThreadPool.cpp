#include "RenderThreadPool.h"

#include <algorithm>
#include <exception>

namespace {
/**
 * @brief parallelFor 的共享同步状态，负责统计剩余任务并传递异常。
 */
struct ParallelForSyncState
{
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t remainingJobs = 0;
    std::exception_ptr firstException = nullptr;
};
}

RenderThreadPool::RenderThreadPool(std::size_t threadCount)
{
    StartWorkers(NormalizeThreadCount(threadCount));
}

RenderThreadPool::~RenderThreadPool()
{
    waitIdle();
    StopWorkers();
}

void RenderThreadPool::setThreadCount(std::size_t threadCount)
{
    const std::size_t targetThreadCount = NormalizeThreadCount(threadCount);
    if (targetThreadCount == this->threadCount()) {
        return;
    }

    // 调整线程数量前先等待队列排空，避免任务在重建线程过程中丢失。
    waitIdle();
    StopWorkers();
    StartWorkers(targetThreadCount);
}

std::size_t RenderThreadPool::threadCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

std::size_t RenderThreadPool::pendingTaskCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

bool RenderThreadPool::idle() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty() && activeWorkers_ == 0;
}

void RenderThreadPool::waitIdle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idleCv_.wait(lock, [this]() {
        return tasks_.empty() && activeWorkers_ == 0;
    });
}

void RenderThreadPool::parallelFor(
    std::size_t begin,
    std::size_t end,
    std::size_t minChunkSize,
    const std::function<void(std::size_t, std::size_t)>& rangeTask)
{
    if (begin >= end || !rangeTask) {
        return;
    }

    const std::size_t normalizedChunkSize = std::max<std::size_t>(1, minChunkSize);
    const std::size_t totalWork = end - begin;
    const std::size_t jobCount = (totalWork + normalizedChunkSize - 1) / normalizedChunkSize;

    if (jobCount <= 1) {
        rangeTask(begin, end);
        return;
    }

    auto syncState = std::make_shared<ParallelForSyncState>();
    syncState->remainingJobs = jobCount;

    for (std::size_t jobIndex = 0; jobIndex < jobCount; ++jobIndex) {
        const std::size_t rangeBegin = begin + jobIndex * normalizedChunkSize;
        const std::size_t rangeEnd = std::min(end, rangeBegin + normalizedChunkSize);

        enqueue(
            [syncState, rangeTask, rangeBegin, rangeEnd]() {
                try {
                    rangeTask(rangeBegin, rangeEnd);
                } catch (...) {
                    // 仅记录第一个异常，后续异常不覆盖，保证可追踪最早失败点。
                    std::lock_guard<std::mutex> lock(syncState->mutex);
                    if (!syncState->firstException) {
                        syncState->firstException = std::current_exception();
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(syncState->mutex);
                    if (syncState->remainingJobs > 0) {
                        --syncState->remainingJobs;
                    }
                }
                syncState->cv.notify_one();
            });
    }

    {
        std::unique_lock<std::mutex> lock(syncState->mutex);
        syncState->cv.wait(lock, [syncState]() {
            return syncState->remainingJobs == 0;
        });
    }

    if (syncState->firstException) {
        std::rethrow_exception(syncState->firstException);
    }
}

std::size_t RenderThreadPool::NormalizeThreadCount(std::size_t requested)
{
    if (requested > 0) {
        return requested;
    }

    const std::size_t hardwareCount = static_cast<std::size_t>(std::thread::hardware_concurrency());
    return std::max<std::size_t>(hardwareCount, 1);
}

void RenderThreadPool::StartWorkers(std::size_t threadCount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = false;
    workers_.reserve(threadCount);

    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back(&RenderThreadPool::WorkerLoop, this);
    }
}

void RenderThreadPool::StopWorkers()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    taskCv_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.clear();
        tasks_.clear();
        activeWorkers_ = 0;
        stopping_ = false;
    }
}

void RenderThreadPool::WorkerLoop()
{
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            taskCv_.wait(lock, [this]() {
                return stopping_ || !tasks_.empty();
            });

            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop_front();
            ++activeWorkers_;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeWorkers_ > 0) {
                --activeWorkers_;
            }

            if (tasks_.empty() && activeWorkers_ == 0) {
                idleCv_.notify_all();
            }
        }
    }
}
