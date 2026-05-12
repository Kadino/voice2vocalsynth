#include <Voice2VocalSynth/PitchTarget.h>
#include <Voice2VocalSynth/VoicebankScanner.h>

#include <cassert>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
using namespace Voice2VocalSynth;

std::filesystem::path makeTempVoicebankRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() /
                ("voice2vocalsynth-scanner-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

void writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

void infersBankRootPitchFromPrefixMapMedian()
{
    const auto root = makeTempVoicebankRoot();
    writeTextFile(root / "oto.ini", "ka.wav=ka,0,80,180,25,5\n");
    writeTextFile(root / "prefix.map",
                   "A3\t\t_A3\n"
                   "C4\tC4_\t_C4\n"
                   "E5\tE5_\t_E5\n");

    const auto scan = VoicebankScanner::scan(root);
    assert(scan.hasBankRootRecordingPitch());
    assert(scan.bankRootNoteName == "C4");
    assert(std::abs(scan.bankRootRecordingFrequencyHz - PitchTargetCalculator::midiToFrequency(60.0)) < 0.02);

    std::filesystem::remove_all(root);
}

void infersBankRootPitchFromAliasTokensWhenNoPrefixMap()
{
    const auto root = makeTempVoicebankRoot();
    writeTextFile(root / "oto.ini", "ka.wav=C4_ka-CV,0,80,180,25,5\n");

    const auto scan = VoicebankScanner::scan(root);
    assert(scan.hasBankRootRecordingPitch());
    assert(scan.bankRootNoteName == "C4");

    std::filesystem::remove_all(root);
}

void recursivelyScansMultipleOtoFiles()
{
    const auto root = makeTempVoicebankRoot();
    writeTextFile(root / "oto.ini",
                  "a.wav=a,0,100,200,30,5\n"
                  "ka.wav=ka,0,80,180,25,5\n");
    writeTextFile(root / "append" / "OTO.INI",
                  "shi.wav=shi,0,90,190,28,5\n");
    writeTextFile(root / "prefix.map",
                  "C4\t\t_A3\n"
                  "D4\tD4_\t_D4\n");

    const auto scan = VoicebankScanner::scan(root);

    assert(scan.foundOtoIni());
    assert(scan.foundPrefixMap());
    assert(scan.otoFiles.size() == 2);
    assert(scan.prefixMapFiles.size() == 1);
    assert(scan.entries.size() == 3);
    assert(scan.prefixMapEntries.size() == 2);
    assert(scan.aliasIndex.containsAlias("a"));
    assert(scan.aliasIndex.containsAlias("ka"));
    assert(scan.aliasIndex.containsAlias("shi"));
    assert(scan.aliasStyle() == VoicebankAliasStyle::Romaji);
    assert(scan.warnings.empty());

    const auto* entry = scan.aliasIndex.findFirst("shi");
    assert(entry != nullptr);
    assert(entry->sourceName == "append/OTO.INI");
    assert(entry->wavFile == "append/shi.wav");
    assert(scan.prefixMapEntries[0].noteName == "C4");
    assert(scan.prefixMapEntries[0].prefix.empty());
    assert(scan.prefixMapEntries[0].suffix == "_A3");

    std::filesystem::remove_all(root);
}

void canScanOnlyTopLevelWhenRequested()
{
    const auto root = makeTempVoicebankRoot();
    writeTextFile(root / "oto.ini", "a.wav=a,0,100,200,30,5\n");
    writeTextFile(root / "nested" / "oto.ini", "ka.wav=ka,0,80,180,25,5\n");
    writeTextFile(root / "nested" / "prefix.map", "C4\t\t_C4\n");

    VoicebankScanOptions options;
    options.recursive = false;
    const auto scan = VoicebankScanner::scan(root, options);

    assert(scan.otoFiles.size() == 1);
    assert(scan.aliasIndex.containsAlias("a"));
    assert(!scan.aliasIndex.containsAlias("ka"));
    assert(!scan.foundPrefixMap());

    std::filesystem::remove_all(root);
}

void parsesPrefixMapFormats()
{
    const auto entries = parsePrefixMapContent(
        "# comment\n"
        "C4\t\t_C4\n"
        "D4,D4_,_D4\n"
        "E4 E4_ _E4\n"
        "malformed\n");

    assert(entries.size() == 3);
    assert(entries[0].noteName == "C4");
    assert(entries[0].prefix.empty());
    assert(entries[0].suffix == "_C4");
    assert(entries[1].prefix == "D4_");
    assert(entries[1].suffix == "_D4");
    assert(entries[2].noteName == "E4");
}

void appliesPrefixMapToAliasCandidates()
{
    AliasEvent event;
    event.role = AliasRole::CvSyllable;
    event.sourcePhonemes = {"K", "AE"};
    event.candidates = {{"ka", "primary"}, {"ga", "fallback"}};

    const auto prefixMap = parsePrefixMapContent(
        "C4\tC4_\t_C4\n"
        "D4\tD4_\t_D4\n");

    const auto mapped = applyPrefixMapToAliasEvent(event, prefixMap, "C4");

    assert(mapped.role == AliasRole::CvSyllable);
    assert(mapped.sourcePhonemes == event.sourcePhonemes);
    assert(mapped.candidates.size() == 4);
    assert(mapped.candidates[0].alias == "C4_ka_C4");
    assert(mapped.candidates[0].reason == "prefixMap");
    assert(mapped.candidates[1].alias == "C4_ga_C4");
    assert(mapped.candidates[2].alias == "ka");
    assert(mapped.candidates[3].alias == "ga");
}

void keepsAliasesWhenPrefixMapNoteIsMissing()
{
    AliasEvent event;
    event.candidates = {{"ka", "primary"}};

    const auto prefixMap = parsePrefixMapContent("C4\tC4_\t_C4\n");
    const auto mapped = applyPrefixMapToAliasEvent(event, prefixMap, "G4");

    assert(mapped.candidates.size() == 1);
    assert(mapped.candidates[0].alias == "ka");
}

void preservesHighByteAliasesForLaterEncodingHandling()
{
    const auto root = makeTempVoicebankRoot();
    const std::string highByteAlias = "\x82\xa9"; // Shift-JIS bytes for a kana-like alias.
    writeTextFile(root / "oto.ini", "ka.wav=" + highByteAlias + ",0,100,200,30,5\n");

    const auto scan = VoicebankScanner::scan(root);

    assert(scan.entries.size() == 1);
    assert(scan.entries[0].alias == highByteAlias);
    assert(scan.aliasStyle() == VoicebankAliasStyle::NonAscii);

    std::filesystem::remove_all(root);
}

void reportsMissingVoicebankFolder()
{
    const auto root = makeTempVoicebankRoot();
    std::filesystem::remove_all(root);

    const auto scan = VoicebankScanner::scan(root);

    assert(!scan.foundOtoIni());
    assert(!scan.warnings.empty());
}

} // namespace

int main()
{
    recursivelyScansMultipleOtoFiles();
    infersBankRootPitchFromPrefixMapMedian();
    infersBankRootPitchFromAliasTokensWhenNoPrefixMap();
    canScanOnlyTopLevelWhenRequested();
    parsesPrefixMapFormats();
    appliesPrefixMapToAliasCandidates();
    keepsAliasesWhenPrefixMapNoteIsMissing();
    preservesHighByteAliasesForLaterEncodingHandling();
    reportsMissingVoicebankFolder();

    std::cout << "VoicebankScanner tests passed\n";
    return 0;
}
