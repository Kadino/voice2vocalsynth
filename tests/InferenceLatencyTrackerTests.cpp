#include <Voice2VocalSynth/InferenceLatencyTracker.h>

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace Voice2VocalSynth;

void clamps_large_jumps()
{
    InferenceLatencyTrackerOptions opt;
    opt.window_size = 4;
    opt.max_update_step_ms = 5.0;
    opt.initial_estimate_ms = 10.0;
    InferenceLatencyTracker tracker(opt);

    tracker.observe_ms(20.0);
    tracker.observe_ms(20.0);
    const double before = tracker.estimate_ms();
    tracker.observe_ms(100.0);
    assert(tracker.estimate_ms() <= before + opt.max_update_step_ms + 1.0e-6);
}

void tracks_median_window()
{
    InferenceLatencyTracker tracker;
    tracker.observe_ms(20.0);
    tracker.observe_ms(22.0);
    tracker.observe_ms(21.0);
    assert(std::abs(tracker.estimate_ms() - 21.0) < 6.0);
}

} // namespace

int main()
{
    clamps_large_jumps();
    tracks_median_window();
    std::cout << "InferenceLatencyTracker tests passed\n";
    return 0;
}
