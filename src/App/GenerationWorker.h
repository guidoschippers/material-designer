#pragma once

// Runs material generation on a background thread so the UI stays responsive.
// Starting a new job cancels the running one; results of superseded jobs are discarded.

#include "md/core/Material.h"
#include "md/io/Export.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class GenerationWorker {
public:
    struct Result {
        std::uint64_t jobId = 0;
        md::MaterialSettings settings;
        int size = 0;
        md::MaterialMaps maps;                    ///< Empty for failed jobs.
        std::optional<md::ExportResult> exported; ///< Set for export jobs.
        std::string error;                        ///< Non-empty when the job failed.
        double milliseconds = 0.0;
    };

    GenerationWorker() = default;
    ~GenerationWorker();
    GenerationWorker(const GenerationWorker&) = delete;
    GenerationWorker& operator=(const GenerationWorker&) = delete;

    /// Generates `settings` at resolution `size`. When exportDir is set, the maps are also written there.
    std::uint64_t start(const md::Material& material, const md::MaterialSettings& settings, int size,
                        std::optional<std::filesystem::path> exportDir = std::nullopt);

    /// Requests cancellation of the running job (non-blocking).
    void cancel();

    bool busy() const { return busy_.load(); }

    /// Returns the result of the most recent job once it has finished (only once).
    std::optional<Result> takeResult();

private:
    std::jthread thread_;
    std::atomic<bool> busy_{false};
    std::atomic<std::uint64_t> latestJob_{0};
    std::mutex mutex_;
    std::optional<Result> result_;
};
