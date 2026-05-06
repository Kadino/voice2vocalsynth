#include "Voice2VocalSynth/VoicebankAliasIndex.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace Voice2VocalSynth
{
namespace
{

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::vector<std::string> splitCommaFields(std::string_view value)
{
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(trim(value.substr(start)));
            break;
        }

        fields.push_back(trim(value.substr(start, comma - start)));
        start = comma + 1;
    }

    return fields;
}

double parseDoubleField(const std::string& value, std::string_view fieldName)
{
    double parsed = 0.0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [parsedEnd, error] = std::from_chars(begin, end, parsed);

    if (error != std::errc{} || parsedEnd != end) {
        throw std::invalid_argument("Invalid oto.ini numeric field " + std::string(fieldName) + ": " + value);
    }

    return parsed;
}

bool isCommentOrEmpty(std::string_view line)
{
    const auto trimmed = trim(line);
    return trimmed.empty() || trimmed.rfind("#", 0) == 0 || trimmed.rfind("//", 0) == 0;
}

bool aliasContainsNonAscii(std::string_view alias)
{
    return std::any_of(alias.begin(), alias.end(), [](unsigned char character) {
        return character > 0x7F;
    });
}

bool aliasContainsAsciiLetter(std::string_view alias)
{
    return std::any_of(alias.begin(), alias.end(), [](unsigned char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
    });
}

} // namespace

std::optional<OtoEntry> parseOtoIniLine(std::string_view line,
                                        std::size_t sourceLine,
                                        std::string sourceName)
{
    if (isCommentOrEmpty(line)) {
        return std::nullopt;
    }

    const auto equals = line.find('=');
    if (equals == std::string_view::npos) {
        return std::nullopt;
    }

    auto wavFile = trim(line.substr(0, equals));
    const auto fields = splitCommaFields(line.substr(equals + 1));
    if (wavFile.empty() || fields.size() < 6) {
        return std::nullopt;
    }

    OtoEntry entry;
    entry.wavFile = std::move(wavFile);
    entry.alias = fields[0].empty() ? entry.wavFile : fields[0];
    entry.offsetMs = parseDoubleField(fields[1], "offset");
    entry.consonantMs = parseDoubleField(fields[2], "consonant");
    entry.cutoffMs = parseDoubleField(fields[3], "cutoff");
    entry.preutteranceMs = parseDoubleField(fields[4], "preutterance");
    entry.overlapMs = parseDoubleField(fields[5], "overlap");
    entry.sourceName = std::move(sourceName);
    entry.sourceLine = sourceLine;
    return entry;
}

std::vector<OtoEntry> parseOtoIniContent(std::string_view content, std::string sourceName)
{
    std::vector<OtoEntry> entries;
    std::size_t start = 0;
    std::size_t lineNumber = 1;

    while (start <= content.size()) {
        const auto newline = content.find('\n', start);
        const auto line = newline == std::string_view::npos
                              ? content.substr(start)
                              : content.substr(start, newline - start);

        if (auto entry = parseOtoIniLine(line, lineNumber, sourceName)) {
            entries.push_back(std::move(*entry));
        }

        if (newline == std::string_view::npos) {
            break;
        }

        start = newline + 1;
        ++lineNumber;
    }

    return entries;
}

void VoicebankAliasIndex::addEntry(OtoEntry entry)
{
    const auto alias = entry.alias;
    entriesByAlias_[alias].push_back(std::move(entry));
}

void VoicebankAliasIndex::addAlias(std::string alias)
{
    OtoEntry entry;
    entry.alias = std::move(alias);
    addEntry(std::move(entry));
}

VoicebankAliasIndex VoicebankAliasIndex::fromEntries(const std::vector<OtoEntry>& entries)
{
    return buildVoicebankAliasIndex(entries);
}

bool VoicebankAliasIndex::containsAlias(std::string_view alias) const
{
    return entriesByAlias_.find(std::string(alias)) != entriesByAlias_.end();
}

const OtoEntry* VoicebankAliasIndex::findFirst(std::string_view alias) const
{
    const auto found = entriesByAlias_.find(std::string(alias));
    if (found == entriesByAlias_.end() || found->second.empty()) {
        return nullptr;
    }

    return &found->second.front();
}

AliasResolution VoicebankAliasIndex::resolve(const AliasEvent& event) const
{
    AliasResolution resolution;
    resolution.role = event.role;
    resolution.sourcePhonemes = event.sourcePhonemes;
    resolution.usedPartialFallback = event.isPartialFallback();
    resolution.candidateIndex = std::numeric_limits<std::size_t>::max();

    for (std::size_t index = 0; index < event.candidates.size(); ++index) {
        const auto& candidate = event.candidates[index];
        resolution.attemptedAliases.push_back(candidate.alias);

        if (const auto* entry = findFirst(candidate.alias)) {
            resolution.resolved = true;
            resolution.selectedAlias = candidate.alias;
            resolution.candidateIndex = index;
            resolution.reason = candidate.reason;
            resolution.entry = entry;
            return resolution;
        }

        resolution.missingCandidates.push_back(candidate.alias);
    }

    resolution.reason = "missing";
    return resolution;
}

std::vector<AliasResolution> VoicebankAliasIndex::resolveAll(const std::vector<AliasEvent>& events) const
{
    std::vector<AliasResolution> resolutions;
    resolutions.reserve(events.size());

    for (const auto& event : events) {
        resolutions.push_back(resolve(event));
    }

    return resolutions;
}

VoicebankAliasStyle VoicebankAliasIndex::aliasStyle() const noexcept
{
    return detectAliasStyle();
}

VoicebankAliasStyle VoicebankAliasIndex::detectAliasStyle() const noexcept
{
    bool hasRomajiLikeAlias = false;
    bool hasNonAsciiAlias = false;

    for (const auto& [alias, entries] : entriesByAlias_) {
        (void)entries;
        hasRomajiLikeAlias = hasRomajiLikeAlias || aliasContainsAsciiLetter(alias);
        hasNonAsciiAlias = hasNonAsciiAlias || aliasContainsNonAscii(alias);
    }

    if (hasRomajiLikeAlias && hasNonAsciiAlias) {
        return VoicebankAliasStyle::Mixed;
    }
    if (hasNonAsciiAlias) {
        return VoicebankAliasStyle::NonAscii;
    }
    if (hasRomajiLikeAlias) {
        return VoicebankAliasStyle::Romaji;
    }

    return VoicebankAliasStyle::Empty;
}

std::size_t VoicebankAliasIndex::aliasCount() const noexcept
{
    return entriesByAlias_.size();
}

std::size_t VoicebankAliasIndex::entryCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& [alias, entries] : entriesByAlias_) {
        (void)alias;
        count += entries.size();
    }
    return count;
}

std::vector<std::string> VoicebankAliasIndex::aliases() const
{
    std::vector<std::string> result;
    result.reserve(entriesByAlias_.size());

    for (const auto& [alias, entries] : entriesByAlias_) {
        (void)entries;
        result.push_back(alias);
    }

    std::sort(result.begin(), result.end());
    return result;
}

VoicebankAliasIndex buildVoicebankAliasIndex(const std::vector<OtoEntry>& entries)
{
    VoicebankAliasIndex index;
    for (const auto& entry : entries) {
        index.addEntry(entry);
    }
    return index;
}

} // namespace Voice2VocalSynth
