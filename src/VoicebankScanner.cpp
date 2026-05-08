#include "Voice2VocalSynth/VoicebankScanner.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace Voice2VocalSynth
{
namespace
{

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string readFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open " + path.string());
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

std::string stripUtf8Bom(std::string content)
{
    constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == bom[0] &&
        static_cast<unsigned char>(content[1]) == bom[1] &&
        static_cast<unsigned char>(content[2]) == bom[2]) {
        content.erase(0, 3);
    }
    return content;
}

std::filesystem::path makeGenericRelative(const std::filesystem::path& path,
                                          const std::filesystem::path& base)
{
    std::error_code error;
    auto relative = std::filesystem::relative(path, base, error);
    if (error) {
        return path.lexically_normal();
    }
    return relative.lexically_normal();
}

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

bool isCommentOrEmpty(std::string_view line)
{
    const auto trimmed = trim(line);
    return trimmed.empty() || trimmed.rfind("#", 0) == 0 || trimmed.rfind("//", 0) == 0;
}

std::vector<std::string> splitPrefixMapFields(std::string_view line)
{
    std::vector<std::string> fields;

    const auto splitByDelimiter = [&](char delimiter) {
        std::size_t start = 0;
        while (start <= line.size()) {
            const auto next = line.find(delimiter, start);
            if (next == std::string_view::npos) {
                fields.push_back(trim(line.substr(start)));
                break;
            }

            fields.push_back(trim(line.substr(start, next - start)));
            start = next + 1;
        }
    };

    if (line.find('\t') != std::string_view::npos) {
        splitByDelimiter('\t');
        return fields;
    }

    if (line.find(',') != std::string_view::npos) {
        splitByDelimiter(',');
        return fields;
    }

    std::istringstream stream{std::string(line)};
    std::string field;
    while (stream >> field) {
        fields.push_back(field);
    }
    return fields;
}

bool containsAliasCandidate(const std::vector<AliasCandidate>& candidates,
                            const std::string& alias)
{
    return std::any_of(candidates.begin(), candidates.end(), [&alias](const AliasCandidate& candidate) {
        return candidate.alias == alias;
    });
}

void appendCandidateIfUnique(std::vector<AliasCandidate>& candidates,
                             AliasCandidate candidate)
{
    if (candidate.alias.empty() || containsAliasCandidate(candidates, candidate.alias)) {
        return;
    }
    candidates.push_back(std::move(candidate));
}

} // namespace

VoicebankAliasStyle VoicebankScanResult::aliasStyle() const noexcept
{
    return aliasIndex.aliasStyle();
}

bool VoicebankScanResult::foundOtoIni() const noexcept
{
    return !otoFiles.empty();
}

bool VoicebankScanResult::foundPrefixMap() const noexcept
{
    return !prefixMapFiles.empty();
}

std::vector<VoicebankPrefixMapEntry> parsePrefixMapContent(std::string_view content,
                                                           std::string sourceName)
{
    std::vector<VoicebankPrefixMapEntry> entries;
    std::size_t start = 0;
    std::size_t lineNumber = 1;

    while (start <= content.size()) {
        const auto newline = content.find('\n', start);
        const auto line = newline == std::string_view::npos
                              ? content.substr(start)
                              : content.substr(start, newline - start);

        if (!isCommentOrEmpty(line)) {
            const auto fields = splitPrefixMapFields(line);
            if (fields.size() >= 3 && !fields[0].empty()) {
                VoicebankPrefixMapEntry entry;
                entry.noteName = fields[0];
                entry.prefix = fields[1];
                entry.suffix = fields[2];
                entry.sourceName = sourceName;
                entry.sourceLine = lineNumber;
                entries.push_back(std::move(entry));
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }

        start = newline + 1;
        ++lineNumber;
    }

    return entries;
}

VoicebankPrefixMapMatch findPrefixMapEntry(const std::vector<VoicebankPrefixMapEntry>& entries,
                                           std::string_view noteName)
{
    const auto found = std::find_if(entries.begin(), entries.end(), [noteName](const auto& entry) {
        return entry.noteName == noteName;
    });

    if (found == entries.end()) {
        return {};
    }

    VoicebankPrefixMapMatch match;
    match.found = true;
    match.entry = *found;
    return match;
}

AliasEvent applyPrefixMapToAliasEvent(const AliasEvent& event,
                                      const std::vector<VoicebankPrefixMapEntry>& entries,
                                      std::string_view noteName)
{
    const auto match = findPrefixMapEntry(entries, noteName);
    if (!match.found || (match.entry.prefix.empty() && match.entry.suffix.empty())) {
        return event;
    }

    AliasEvent mapped = event;
    mapped.candidates.clear();

    for (const auto& candidate : event.candidates) {
        appendCandidateIfUnique(mapped.candidates,
                                {match.entry.prefix + candidate.alias + match.entry.suffix,
                                 "prefixMap"});
    }

    for (const auto& candidate : event.candidates) {
        appendCandidateIfUnique(mapped.candidates, candidate);
    }

    return mapped;
}

VoicebankScanResult VoicebankScanner::scan(const std::filesystem::path& rootPath,
                                           const VoicebankScanOptions& options)
{
    VoicebankScanResult result;
    result.rootPath = rootPath;

    std::error_code statusError;
    if (!std::filesystem::exists(rootPath, statusError) ||
        !std::filesystem::is_directory(rootPath, statusError)) {
        result.warnings.push_back("Voicebank folder does not exist or is not a directory: " + rootPath.string());
        return result;
    }

    const auto root = std::filesystem::weakly_canonical(rootPath, statusError);
    result.rootPath = statusError ? rootPath.lexically_normal() : root;

    auto scanOtoFile = [&](const std::filesystem::path& otoPath) {
        const auto relativeOto = makeGenericRelative(otoPath, result.rootPath);
        result.otoFiles.push_back(relativeOto);

        try {
            const auto content = stripUtf8Bom(readFileBytes(otoPath));
            auto entries = parseOtoIniContent(content, relativeOto.generic_string());
            const auto otoDirectory = otoPath.parent_path();

            for (auto& entry : entries) {
                const auto samplePath = otoDirectory / std::filesystem::path(entry.wavFile);
                entry.wavFile = makeGenericRelative(samplePath, result.rootPath).generic_string();
                result.entries.push_back(std::move(entry));
            }
        } catch (const std::exception& exception) {
            result.warnings.push_back("Unable to parse " + relativeOto.generic_string() + ": " + exception.what());
        }
    };

    auto scanPrefixMapFile = [&](const std::filesystem::path& mapPath) {
        const auto relativeMap = makeGenericRelative(mapPath, result.rootPath);
        result.prefixMapFiles.push_back(relativeMap);

        try {
            const auto content = stripUtf8Bom(readFileBytes(mapPath));
            auto entries = parsePrefixMapContent(content, relativeMap.generic_string());
            result.prefixMapEntries.insert(result.prefixMapEntries.end(),
                                           std::make_move_iterator(entries.begin()),
                                           std::make_move_iterator(entries.end()));
        } catch (const std::exception& exception) {
            result.warnings.push_back("Unable to parse " + relativeMap.generic_string() + ": " + exception.what());
        }
    };

    if (options.recursive) {
        std::filesystem::recursive_directory_iterator iterator(result.rootPath,
                                                              std::filesystem::directory_options::skip_permission_denied,
                                                              statusError);
        const std::filesystem::recursive_directory_iterator end;
        while (!statusError && iterator != end) {
            if (iterator->is_regular_file(statusError)) {
                if (matchesConfiguredFileName(iterator->path(),
                                              options.otoFileName,
                                              options.caseInsensitiveOtoFileName)) {
                    scanOtoFile(iterator->path());
                } else if (matchesConfiguredFileName(iterator->path(),
                                                     options.prefixMapFileName,
                                                     options.caseInsensitiveOtoFileName)) {
                    scanPrefixMapFile(iterator->path());
                }
            }
            iterator.increment(statusError);
        }
    } else {
        std::filesystem::directory_iterator iterator(result.rootPath,
                                                    std::filesystem::directory_options::skip_permission_denied,
                                                    statusError);
        const std::filesystem::directory_iterator end;
        while (!statusError && iterator != end) {
            if (iterator->is_regular_file(statusError)) {
                if (matchesConfiguredFileName(iterator->path(),
                                              options.otoFileName,
                                              options.caseInsensitiveOtoFileName)) {
                    scanOtoFile(iterator->path());
                } else if (matchesConfiguredFileName(iterator->path(),
                                                     options.prefixMapFileName,
                                                     options.caseInsensitiveOtoFileName)) {
                    scanPrefixMapFile(iterator->path());
                }
            }
            iterator.increment(statusError);
        }
    }

    if (statusError) {
        result.warnings.push_back("Voicebank scan stopped early: " + statusError.message());
    }

    std::sort(result.otoFiles.begin(), result.otoFiles.end());
    std::sort(result.prefixMapFiles.begin(), result.prefixMapFiles.end());
    if (result.otoFiles.empty()) {
        result.warnings.push_back("No oto.ini files were found in " + result.rootPath.string());
    }

    result.aliasIndex = buildVoicebankAliasIndex(result.entries);
    return result;
}

bool VoicebankScanner::matchesConfiguredFileName(const std::filesystem::path& path,
                                                 const std::string& expectedFileName,
                                                 bool caseInsensitive)
{
    auto actual = path.filename().string();
    auto expected = expectedFileName;
    if (caseInsensitive) {
        actual = lowerAscii(std::move(actual));
        expected = lowerAscii(std::move(expected));
    }
    return actual == expected;
}

} // namespace Voice2VocalSynth
