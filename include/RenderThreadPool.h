#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @brief 通用线程池模块，提供任务提交、并行区间执行与空闲同步能力。
 */
class RenderThreadPool
{
public:
    /**
     * @brief 创建线程池并启动工作线程。
     * @param threadCount 线程数量，传入 0 时自动使用硬件并发数。
     */
    explicit RenderThreadPool(std::size_t threadCount = 0);

    /**
     * @brief 销毁线程池并等待已提交任务完成。
     */
    ~RenderThreadPool();

    RenderThreadPool(const RenderThreadPool&) = delete;
    RenderThreadPool& operator=(const RenderThreadPool&) = delete;
    RenderThreadPool(RenderThreadPool&&) = delete;
    RenderThreadPool& operator=(RenderThreadPool&&) = delete;

    /**
     * @brief 动态调整线程池工作线程数量。
     * @param threadCount 新线程数量，传入 0 时自动使用硬件并发数。
     * @return 无返回值。
     */
    void setThreadCount(std::size_t threadCount);

    /**
     * @brief 获取当前线程池工作线程数量。
     * @return 返回线程数量。
     */
    std::size_t threadCount() const;

    /**
     * @brief 获取待执行任务队列中的任务数量。
     * @return 返回尚未被工作线程取走的任务数。
     */
    std::size_t pendingTaskCount() const;

    /**
     * @brief 判断线程池是否空闲（无排队任务且无正在执行任务）。
     * @return 空闲返回 true，否则返回 false。
     */
    bool idle() const;

    /**
     * @brief 阻塞等待直到线程池进入空闲状态。
     * @return 无返回值。
     */
    void waitIdle();

    /**
     * @brief 提交一个可调用任务到线程池并返回 future。
     * @tparam Func 可调用对象类型。
     * @tparam Args 参数类型列表。
     * @param func 可调用对象。
     * @param args 可调用对象参数。
     * @return 返回任务结果对应的 future。
     */
    template <typename Func, typename... Args>
    auto enqueue(Func&& func, Args&&... args)
        -> std::future<std::invoke_result_t<Func, Args...>>;

    /**
     * @brief 按区间分块并行执行任务函数。
     * @param begin 起始索引（包含）。
     * @param end 结束索引（不包含）。
     * @param minChunkSize 每个任务最小分块大小，传入 0 会自动按 1 处理。
     * @param rangeTask 区间任务函数，参数为 [rangeBegin, rangeEnd)。
     * @return 无返回值。
     */
    void parallelFor(
        std::size_t begin,
        std::size_t end,
        std::size_t minChunkSize,
        const std::function<void(std::size_t, std::size_t)>& rangeTask);

private:
    /**
     * @brief 归一化线程数量参数。
     * @param requested 用户请求线程数。
     * @return 返回最终有效线程数（至少为 1）。
     */
    static std::size_t NormalizeThreadCount(std::size_t requested);

    /**
     * @brief 启动指定数量的工作线程。
     * @param threadCount 待启动线程数量。
     * @return 无返回值。
     */
    void StartWorkers(std::size_t threadCount);

    /**
     * @brief 停止并回收全部工作线程。
     * @return 无返回值。
     */
    void StopWorkers();

    /**
     * @brief 工作线程主循环，负责取任务并执行。
     * @return 无返回值。
     */
    void WorkerLoop();

private:
    mutable std::mutex mutex_;
    std::condition_variable taskCv_;
    std::condition_variable idleCv_;

    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;

    std::size_t activeWorkers_ = 0;
    bool stopping_ = false;
};

template <typename Func, typename... Args>
auto RenderThreadPool::enqueue(Func&& func, Args&&... args)
    -> std::future<std::invoke_result_t<Func, Args...>>
{
    using ReturnType = std::invoke_result_t<Func, Args...>;

    auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::move(boundTask));
    std::future<ReturnType> future = packagedTask->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("RenderThreadPool is stopping, cannot enqueue new task.");
        }

        // 将打包任务包装为无参任务，统一进入线程池任务队列。
        tasks_.emplace_back([packagedTask]() {
            (*packagedTask)();
        });
    }

    taskCv_.notify_one();
    return future;
}
