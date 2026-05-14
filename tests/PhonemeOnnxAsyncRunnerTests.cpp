#include <Voice2VocalSynth/PhonemeOnnxAsyncRunner.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

void asyncRunnerIsInertWhenOnnxDisabled()
{
    PhonemeOnnxAsyncRunner async;
    std::string error;
    assert(!async.start(std::filesystem::path{"missing.onnx"}, error));
    assert(!async.running());
    assert(async.enqueue({}) == 0U);
}

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
void asyncWorkerReturnsTimestampedIdentityResults()
{
    PhonemeOnnxAsyncRunner async;
    std::string error;
    const std::filesystem::path model{VOICE2VOCALSYNTH_TEST_ONNX_MODEL};
    assert(async.start(model, error));
    assert(error.empty());
    assert(async.running());

    PhonemeOnnxAsyncJobInput job_a;
    job_a.stream_time_start_seconds = 0.01;
    job_a.features.assign(32, 0.125F);

    PhonemeOnnxAsyncJobInput job_b;
    job_b.stream_time_start_seconds = 0.02;
    job_b.features.assign(32, -0.5F);

    const auto id_a = async.enqueue(std::move(job_a));
    const auto id_b = async.enqueue(std::move(job_b));
    assert(id_a != 0U);
    assert(id_b != 0U);
    assert(id_a != id_b);

    PhonemeOnnxAsyncJobOutput out_a;
    assert(async.wait_pop_completed_for(out_a, std::chrono::seconds{5}));
    assert(out_a.job_id == id_a);
    assert(out_a.run.ok);
    assert(out_a.run.output.size() == 32U);
    assert(out_a.stream_time_start_seconds == 0.01);

    PhonemeOnnxAsyncJobOutput out_b;
    assert(async.wait_pop_completed_for(out_b, std::chrono::seconds{5}));
    assert(out_b.job_id == id_b);
    assert(out_b.run.ok);
    assert(out_b.stream_time_start_seconds == 0.02);

    assert(out_b.inference_completed_steady_time >= out_a.inference_completed_steady_time);

    async.stop();
    assert(!async.running());
}
#endif

} // namespace

int main()
{
    asyncRunnerIsInertWhenOnnxDisabled();

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    asyncWorkerReturnsTimestampedIdentityResults();
    std::cout << "PhonemeOnnxAsyncRunner tests passed (ONNX enabled)\n";
#else
    std::cout << "PhonemeOnnxAsyncRunner tests passed (ONNX disabled)\n";
#endif
    return 0;
}
