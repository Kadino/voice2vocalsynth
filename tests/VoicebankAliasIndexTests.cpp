#include <Voice2VocalSynth/PhonemeFallbackMapper.h>
#include <Voice2VocalSynth/VoicebankAliasIndex.h>

#include <cassert>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
using namespace Voice2VocalSynth;

void parsesOtoEntries()
{
    const auto entries = parseOtoIniContent(
        "a.wav=a,0,120,300,45,10\n"
        "ka.wav=ka,5.5,80,-120,35,8\r\n");

    assert(entries.size() == 2);
    assert(entries[0].wavFile == "a.wav");
    assert(entries[0].alias == "a");
    assert(entries[0].offsetMs == 0.0);
    assert(entries[0].consonantMs == 120.0);
    assert(entries[0].cutoffMs == 300.0);
    assert(entries[0].preutteranceMs == 45.0);
    assert(entries[0].overlapMs == 10.0);

    assert(entries[1].wavFile == "ka.wav");
    assert(entries[1].alias == "ka");
    assert(entries[1].offsetMs == 5.5);
    assert(entries[1].cutoffMs == -120.0);
}

void ignoresBlankAndMalformedLines()
{
    const auto entries = parseOtoIniContent(
        "\n"
        "# comment\n"
        "not an oto line\n"
        "sa.wav=sa,0,80,200,30,5\n");

    assert(entries.size() == 1);
    assert(entries[0].alias == "sa");
}

void resolvesPrimaryCandidate()
{
    const auto entries = parseOtoIniContent(
        "ka.wav=ka,0,90,200,30,5\n"
        "to.wav=to,0,75,150,25,4\n");
    const auto index = VoicebankAliasIndex::fromEntries(entries);

    const PhonemeFallbackMapper mapper;
    const auto events = mapper.mapPhonemes({"K", "AE", "T"});

    const auto first = index.resolve(events[0]);
    const auto second = index.resolve(events[1]);

    assert(first.resolved);
    assert(first.selectedAlias == "ka");
    assert(first.candidateIndex == 0);

    assert(second.resolved);
    assert(second.selectedAlias == "to");
    assert(second.candidateIndex == 0);
    assert(second.usedPartialFallback);
}

void resolvesFallbackCandidateWhenPrimaryIsMissing()
{
    auto options = PhonemeFallbackMapper::makeDefaultOptions();
    options.consonantSubstitutions["TH"] = "t";
    options.consonantFallbackCandidates["TH"] = {"s"};
    const PhonemeFallbackMapper mapper(std::move(options));
    const auto events = mapper.mapPhonemes({"TH", "AE"});

    VoicebankAliasIndex index;
    index.addAlias("sa");

    const auto resolution = index.resolve(events[0]);

    assert(resolution.resolved);
    assert(resolution.selectedAlias == "sa");
    assert(resolution.candidateIndex == 1);
    assert(resolution.reason == "fallback");
}

void reportsUnresolvedCandidates()
{
    const PhonemeFallbackMapper mapper;
    const auto events = mapper.mapPhonemes({"V", "AE"});

    VoicebankAliasIndex index;
    index.addAlias("ka");

    const auto resolution = index.resolve(events[0]);

    assert(!resolution.resolved);
    assert(resolution.selectedAlias.empty());
    assert(resolution.candidateIndex == std::numeric_limits<std::size_t>::max());
    assert(!resolution.missingCandidates.empty());
    assert(resolution.missingCandidates[0] == "ba");
}

void detectsAliasStyle()
{
    VoicebankAliasIndex romaji;
    romaji.addAlias("ka");
    romaji.addAlias("shi");
    assert(romaji.detectAliasStyle() == VoicebankAliasStyle::Romaji);

    VoicebankAliasIndex nonAscii;
    nonAscii.addAlias("\xE3\x81\x8B");
    nonAscii.addAlias("\xE3\x81\x97");
    assert(nonAscii.detectAliasStyle() == VoicebankAliasStyle::NonAscii);

    VoicebankAliasIndex mixed;
    mixed.addAlias("ka");
    mixed.addAlias("\xE3\x81\x97");
    assert(mixed.detectAliasStyle() == VoicebankAliasStyle::Mixed);
}

} // namespace

int main()
{
    parsesOtoEntries();
    ignoresBlankAndMalformedLines();
    resolvesPrimaryCandidate();
    resolvesFallbackCandidateWhenPrimaryIsMissing();
    reportsUnresolvedCandidates();
    detectsAliasStyle();

    std::cout << "VoicebankAliasIndex tests passed\n";
    return 0;
}
