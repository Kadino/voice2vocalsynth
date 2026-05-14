#pragma once

#include "Voice2VocalSynth/PhonemeOnnxRunner.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Voice2VocalSynth
{

struct PhonemeOnnxAsyncJobInput
{
    std::vector<float> features;
    double stream_time_start_seconds = 0.0;
};

struct PhonemeOnnxAsyncJobOutput
{
    std::uint64_t job_id = 0;
    double stream_time_start_seconds = 0.0;
    std::chrono::steady_clock::time_point inference_completed_steady_time {};
    PhonemeOnnxRunner::RunResult run;
};

class PhonemeOnnxAsyncRunner
{
public:
    PhonemeOnnxAsyncRunner();
    ~PhonemeOnnxAsyncRunner();

    PhonemeOnnxAsyncRunner(const PhonemeOnnxAsyncRunner&) = delete;
    PhonemeOnnxAsyncRunner& operator=(const PhonemeOnnxAsyncRunner&) = delete;
    PhonemeOnnxAsyncRunner(PhonemeOnnxAsyncRunner&&) = delete;
    PhonemeOnnxAsyncRunner& operator=(PhonemeOnnxAsyncRunner&&) = delete;

    [[nodiscard]] bool running() const noexcept;

    bool start(const std::filesystem::path& model_path, std::string& error);
    void stop();

    [[nodiscard]] std::uint64_t enqueue(PhonemeOnnxAsyncJobInput job);

    [[nodiscard]] bool try_pop_completed(PhonemeOnnxAsyncJobOutput& out);

    template <class Rep, class Period>
    bool wait_pop_completed_for(PhonemeOnnxAsyncJobOutput& out,
                                const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_out_.wait_for(lock, timeout, [this] { return !completed_.empty() || !worker_running_; })) {
            return false;
        }
        if (completed_.empty()) {
            return false;
        }
        out = std::move(completed_.front());
        completed_.pop_front();
        return true;
    }

private:
    struct QueuedJob
    {
        std::uint64_t job_id = 0;
        std::vector<float> features;
        double stream_time_start_seconds = 0.0;
    };

    void worker_loop();

    std::unique_ptr<PhonemeOnnxRunner> runner_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_in_;
    std::condition_variable cv_out_;
    std::deque<QueuedJob> pending_;
    std::deque<PhonemeOnnxAsyncJobOutput> completed_;
    std::atomic<std::uint64_t> next_job_id_{1};
    bool stop_requested_ = false;
    bool worker_running_ = false;
};

} // namespace Voice2VocalSynth
