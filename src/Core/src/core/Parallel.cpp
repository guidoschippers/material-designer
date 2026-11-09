#include "md/core/Parallel.h"

#include "md/core/Error.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace md {

namespace {

std::atomic<int> g_maxThreads{0};

} // namespace

void setMaxThreads(int threads)
{
    g_maxThreads = std::max(0, threads);
}

int maxThreads()
{
    const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int limit = g_maxThreads.load();
    return limit > 0 ? std::min(limit, hw) : hw;
}

void parallelFor(int count, const std::function<void(int)>& fn, std::stop_token stop)
{
    if (count <= 0) {
        return;
    }
    std::atomic<int> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex errorMutex;

    auto worker = [&] {
        for (;;) {
            if (failed.load(std::memory_order_relaxed) || stop.stop_requested()) {
                return;
            }
            const int i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= count) {
                return;
            }
            try {
                fn(i);
            } catch (...) {
                std::lock_guard lock(errorMutex);
                if (!error) {
                    error = std::current_exception();
                }
                failed = true;
            }
        }
    };

    const int threadCount = std::min(maxThreads(), count);
    std::vector<std::jthread> threads;
    threads.reserve(static_cast<std::size_t>(threadCount - 1));
    for (int t = 1; t < threadCount; ++t) {
        threads.emplace_back(worker);
    }
    worker();
    threads.clear(); // joins

    if (error) {
        std::rethrow_exception(error);
    }
    if (stop.stop_requested()) {
        throw Cancelled();
    }
}

} // namespace md
