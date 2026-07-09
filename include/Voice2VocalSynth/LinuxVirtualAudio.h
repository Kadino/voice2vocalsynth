#pragma once

#include "Voice2VocalSynth/LivePhonemeVerifyPaths.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Voice2VocalSynth
{

inline constexpr std::string_view kLinuxAudioRoutePipeWireLoopback = "pipewire-loopback";
inline constexpr std::string_view kLinuxAudioRouteAlsaSndAloop = "alsa-snd-aloop";
inline constexpr std::string_view kLinuxAudioRouteJack = "jack";

/// Recommended PipeWire null-sink name for live phoneme verification routing.
inline constexpr std::string_view kLinuxAudioRecommendedSinkName = "LivePhonemeVerify";

struct LinuxVirtualAudioRoute
{
    std::string routeId;
    std::string playbackDevice;
    std::string captureDevice;
    std::string serverName;
    std::string note;
};

struct LinuxVirtualAudioValidation
{
    bool valid = false;
    bool probePassed = false;
    LinuxVirtualAudioRoute route;
    std::vector<std::string> warnings;
    std::string error;
};

struct LinuxVirtualAudioManifestInfo
{
    LinuxVirtualAudioRoute route;
    bool probePassed = false;
    std::vector<std::string> warnings;
};

struct LinuxVirtualAudioCheckReportParseResult
{
    bool ok = false;
    LinuxVirtualAudioValidation validation;
    std::string error;
};

/// Persistent validation manifest under the verification root.
[[nodiscard]] std::filesystem::path defaultLinuxVirtualAudioManifestPath(
    const std::filesystem::path& verifyRoot = defaultLivePhonemeVerifyRoot());

/// Extracts PulseAudio/PipeWire device names from `pactl list sinks` output.
[[nodiscard]] std::vector<std::string> parsePactlDeviceNames(std::string_view listing,
                                                              std::string_view blockHeader);

/// Returns the monitor source name for a sink, e.g. `LivePhonemeVerify.monitor`.
[[nodiscard]] std::string pactlMonitorSourceName(std::string_view sinkName);

/// Detects the recommended PipeWire/Pulse loopback route from pactl listings.
[[nodiscard]] std::optional<LinuxVirtualAudioRoute> detectPipeWireLoopbackRoute(
    std::string_view pactlInfo,
    std::string_view pactlSinks,
    std::string_view pactlSources);

/// Detects ALSA `snd-aloop` playback/capture pair names from `aplay -l` / `arecord -l`.
[[nodiscard]] std::optional<LinuxVirtualAudioRoute> detectAlsaLoopbackRoute(
    std::string_view aplayListing,
    std::string_view arecordListing);

/// Parses the JSON report emitted by `scripts/validate_linux_virtual_audio.sh`.
[[nodiscard]] LinuxVirtualAudioCheckReportParseResult parseLinuxVirtualAudioCheckReport(
    std::string_view json);

[[nodiscard]] bool writeLinuxVirtualAudioManifest(const LinuxVirtualAudioManifestInfo& info,
                                                  const std::filesystem::path& manifestPath,
                                                  std::string& error);

} // namespace Voice2VocalSynth
