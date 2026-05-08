#include "Voice2VocalSynth/VoicebankScanner.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
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

} // namespace

VoicebankAliasStyle VoicebankScanResult::aliasStyle() const noexcept
{
    return aliasIndex.aliasStyle();
}

bool VoicebankScanResult::foundOtoIni() const noexcept
{
    return !otoFiles.empty();
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

    if (options.recursive) {
        std::filesystem::recursive_directory_iterator iterator(result.rootPath,
                                                              std::filesystem::directory_options::skip_permission_denied,
                                                              statusError);
        const std::filesystem::recursive_directory_iterator end;
        while (!statusError && iterator != end) {
            if (iterator->is_regular_file(statusError) && isOtoIniFile(iterator->path(), options)) {
                scanOtoFile(iterator->path());
            }
            iterator.increment(statusError);
        }
    } else {
        std::filesystem::directory_iterator iterator(result.rootPath,
                                                    std::filesystem::directory_options::skip_permission_denied,
                                                    statusError);
        const std::filesystem::directory_iterator end;
        while (!statusError && iterator != end) {
            if (iterator->is_regular_file(statusError) && isOtoIniFile(iterator->path(), options)) {
                scanOtoFile(iterator->path());
            }
            iterator.increment(statusError);
        }
    }

    if (statusError) {
        result.warnings.push_back("Voicebank scan stopped early: " + statusError.message());
    }

    std::sort(result.otoFiles.begin(), result.otoFiles.end());
    if (result.otoFiles.empty()) {
        result.warnings.push_back("No oto.ini files were found in " + result.rootPath.string());
    }

    result.aliasIndex = buildVoicebankAliasIndex(result.entries);
    return result;
}

bool VoicebankScanner::isOtoIniFile(const std::filesystem::path& path,
                                    const VoicebankScanOptions& options)
{
    auto actual = path.filename().string();
    auto expected = options.otoFileName;
    if (options.caseInsensitiveOtoFileName) {
        actual = lowerAscii(std::move(actual));
        expected = lowerAscii(std::move(expected));
    }
    return actual == expected;
}

} // namespace Voice2VocalSynth
