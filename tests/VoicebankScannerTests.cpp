#include <Voice2VocalSynth/VoicebankScanner.h>

#include <cassert>
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

void recursivelyScansMultipleOtoFiles()
{
    const auto root = makeTempVoicebankRoot();
    writeTextFile(root / "oto.ini",
                  "a.wav=a,0,100,200,30,5\n"
                  "ka.wav=ka,0,80,180,25,5\n");
    writeTextFile(root / "append" / "OTO.INI",
                  "shi.wav=shi,0,90,190,28,5\n");

    const auto scan = VoicebankScanner::scan(root);

    assert(scan.foundOtoIni());
    assert(scan.otoFiles.size() == 2);
    assert(scan.entries.size() == 3);
    assert(scan.aliasIndex.containsAlias("a"));
    assert(scan.aliasIndex.containsAlias("ka"));
    assert(scan.aliasIndex.containsAlias("shi"));
    assert(scan.aliasStyle() == VoicebankAliasStyle::Romaji);
    assert(scan.warnings.empty());

    const auto* entry = scan.aliasIndex.findFirst("shi");
    assert(entry != nullptr);
    assert(entry->sourceName == "append/OTO.INI");
    assert(entry->wavFile == "append/shi.wav");

    std::filesystem::remove_all(root);
}

void canScanOnlyTopLevelWhenRequested()
{
    const auto root = makeTempVoicebankRoot();
    writeTextFile(root / "oto.ini", "a.wav=a,0,100,200,30,5\n");
    writeTextFile(root / "nested" / "oto.ini", "ka.wav=ka,0,80,180,25,5\n");

    VoicebankScanOptions options;
    options.recursive = false;
    const auto scan = VoicebankScanner::scan(root, options);

    assert(scan.otoFiles.size() == 1);
    assert(scan.aliasIndex.containsAlias("a"));
    assert(!scan.aliasIndex.containsAlias("ka"));

    std::filesystem::remove_all(root);
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
    canScanOnlyTopLevelWhenRequested();
    preservesHighByteAliasesForLaterEncodingHandling();
    reportsMissingVoicebankFolder();

    std::cout << "VoicebankScanner tests passed\n";
    return 0;
}
