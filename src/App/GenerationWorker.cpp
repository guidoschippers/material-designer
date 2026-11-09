#include "GenerationWorker.h"

#include "md/core/Error.h"

#include <chrono>

GenerationWorker::~GenerationWorker()
{
    cancel();
}

std::uint64_t GenerationWorker::start(const md::Material& material, const md::MaterialSettings& settings, int size,
                                      std::optional<std::filesystem::path> exportDir)
{
    const std::uint64_t jobId = ++latestJob_;
    // Replacing the jthread requests stop on the previous job and joins it. Generation checks the
    // stop token after every row, so this only blocks briefly.
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    busy_ = true;
    thread_ = std::jthread([this, &material, settings, size, jobId, exportDir](std::stop_token stop) {
        Result r;
        r.jobId = jobId;
        r.settings = settings;
        r.size = size;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            md::GenerateOptions options;
            options.sizeOverride = size;
            options.stop = stop;
            r.maps = material.generate(settings, options);
            if (exportDir) {
                r.exported = md::exportMaps(r.maps, settings, *exportDir);
            }
        } catch (const md::Cancelled&) {
            // Superseded or cancelled: no result. (start() joins this thread before starting the next job.)
            busy_ = false;
            return;
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        r.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (jobId == latestJob_.load()) {
            std::lock_guard lock(mutex_);
            result_ = std::move(r);
        }
        busy_ = false;
    });
    return jobId;
}

void GenerationWorker::cancel()
{
    if (thread_.joinable()) {
        thread_.request_stop();
    }
}

std::optional<GenerationWorker::Result> GenerationWorker::takeResult()
{
    std::lock_guard lock(mutex_);
    std::optional<Result> r = std::move(result_);
    result_.reset();
    return r;
}
