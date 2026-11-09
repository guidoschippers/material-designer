#pragma once

#include <functional>
#include <stop_token>

namespace md {

/// Runs fn(i) for i in [0, count) on all hardware threads. Work is handed out in index order,
/// and each index is processed exactly once, so results are independent of the thread count.
/// Throws md::Cancelled when `stop` is requested; rethrows the first exception from `fn`.
void parallelFor(int count, const std::function<void(int)>& fn, std::stop_token stop = {});

/// Limits the number of worker threads (0 = use all hardware threads). Mainly for tests.
void setMaxThreads(int threads);
int maxThreads();

} // namespace md
