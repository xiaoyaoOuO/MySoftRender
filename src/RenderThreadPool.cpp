#include "RenderThreadPool.h"

#include <algorithm>
#include <exception>

namespace {
/**
 * @brief parallelFor 的共享同步状态，负责统计剩余任务并传递异常。
 */
struct ParallelForSyncState
{
    std::mutex mutex; // 保护剩余任务计数与异常指针的互斥锁
    std::condition_variable cv; // 通知主线程任务完成进度
    std::size_t remainingJobs = 0; // 尚未完成的分块任务数量
    std::exception_ptr firstException = nullptr; // 记录首个任务异常
};
}

/**
 * @brief 构造线程池并启动指定数量的工作线程。
 * @param threadCount 线程数量，传入 0 时自动使用硬件并发数。
 */
RenderThreadPool::RenderThreadPool(std::size_t threadCount)
{
    StartWorkers(NormalizeThreadCount(threadCount));
}

/**
 * @brief 析构线程池，等待任务执行完成后停止所有工作线程。
 */
RenderThreadPool::~RenderThreadPool()
{
    waitIdle();
    StopWorkers();
}

/**
 * @brief 动态调整线程池线程数量。
 * @param threadCount 新线程数量，传入 0 时自动使用硬件并发数。
 */
void RenderThreadPool::setThreadCount(std::size_t threadCount)
{
    const std::size_t targetThreadCount = NormalizeThreadCount(threadCount);
    // 线程数量未变化时直接返回，避免无意义的停启线程。
    if (targetThreadCount == this->threadCount()) {
        return;
    }

    // 调整线程数量前先等待队列排空，避免任务在重建线程过程中丢失。
    waitIdle();
    StopWorkers();
    StartWorkers(targetThreadCount);
}

/**
 * @brief 获取线程池当前工作线程数量。
 * @return 返回当前线程数量。
 */
std::size_t RenderThreadPool::threadCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

/**
 * @brief 获取当前待执行任务数量。
 * @return 返回任务队列中未被取走的任务数。
 */
std::size_t RenderThreadPool::pendingTaskCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

/**
 * @brief 获取当前正在执行任务的工作线程数量。
 * @return 返回活跃工作线程数。
 */
std::size_t RenderThreadPool::activeWorkerCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return activeWorkers_;
}

/**
 * @brief 判断线程池是否处于空闲状态。
 * @return 当任务队列为空且无工作线程执行任务时返回 true。
 */
bool RenderThreadPool::idle() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty() && activeWorkers_ == 0;
}

/**
 * @brief 阻塞等待线程池进入空闲状态。
 */
void RenderThreadPool::waitIdle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    // 等待“无排队任务 + 无执行中任务”两个条件同时满足。
    idleCv_.wait(lock, [this]() {
        return tasks_.empty() && activeWorkers_ == 0;
    });
}

/**
 * @brief 按区间分块并行执行任务函数。
 * @param begin 起始索引（包含）。
 * @param end 结束索引（不包含）。
 * @param minChunkSize 每个分块的最小长度。
 * @param rangeTask 分块任务函数，参数为 [rangeBegin, rangeEnd)。
 */
void RenderThreadPool::parallelFor(
    std::size_t begin,
    std::size_t end,
    std::size_t minChunkSize,
    const std::function<void(std::size_t, std::size_t)>& rangeTask)
{
    // 非法区间或空任务直接返回，避免提交无效工作项。
    if (begin >= end || !rangeTask) {
        return;
    }

    const std::size_t normalizedChunkSize = std::max<std::size_t>(1, minChunkSize);
    const std::size_t totalWork = end - begin;
    const std::size_t jobCount = (totalWork + normalizedChunkSize - 1) / normalizedChunkSize;

    // 只有一个分块时走串行直调，减少线程池调度开销。
    if (jobCount <= 1) {
        rangeTask(begin, end);
        return;
    }

    // 创建共享同步状态，用于汇总全部分块任务执行结果。
    auto syncState = std::make_shared<ParallelForSyncState>();
    syncState->remainingJobs = jobCount;

    for (std::size_t jobIndex = 0; jobIndex < jobCount; ++jobIndex) {
        const std::size_t rangeBegin = begin + jobIndex * normalizedChunkSize;
        const std::size_t rangeEnd = std::min(end, rangeBegin + normalizedChunkSize);

        // 将每个分块提交到线程池，由工作线程并行消费。
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
        // 主线程等待直到所有分块任务都完成。
        syncState->cv.wait(lock, [syncState]() {
            return syncState->remainingJobs == 0;
        });
    }

    // 若任一任务抛出异常，则在调用线程重新抛出首个异常。
    if (syncState->firstException) {
        std::rethrow_exception(syncState->firstException);
    }
}

/**
 * @brief 归一化线程数量参数。
 * @param requested 请求的线程数量。
 * @return 返回最终可用线程数量（至少为 1）。
 */
std::size_t RenderThreadPool::NormalizeThreadCount(std::size_t requested)
{
    if (requested > 0) {
        return requested;
    }

    const std::size_t hardwareCount = static_cast<std::size_t>(std::thread::hardware_concurrency());
    return std::max<std::size_t>(hardwareCount, 1);
}

/**
 * @brief 启动指定数量的工作线程。
 * @param threadCount 待启动线程数量。
 */
void RenderThreadPool::StartWorkers(std::size_t threadCount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 启动前先清除停止标记，保证工作线程可以正常取任务。
    stopping_ = false;
    workers_.reserve(threadCount);

    // 创建固定数量的常驻工作线程。
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back(&RenderThreadPool::WorkerLoop, this);
    }
}

/**
 * @brief 停止并回收全部工作线程。
 */
void RenderThreadPool::StopWorkers()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 设置停止标记后唤醒所有工作线程，让其尽快退出循环。
        stopping_ = true;
    }
    taskCv_.notify_all();

    // 等待所有工作线程结束，确保线程资源被完整回收。
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

/**
 * @brief 工作线程主循环：等待任务、执行任务并更新活跃计数。
 */
void RenderThreadPool::WorkerLoop()
{
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 当停止标记置位或队列有任务时被唤醒。
            taskCv_.wait(lock, [this]() {
                return stopping_ || !tasks_.empty();
            });

            // 停止且无待执行任务时退出线程循环。
            if (stopping_ && tasks_.empty()) {
                return;
            }

            // 从队列头部取出任务，并标记当前线程进入执行状态。
            task = std::move(tasks_.front());
            tasks_.pop_front();
            ++activeWorkers_;
        }

        // 在锁外执行任务，减少对任务队列互斥锁的占用时间。
        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeWorkers_ > 0) {
                --activeWorkers_;
            }

            // 当队列清空且无活跃线程时，通知 waitIdle 的等待方。
            if (tasks_.empty() && activeWorkers_ == 0) {
                idleCv_.notify_all();
            }
        }
    }
}
