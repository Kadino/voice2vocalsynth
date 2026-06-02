#include "Voice2VocalSynth/PhonemeOnnxAsyncRunner.h"

namespace Voice2VocalSynth
{

PhonemeOnnxAsyncRunner::PhonemeOnnxAsyncRunner() = default;

PhonemeOnnxAsyncRunner::~PhonemeOnnxAsyncRunner()
{
    stop();
}

bool PhonemeOnnxAsyncRunner::running() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return worker_running_;
}

bool PhonemeOnnxAsyncRunner::start(const std::filesystem::path& model_path, std::string& error)
{
    stop();

    auto runner = std::make_unique<PhonemeOnnxRunner>();
    if (!runner->load(model_path, error)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        runner_ = std::move(runner);
        stop_requested_ = false;
        worker_running_ = true;
    }

    worker_ = std::thread([this] { worker_loop(); });
    return true;
}

void PhonemeOnnxAsyncRunner::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    cv_in_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    worker_running_ = false;
    stop_requested_ = false;
    pending_.clear();
    completed_.clear();
    runner_.reset();
    cv_out_.notify_all();
}

std::uint64_t PhonemeOnnxAsyncRunner::enqueue(PhonemeOnnxAsyncJobInput job)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!worker_running_) {
        return 0;
    }

    QueuedJob queued;
    queued.job_id = next_job_id_.fetch_add(1, std::memory_order_relaxed);
    queued.features = std::move(job.features);
    queued.stream_time_start_seconds = job.stream_time_start_seconds;
    queued.enqueued_steady_time = std::chrono::steady_clock::now();
    const auto id = queued.job_id;
    pending_.push_back(std::move(queued));
    lock.unlock();
    cv_in_.notify_one();
    return id;
}

bool PhonemeOnnxAsyncRunner::try_pop_completed(PhonemeOnnxAsyncJobOutput& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (completed_.empty()) {
        return false;
    }
    out = std::move(completed_.front());
    completed_.pop_front();
    return true;
}

void PhonemeOnnxAsyncRunner::worker_loop()
{
    for (;;) {
        QueuedJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_in_.wait(lock, [this] { return !pending_.empty() || stop_requested_; });
            if (stop_requested_ && pending_.empty()) {
                break;
            }
            if (pending_.empty()) {
                continue;
            }
            job = std::move(pending_.front());
            pending_.pop_front();
        }

        PhonemeOnnxAsyncJobOutput out;
        out.job_id = job.job_id;
        out.stream_time_start_seconds = job.stream_time_start_seconds;
        out.enqueued_steady_time = job.enqueued_steady_time;
        out.run = runner_->run(job.features);
        out.inference_completed_steady_time = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back(std::move(out));
        }
        cv_out_.notify_one();
    }
}

} // namespace Voice2VocalSynth
