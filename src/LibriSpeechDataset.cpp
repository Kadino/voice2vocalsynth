#include "Voice2VocalSynth/LibriSpeechDataset.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>
#include <algorithm>

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::string jsonEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(character);
            break;
        }
    }
    return out;
}

[[nodiscard]] bool isNumericSpeakerId(const std::filesystem::path& name)
{
    const auto text = name.string();
    if (text.empty()) {
        return false;
    }
    for (const char character : text) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool pathLooksLikeLibriSpeechTestClean(const std::filesystem::path& root,
                                                   LibriSpeechTestCleanSummary& summary,
                                                   std::string& error)
{
    error.clear();
    summary = {};
    summary.root = root;

    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        error = "Dataset root is not a directory: " + root.string();
        return false;
    }

    std::set<std::string> speakers;
    std::set<std::string> chapters;

    for (const auto& speakerEntry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            error = "Unable to read dataset root: " + ec.message();
            return false;
        }
        if (!speakerEntry.is_directory()) {
            continue;
        }
        if (!isNumericSpeakerId(speakerEntry.path().filename())) {
            continue;
        }
        speakers.insert(speakerEntry.path().filename().string());

        for (const auto& chapterEntry : std::filesystem::directory_iterator(speakerEntry.path(), ec)) {
            if (ec) {
                error = "Unable to read speaker directory: " + ec.message();
                return false;
            }
            if (!chapterEntry.is_directory()) {
                continue;
            }
            if (!isNumericSpeakerId(chapterEntry.path().filename())) {
                continue;
            }
            chapters.insert(speakerEntry.path().filename().string() + "/"
                             + chapterEntry.path().filename().string());

            for (const auto& fileEntry : std::filesystem::directory_iterator(chapterEntry.path(), ec)) {
                if (ec) {
                    error = "Unable to read chapter directory: " + ec.message();
                    return false;
                }
                if (!fileEntry.is_regular_file()) {
                    continue;
                }
                const auto extension = fileEntry.path().extension().string();
                if (extension == ".flac") {
                    ++summary.flacCount;
                } else if (extension == ".txt" && fileEntry.path().filename().string().ends_with(".trans.txt")) {
                    ++summary.transcriptFileCount;
                }
            }
        }
    }

    summary.speakerCount = speakers.size();
    summary.chapterCount = chapters.size();

    if (summary.speakerCount == 0 || summary.chapterCount == 0) {
        error = "Expected LibriSpeech speaker/chapter folders under: " + root.string();
        return false;
    }
    if (summary.flacCount == 0) {
        error = "No .flac files found under: " + root.string();
        return false;
    }
    if (summary.transcriptFileCount == 0) {
        error = "No .trans.txt files found under: " + root.string();
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path defaultLibriSpeechTestCleanRoot(const std::filesystem::path& verifyRoot)
{
    const auto layout = livePhonemeVerifyLayout(verifyRoot);
    return layout.datasets / kLibriSpeechTestCleanRelativePath;
}

std::filesystem::path defaultLibriSpeechDatasetManifestPath(const std::filesystem::path& verifyRoot)
{
    return defaultLibriSpeechTestCleanRoot(verifyRoot).parent_path() / "librispeech-test-clean-manifest.json";
}

LibriSpeechTestCleanValidation validateLibriSpeechTestClean(const std::filesystem::path& root)
{
    LibriSpeechTestCleanValidation result;
    result.valid = pathLooksLikeLibriSpeechTestClean(root, result.summary, result.error);
    return result;
}

std::optional<std::filesystem::path> discoverLibriSpeechTestCleanRoot(const std::filesystem::path& verifyRoot,
                                                                      std::string& note)
{
    note.clear();
    std::vector<std::filesystem::path> candidates;

    if (const char* envRoot = std::getenv("LIBRISPEECH_TEST_CLEAN_ROOT")) {
        candidates.emplace_back(envRoot);
    }
    candidates.push_back(defaultLibriSpeechTestCleanRoot(verifyRoot));
    candidates.push_back(livePhonemeVerifyLayout(verifyRoot).datasets / "test-clean");

    for (const auto& candidate : candidates) {
        const auto validation = validateLibriSpeechTestClean(candidate);
        if (validation.valid) {
            note = "Using dataset root: " + candidate.string();
            return candidate;
        }
    }

    note = "No valid LibriSpeech test-clean root found. Expected "
           + defaultLibriSpeechTestCleanRoot(verifyRoot).string()
           + " or set LIBRISPEECH_TEST_CLEAN_ROOT.";
    return std::nullopt;
}

bool writeLibriSpeechDatasetManifest(const LibriSpeechTestCleanSummary& summary,
                                     const std::filesystem::path& manifestPath,
                                     std::string& error)
{
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(manifestPath.parent_path(), ec);

    std::ofstream output(manifestPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Unable to write dataset manifest: " + manifestPath.string();
        return false;
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"schemaVersion\": 1,\n";
    json << "  \"dataset\": \"librispeech-test-clean\",\n";
    json << "  \"verifiedAt\": \"" << jsonEscape(formatLivePhonemeVerifyRunTimestamp()) << "\",\n";
    json << "  \"root\": \"" << jsonEscape(summary.root.string()) << "\",\n";
    json << "  \"speakerCount\": " << summary.speakerCount << ",\n";
    json << "  \"chapterCount\": " << summary.chapterCount << ",\n";
    json << "  \"flacCount\": " << summary.flacCount << ",\n";
    json << "  \"transcriptFileCount\": " << summary.transcriptFileCount << ",\n";
    json << "  \"sourceArchiveUrl\": \"" << jsonEscape(std::string(kLibriSpeechTestCleanArchiveUrl)) << "\",\n";
    json << "  \"relativeLayout\": \"" << jsonEscape(std::string(kLibriSpeechTestCleanRelativePath)) << "\",\n";
    json << "  \"labelPipeline\": {\n";
    json << "    \"nextStep\": \"montreal-forced-aligner\",\n";
    json << "    \"note\": \"Reference label generation is a separate setup step. MFA is not bundled; "
            "install and tune alignment in a follow-up task.\"\n";
    json << "  }\n";
    json << "}\n";
    output << json.str();
    return static_cast<bool>(output);
}

std::filesystem::path defaultLibriSpeechLabelsRoot(const std::filesystem::path& verifyRoot)
{
    const auto layout = livePhonemeVerifyLayout(verifyRoot);
    return layout.labels / kLibriSpeechLabelsRelativePath;
}

std::filesystem::path defaultLibriSpeechLabelManifestPath(const std::filesystem::path& verifyRoot)
{
    return defaultLibriSpeechLabelsRoot(verifyRoot) / "manifest.json";
}

std::vector<LibriSpeechUtterance> listLibriSpeechUtterances(const std::filesystem::path& datasetRoot,
                                                            const std::size_t maxCount)
{
    std::vector<LibriSpeechUtterance> utterances;
    std::error_code ec;

    for (const auto& speakerEntry : std::filesystem::directory_iterator(datasetRoot, ec)) {
        if (ec || !speakerEntry.is_directory() || !isNumericSpeakerId(speakerEntry.path().filename())) {
            continue;
        }

        for (const auto& chapterEntry : std::filesystem::directory_iterator(speakerEntry.path(), ec)) {
            if (ec || !chapterEntry.is_directory()) {
                continue;
            }

            for (const auto& fileEntry : std::filesystem::directory_iterator(chapterEntry.path(), ec)) {
                if (ec || !fileEntry.is_regular_file()) {
                    continue;
                }
                if (fileEntry.path().filename().string().ends_with(".trans.txt")) {
                    std::ifstream transcriptFile(fileEntry.path());
                    std::string line;
                    while (std::getline(transcriptFile, line)) {
                        if (line.empty()) {
                            continue;
                        }
                        const auto space = line.find(' ');
                        if (space == std::string::npos) {
                            continue;
                        }
                        LibriSpeechUtterance utterance;
                        utterance.id = line.substr(0, space);
                        utterance.transcript = line.substr(space + 1);
                        utterance.flacPath = chapterEntry.path() / (utterance.id + ".flac");
                        if (std::filesystem::exists(utterance.flacPath)) {
                            utterances.push_back(std::move(utterance));
                        }
                    }
                }
            }
        }
    }

    std::sort(utterances.begin(),
              utterances.end(),
              [](const LibriSpeechUtterance& left, const LibriSpeechUtterance& right) {
                  return left.id < right.id;
              });

    if (maxCount > 0 && utterances.size() > maxCount) {
        utterances.resize(maxCount);
    }
    return utterances;
}

} // namespace Voice2VocalSynth
