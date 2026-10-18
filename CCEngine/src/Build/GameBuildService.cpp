#include "Build/GameBuildService.h"
#include "Build/AssetDependencyCollector.h"

#include "Core/AssetDatabase.h"
#include "Core/PathUtils.h"
#include "Core/ProjectSettings.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/ShaderCompiler.h"
#include "Runtime/RuntimeManifest.h"
#include "Scripting/ScriptCompiler.h"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef CC_PLATFORM_WINDOWS
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#endif

namespace CCEngine
{
    namespace
    {
#define CC_STRINGIZE_DETAIL(value) #value
#define CC_STRINGIZE(value) CC_STRINGIZE_DETAIL(value)
        constexpr const char* BuildMarkerName = ".ccengine-build";

        class IncrementalBuildCache;
        enum class ReuseResult
        {
            NotAvailable,
            Reused,
            Failed
        };
        ReuseResult TryReuseTrackedFile(
            IncrementalBuildCache* cache,
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            GameBuildReport& report,
            const std::string& stage);
        bool CopyFileTracked(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            GameBuildReport& report,
            const std::string& stage,
            bool required,
            IncrementalBuildCache* incremental);

        void AddMessage(
            GameBuildReport& report,
            GameBuildMessageSeverity severity,
            const std::string& stage,
            const std::string& message,
            const std::filesystem::path& path = {})
        {
            report.Messages.push_back({ severity, stage, message, path });
        }

        std::string Lowercase(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        bool IsInside(const std::filesystem::path& child, const std::filesystem::path& parent)
        {
            std::filesystem::path relative;
            return PathUtils::TryMakeRelativeInside(child, parent, relative, false);
        }

        bool IsOwnedBuildDirectory(const std::filesystem::path& directory)
        {
            std::error_code ec;
            return std::filesystem::is_regular_file(directory / BuildMarkerName, ec) && !ec;
        }

        bool RemoveOwnedBuildDirectory(const std::filesystem::path& directory, std::string& error)
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec) || ec)
                return true;
            if (!IsOwnedBuildDirectory(directory))
            {
                error = "Refusing to remove a directory without a CCEngine build marker: " + directory.string();
                return false;
            }

            std::filesystem::remove_all(directory, ec);
            if (ec)
            {
                error = "Failed to remove previous build directory: " + ec.message();
                return false;
            }
            return true;
        }

        bool WriteBuildMarker(const std::filesystem::path& directory, std::string& error)
        {
            std::ofstream marker(directory / BuildMarkerName, std::ios::trunc);
            if (!marker.is_open())
            {
                error = "Failed to create build ownership marker.";
                return false;
            }
            marker << "CCEngine Game Build\n";
            return true;
        }

        bool ShouldExcludeAsset(const std::filesystem::path& relativePath, bool excludeLicensedSamples)
        {
            const std::string generic = Lowercase(relativePath.generic_string());
            if (generic.empty())
                return false;

            for (const auto& part : relativePath)
            {
                const std::string loweredPart = Lowercase(part.string());
                if (loweredPart == ".git" || loweredPart == "local" || loweredPart.rfind("__qa_", 0) == 0)
                    return true;
            }

            const std::string extension = Lowercase(relativePath.extension().string());
            if (extension == ".cs" || extension == ".csproj" || extension == ".ps1" || extension == ".pdb")
                return true;

            if (excludeLicensedSamples && generic.rfind("chocolate rice/", 0) == 0)
                return true;

            return false;
        }

        std::filesystem::path FindVisualCppRuntimeDirectory()
        {
            if (const char* configured = std::getenv("VCToolsRedistDir"))
            {
                const std::filesystem::path candidate = std::filesystem::path(configured) / "x64" / "Microsoft.VC143.CRT";
                std::error_code ec;
                if (std::filesystem::is_directory(candidate, ec) && !ec)
                    return candidate;
            }

            const std::filesystem::path root = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC";
            std::error_code ec;
            std::vector<std::filesystem::path> candidates;
            for (std::filesystem::directory_iterator iterator(root, ec), end; !ec && iterator != end; iterator.increment(ec))
            {
                if (!iterator->is_directory(ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                const auto candidate = iterator->path() / "x64" / "Microsoft.VC143.CRT";
                if (std::filesystem::is_directory(candidate, ec) && !ec)
                    candidates.push_back(candidate);
                ec.clear();
            }
            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? std::filesystem::path{} : candidates.back();
        }

        std::vector<int> ParseVersion(const std::string& text)
        {
            std::vector<int> result;
            std::stringstream stream(text);
            std::string part;
            while (std::getline(stream, part, '.'))
            {
                try { result.push_back(std::stoi(part)); }
                catch (...) { return {}; }
            }
            return result;
        }

        std::filesystem::path FindCompatibleDotNetRuntime(
            const std::filesystem::path& runtimeRoot,
            const std::string& requestedVersion)
        {
            const auto requested = ParseVersion(requestedVersion);
            if (requested.size() < 2)
                return {};

            std::filesystem::path best;
            std::vector<int> bestVersion;
            std::error_code ec;
            for (std::filesystem::directory_iterator iterator(runtimeRoot, ec), end; !ec && iterator != end; iterator.increment(ec))
            {
                if (!iterator->is_directory(ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                const auto version = ParseVersion(iterator->path().filename().string());
                if (version.size() < 2 || version[0] != requested[0] || version[1] != requested[1])
                    continue;
                if (best.empty() || version > bestVersion)
                {
                    best = iterator->path();
                    bestVersion = version;
                }
            }
            return best;
        }

        bool StageVisualCppRuntime(
            const std::filesystem::path& staging,
            GameBuildReport& report,
            IncrementalBuildCache* incremental)
        {
            const auto runtimeDirectory = FindVisualCppRuntimeDirectory();
            if (runtimeDirectory.empty())
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Native Runtime", "Visual C++ x64 redistributable files were not found. Install the MSVC v143 redistributable component.");
                return false;
            }

            // Release 패키지는 대상 PC의 VC++ 설치 여부에 기대지 않는다.
            // Microsoft 재배포 폴더의 CRT DLL을 실행 파일 옆에 두어 app-local 방식으로 고정한다.
            const std::array<const char*, 10> files = {
                "concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
                "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "vccorlib140.dll",
                "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll"
            };
            bool success = true;
            for (const char* filename : files)
                success &= CopyFileTracked(runtimeDirectory / filename, staging / filename, report, "Native Runtime", true, incremental);
            return success;
        }

#ifdef CC_PLATFORM_WINDOWS
        bool ReadPeImports(
            const std::filesystem::path& binary,
            std::vector<std::string>& imports,
            std::string& error)
        {
            std::ifstream input(binary, std::ios::binary | std::ios::ate);
            if (!input.is_open())
            {
                error = "Could not open native binary.";
                return false;
            }
            const std::streamsize size = input.tellg();
            if (size < static_cast<std::streamsize>(sizeof(IMAGE_DOS_HEADER)))
            {
                error = "Native binary is too small.";
                return false;
            }
            std::vector<uint8_t> data(static_cast<size_t>(size));
            input.seekg(0, std::ios::beg);
            input.read(reinterpret_cast<char*>(data.data()), size);
            if (!input)
            {
                error = "Could not read native binary.";
                return false;
            }

            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data.data());
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
                static_cast<size_t>(dos->e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > data.size())
            {
                error = "Invalid PE header.";
                return false;
            }

            const uint8_t* ntBase = data.data() + dos->e_lfanew;
            if (*reinterpret_cast<const DWORD*>(ntBase) != IMAGE_NT_SIGNATURE)
            {
                error = "Invalid PE signature.";
                return false;
            }
            const auto* fileHeader = reinterpret_cast<const IMAGE_FILE_HEADER*>(ntBase + sizeof(DWORD));
            const uint8_t* optionalBase = reinterpret_cast<const uint8_t*>(fileHeader + 1);
            if (optionalBase + fileHeader->SizeOfOptionalHeader > data.data() + data.size())
            {
                error = "Invalid PE optional header.";
                return false;
            }

            DWORD importRva = 0;
            const WORD magic = *reinterpret_cast<const WORD*>(optionalBase);
            if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(optionalBase);
                importRva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            }
            else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(optionalBase);
                importRva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            }
            else
            {
                error = "Unsupported PE optional header.";
                return false;
            }
            if (importRva == 0)
                return true;

            const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(optionalBase + fileHeader->SizeOfOptionalHeader);
            if (reinterpret_cast<const uint8_t*>(sections + fileHeader->NumberOfSections) > data.data() + data.size())
            {
                error = "Invalid PE section table.";
                return false;
            }

            auto rvaToOffset = [&](DWORD rva, size_t& offset)
            {
                for (WORD index = 0; index < fileHeader->NumberOfSections; ++index)
                {
                    const auto& section = sections[index];
                    const DWORD span = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
                    if (rva < section.VirtualAddress || rva >= section.VirtualAddress + span)
                        continue;
                    offset = static_cast<size_t>(section.PointerToRawData) + (rva - section.VirtualAddress);
                    return offset < data.size();
                }
                return false;
            };

            size_t descriptorOffset = 0;
            if (!rvaToOffset(importRva, descriptorOffset))
            {
                error = "PE import table points outside file sections.";
                return false;
            }
            while (descriptorOffset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= data.size())
            {
                const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(data.data() + descriptorOffset);
                if (descriptor->Name == 0)
                    return true;
                size_t nameOffset = 0;
                if (!rvaToOffset(descriptor->Name, nameOffset))
                {
                    error = "PE import name points outside file sections.";
                    return false;
                }
                const char* name = reinterpret_cast<const char*>(data.data() + nameOffset);
                const size_t remaining = data.size() - nameOffset;
                const size_t length = strnlen_s(name, remaining);
                if (length == 0 || length == remaining)
                {
                    error = "PE import name is malformed.";
                    return false;
                }
                imports.emplace_back(name, length);
                descriptorOffset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
            }
            error = "PE import table is not terminated.";
            return false;
        }

        bool ValidateNativeDependencies(const std::filesystem::path& staging, GameBuildReport& report)
        {
            std::unordered_set<std::string> packaged;
            std::vector<std::filesystem::path> binaries;
            std::error_code ec;
            for (std::filesystem::directory_iterator iterator(staging, ec), end; !ec && iterator != end; iterator.increment(ec))
            {
                if (!iterator->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                const std::string extension = Lowercase(iterator->path().extension().string());
                if (extension == ".exe" || extension == ".dll")
                    binaries.push_back(iterator->path());
                packaged.insert(Lowercase(iterator->path().filename().string()));
            }
            if (ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Native Dependencies", "Could not enumerate staged native files: " + ec.message(), staging);
                return false;
            }

            wchar_t systemDirectory[MAX_PATH] = {};
            const UINT systemLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
            if (systemLength == 0 || systemLength >= MAX_PATH)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Native Dependencies", "Windows system directory could not be resolved.");
                return false;
            }

            bool success = true;
            for (const auto& binary : binaries)
            {
                std::vector<std::string> imports;
                std::string parseError;
                if (!ReadPeImports(binary, imports, parseError))
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Native Dependencies", parseError, binary);
                    success = false;
                    continue;
                }
                for (const std::string& imported : imports)
                {
                    const std::string key = Lowercase(imported);
                    if (packaged.contains(key) || key.rfind("api-ms-win-", 0) == 0 || key.rfind("ext-ms-win-", 0) == 0)
                        continue;
                    if (std::filesystem::is_regular_file(std::filesystem::path(systemDirectory) / imported, ec) && !ec)
                        continue;
                    ec.clear();
                    AddMessage(report, GameBuildMessageSeverity::Error, "Native Dependencies", "Required DLL is neither packaged nor provided by Windows: " + imported, binary);
                    success = false;
                }
            }
            if (success)
                AddMessage(report, GameBuildMessageSeverity::Info, "Native Dependencies", "Validated PE imports for " + std::to_string(binaries.size()) + " native binary file(s).");
            return success;
        }
#endif

        bool CopyFileTracked(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            GameBuildReport& report,
            const std::string& stage,
            bool required,
            IncrementalBuildCache* incremental = nullptr)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(source, ec) || ec)
            {
                if (required)
                    AddMessage(report, GameBuildMessageSeverity::Error, stage, "Required file is missing.", source);
                return !required;
            }

            const ReuseResult reuse = TryReuseTrackedFile(incremental, source, destination, report, stage);
            if (reuse == ReuseResult::Reused)
                return true;
            if (reuse == ReuseResult::Failed)
                return false;

            std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, stage, "Failed to create destination directory: " + ec.message(), destination.parent_path());
                return false;
            }

            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, stage, "Failed to copy file: " + ec.message(), source);
                return false;
            }

            ++report.FilesWritten;
            report.BytesWritten += std::filesystem::file_size(destination, ec);
            return true;
        }

        bool CopyDirectoryTracked(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            GameBuildReport& report,
            const std::string& stage,
            IncrementalBuildCache* incremental = nullptr)
        {
            std::error_code ec;
            if (!std::filesystem::is_directory(source, ec) || ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, stage, "Required runtime directory is missing.", source);
                return false;
            }

            for (std::filesystem::recursive_directory_iterator iterator(
                    source,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec), end;
                 !ec && iterator != end;
                 iterator.increment(ec))
            {
                if (!iterator->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                const auto relative = iterator->path().lexically_normal().lexically_relative(source.lexically_normal());
                if (relative.empty() || relative.is_absolute() ||
                    !CopyFileTracked(iterator->path(), destination / relative, report, stage, true, incremental))
                    return false;
            }
            if (ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, stage, "Failed while enumerating runtime files: " + ec.message(), source);
                return false;
            }
            return true;
        }

        std::string HashFile(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open())
                return {};

            uint64_t hash = 14695981039346656037ull;
            std::array<char, 16384> buffer{};
            while (input)
            {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                for (std::streamsize index = 0; index < count; ++index)
                {
                    hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(index)]);
                    hash *= 1099511628211ull;
                }
            }

            std::ostringstream output;
            output << std::hex << std::setfill('0') << std::setw(16) << hash;
            return output.str();
        }

        std::string HashText(const std::string& text)
        {
            uint64_t hash = 14695981039346656037ull;
            for (const unsigned char character : text)
            {
                hash ^= character;
                hash *= 1099511628211ull;
            }
            std::ostringstream output;
            output << std::hex << std::setfill('0') << std::setw(16) << hash;
            return output.str();
        }

        std::string HashScriptInputs(const std::filesystem::path& scriptsRoot)
        {
            std::vector<std::filesystem::path> inputs;
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator iterator(
                    scriptsRoot,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec), end;
                 !ec && iterator != end;
                 iterator.increment(ec))
            {
                const auto relative = iterator->path().lexically_normal().lexically_relative(scriptsRoot.lexically_normal());
                if (relative.empty() || relative.is_absolute())
                    continue;

                if (iterator->is_directory(ec))
                {
                    const std::string folder = Lowercase(relative.filename().string());
                    if (folder == "build" || folder == "bin" || folder == "obj")
                        iterator.disable_recursion_pending();
                    ec.clear();
                    continue;
                }
                if (!iterator->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }

                const std::string extension = Lowercase(iterator->path().extension().string());
                if (extension == ".cs" || extension == ".csproj" || extension == ".json" || extension == ".props" || extension == ".targets" || extension == ".ps1")
                    inputs.push_back(relative.lexically_normal());
            }
            if (ec)
                return {};

            std::sort(inputs.begin(), inputs.end(), [](const auto& left, const auto& right)
            {
                return left.generic_string() < right.generic_string();
            });

            std::string combined;
            for (const auto& relative : inputs)
            {
                const std::string fileHash = HashFile(scriptsRoot / relative);
                if (fileHash.empty())
                    return {};
                combined += Lowercase(relative.generic_string());
                combined += ':';
                combined += fileHash;
                combined += '\n';
            }
            return HashText(combined);
        }

        class IncrementalBuildCache
        {
        public:
            IncrementalBuildCache(std::filesystem::path previousRoot, std::filesystem::path stagingRoot)
                : m_PreviousRoot(std::move(previousRoot)), m_StagingRoot(std::move(stagingRoot))
            {
            }

            void Load(GameBuildReport& report)
            {
                if (!IsOwnedBuildDirectory(m_PreviousRoot))
                    return;

                std::ifstream input(m_PreviousRoot / "BuildManifest.json");
                nlohmann::json manifest;
                try
                {
                    input >> manifest;
                    if (!manifest.contains("Files") || !manifest["Files"].is_array())
                        throw std::runtime_error("file list is missing");

                    for (const auto& file : manifest["Files"])
                    {
                        const std::filesystem::path relative = file.value("Path", "");
                        const std::string hash = file.value("Hash", "");
                        if (!IsSafeRelative(relative) || hash.empty())
                            continue;
                        CacheEntry entry;
                        entry.RelativePath = relative.lexically_normal();
                        entry.Hash = hash;
                        entry.Size = file.value("Size", 0ull);
                        m_Files.emplace(PathKey(relative), std::move(entry));
                    }
                    m_Enabled = !m_Files.empty();
                }
                catch (const std::exception& exception)
                {
                    AddMessage(
                        report,
                        GameBuildMessageSeverity::Warning,
                        "Incremental Build",
                        "Previous BuildManifest is invalid; performing a clean build: " + std::string(exception.what()),
                        m_PreviousRoot / "BuildManifest.json");
                    m_Files.clear();
                    m_Enabled = false;
                }
            }

            bool IsEnabled() const { return m_Enabled; }

            bool CanReuse(const std::filesystem::path& relative, const std::string& expectedHash)
            {
                if (!m_Enabled || !IsSafeRelative(relative) || expectedHash.empty())
                    return false;

                const std::string key = PathKey(relative);
                const auto found = m_Files.find(key);
                if (found == m_Files.end() || found->second.Hash != expectedHash)
                    return false;

                if (m_Verified.find(key) != m_Verified.end())
                    return true;

                const std::filesystem::path previousFile = m_PreviousRoot / relative;
                std::error_code ec;
                if (!std::filesystem::is_regular_file(previousFile, ec) || ec ||
                    std::filesystem::file_size(previousFile, ec) != found->second.Size || ec ||
                    HashFile(previousFile) != expectedHash)
                    return false;

                m_Verified.insert(key);
                return true;
            }

            bool CanReuseManifestFile(const std::filesystem::path& relative)
            {
                const auto found = m_Files.find(PathKey(relative));
                return found != m_Files.end() && CanReuse(relative, found->second.Hash);
            }

            bool ReuseManifestFile(const std::filesystem::path& relative, GameBuildReport& report, const std::string& stage)
            {
                const auto found = m_Files.find(PathKey(relative));
                return found != m_Files.end() && Reuse(relative, found->second.Hash, report, stage);
            }

            std::vector<std::filesystem::path> FilesUnder(const std::filesystem::path& prefix) const
            {
                const std::string normalizedPrefix = PathKey(prefix) + "/";
                std::vector<std::filesystem::path> result;
                for (const auto& [key, entry] : m_Files)
                {
                    if (key.rfind(normalizedPrefix, 0) == 0)
                        result.push_back(entry.RelativePath);
                }
                std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
                {
                    return left.generic_string() < right.generic_string();
                });
                return result;
            }

            bool Reuse(
                const std::filesystem::path& relative,
                const std::string& expectedHash,
                GameBuildReport& report,
                const std::string& stage)
            {
                if (!CanReuse(relative, expectedHash))
                    return false;

                const std::filesystem::path previousFile = m_PreviousRoot / relative;
                const std::filesystem::path destination = m_StagingRoot / relative;
                std::error_code ec;
                std::filesystem::create_directories(destination.parent_path(), ec);
                if (ec)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, stage, "Failed to create incremental destination: " + ec.message(), destination.parent_path());
                    return false;
                }

                // 이전 성공 빌드와 새 Staging은 같은 드라이브에 있으므로 하드링크를 우선 사용한다.
                // 지원하지 않는 파일시스템에서는 일반 복사로 내려가며 결과물의 의미는 동일하다.
                std::filesystem::create_hard_link(previousFile, destination, ec);
                if (ec)
                {
                    ec.clear();
                    std::filesystem::copy_file(previousFile, destination, std::filesystem::copy_options::overwrite_existing, ec);
                }
                if (ec)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, stage, "Failed to reuse unchanged output: " + ec.message(), previousFile);
                    return false;
                }

                ++report.FilesReused;
                const uint64_t size = std::filesystem::file_size(destination, ec);
                if (!ec)
                    report.BytesReused += size;
                return true;
            }

            ReuseResult TryReuseSource(
                const std::filesystem::path& source,
                const std::filesystem::path& destination,
                GameBuildReport& report,
                const std::string& stage)
            {
                std::error_code ec;
                const std::filesystem::path relative = destination.lexically_normal().lexically_relative(m_StagingRoot.lexically_normal());
                if (!IsSafeRelative(relative))
                    return ReuseResult::NotAvailable;

                const std::string sourceHash = HashFile(source);
                if (!CanReuse(relative, sourceHash))
                    return ReuseResult::NotAvailable;
                return Reuse(relative, sourceHash, report, stage) ? ReuseResult::Reused : ReuseResult::Failed;
            }

            std::filesystem::path PreviousPath(const std::filesystem::path& relative) const
            {
                return m_PreviousRoot / relative;
            }

        private:
            struct CacheEntry
            {
                std::filesystem::path RelativePath;
                std::string Hash;
                uint64_t Size = 0;
            };

            static bool IsSafeRelative(const std::filesystem::path& path)
            {
                if (path.empty() || path.is_absolute())
                    return false;
                for (const auto& part : path.lexically_normal())
                {
                    if (part == "..")
                        return false;
                }
                return true;
            }

            static std::string PathKey(const std::filesystem::path& path)
            {
                return Lowercase(path.lexically_normal().generic_string());
            }

            std::filesystem::path m_PreviousRoot;
            std::filesystem::path m_StagingRoot;
            std::unordered_map<std::string, CacheEntry> m_Files;
            std::unordered_set<std::string> m_Verified;
            bool m_Enabled = false;
        };

        ReuseResult TryReuseTrackedFile(
            IncrementalBuildCache* cache,
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            GameBuildReport& report,
            const std::string& stage)
        {
            if (!cache || !cache->IsEnabled())
                return ReuseResult::NotAvailable;
            return cache->TryReuseSource(source, destination, report, stage);
        }

        bool WriteFileManifest(const std::filesystem::path& staging, const GameBuildRequest& request, GameBuildReport& report)
        {
            nlohmann::json manifest;
            manifest["Version"] = 3;
            manifest["ProductName"] = request.ProductName;
            manifest["ProductVersion"] = request.ProductVersion;
            manifest["UpdateChannel"] = request.UpdateChannel;
            manifest["Platform"] = "Windows-x64-D3D11";
            manifest["PackageType"] = "LooseFiles";
            manifest["DevelopmentBuild"] = request.DevelopmentBuild;
            manifest["Files"] = nlohmann::json::array();

            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (std::filesystem::recursive_directory_iterator iterator(
                    staging,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec), end;
                 !ec && iterator != end;
                 iterator.increment(ec))
            {
                if (!iterator->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }

                const auto relative = iterator->path().lexically_normal().lexically_relative(staging.lexically_normal());
                if (relative.empty() || relative.is_absolute() || relative == BuildMarkerName || relative == "BuildManifest.json")
                {
                    ec.clear();
                    continue;
                }

                files.push_back(relative.lexically_normal());
            }

            if (ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Manifest", "Failed while enumerating staged files: " + ec.message(), staging);
                return false;
            }

            // 파일시스템 열거 순서는 OS와 실행 시점에 따라 달라질 수 있다.
            // Manifest 순서를 고정해야 같은 입력으로 만든 빌드의 해시와 diff가 안정적이다.
            std::sort(files.begin(), files.end(), [](const auto& left, const auto& right)
            {
                return left.generic_string() < right.generic_string();
            });

            uint64_t totalBytes = 0;
            std::string fingerprintInput = request.ProductName + "\n" + request.ProductVersion + "\n" +
                request.UpdateChannel + "\n" + (request.DevelopmentBuild ? "Development\n" : "Release\n");
            for (const auto& relative : files)
            {
                const std::filesystem::path stagedFile = staging / relative;
                nlohmann::json file;
                file["Path"] = relative.generic_string();
                uint64_t size = std::filesystem::file_size(stagedFile, ec);
                if (ec)
                {
                    ec.clear();
                    size = 0;
                }
                file["Size"] = size;
                const std::string hash = HashFile(stagedFile);
                file["Hash"] = hash;
                manifest["Files"].push_back(std::move(file));
                totalBytes += size;
                fingerprintInput += relative.generic_string() + "\n" + std::to_string(size) + "\n" + hash + "\n";
            }
            manifest["FileCount"] = files.size();
            manifest["TotalBytes"] = totalBytes;
            manifest["BuildFingerprint"] = HashText(fingerprintInput);

            std::ofstream output(staging / "BuildManifest.json", std::ios::trunc);
            if (!output.is_open())
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Manifest", "Failed to write BuildManifest.json.", staging);
                return false;
            }
            output << manifest.dump(4);
            ++report.FilesWritten;
            return true;
        }

        bool WriteRuntimeAssetManifest(
            const std::filesystem::path& staging,
            const std::vector<AssetDependency>& dependencies,
            GameBuildReport& report)
        {
            nlohmann::json manifest;
            manifest["Version"] = 2;
            manifest["Assets"] = nlohmann::json::array();

            std::error_code ec;
            for (const AssetDependency& dependency : dependencies)
            {
                const auto stagedPath = staging / "assets" / dependency.RelativePath;
                const bool sourcePackaged = std::filesystem::is_regular_file(stagedPath, ec) && !ec;
                if (!sourcePackaged && dependency.Kind != AssetKind::Shader)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Manifest", "Staged asset is missing from the runtime manifest.", stagedPath);
                    ec.clear();
                    return false;
                }

                nlohmann::json asset;
                asset["Guid"] = dependency.Guid;
                asset["Type"] = AssetDatabase::AssetKindToString(dependency.Kind);
                asset["Path"] = (std::filesystem::path("assets") / dependency.RelativePath).generic_string();
                asset["Addresses"] = nlohmann::json::array({
                    dependency.RelativePath.generic_string(),
                    (std::filesystem::path("assets") / dependency.RelativePath).generic_string()
                });
                asset["PackagedSource"] = sourcePackaged;
                asset["Size"] = sourcePackaged ? std::filesystem::file_size(stagedPath, ec) : 0;
                if (sourcePackaged && ec)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Manifest", "Failed to read staged asset size: " + ec.message(), stagedPath);
                    return false;
                }
                asset["Hash"] = sourcePackaged ? HashFile(stagedPath) : "";
                manifest["Assets"].push_back(std::move(asset));
            }

            const auto manifestPath = staging / "RuntimeAssetManifest.json";
            std::ofstream output(manifestPath, std::ios::trunc);
            if (!output.is_open())
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Manifest", "Failed to write RuntimeAssetManifest.json.", manifestPath);
                return false;
            }
            output << manifest.dump(4);
            ++report.FilesWritten;
            return true;
        }

        bool CompileAndStageShaders(
            const std::filesystem::path& staging,
            const std::vector<AssetDependency>& dependencies,
            GameBuildReport& report,
            IncrementalBuildCache* incremental,
            bool allowShaderFallback)
        {
            constexpr const char* CompilerKey = "Windows-x64-D3D11-SM5-v2-variants";
            nlohmann::json manifest;
            manifest["Version"] = 3;
            manifest["Platform"] = "Windows-x64-D3D11";
            manifest["CompilerKey"] = CompilerKey;
            manifest["Shaders"] = nlohmann::json::array();

            std::unordered_map<std::string, std::filesystem::path> dependencyByGuid;
            std::unordered_map<std::string, std::filesystem::path> dependencyByRuntimePath;
            for (const AssetDependency& dependency : dependencies)
            {
                if (!dependency.Guid.empty())
                    dependencyByGuid[dependency.Guid] = dependency.SourcePath;
                dependencyByRuntimePath[Lowercase((std::filesystem::path("assets") / dependency.RelativePath).generic_string())] = dependency.SourcePath;
            }

            std::unordered_map<std::string, std::vector<std::vector<std::string>>> materialVariants;
            for (const AssetDependency& dependency : dependencies)
            {
                if (dependency.Kind != AssetKind::Material)
                    continue;
                MaterialAsset material;
                if (!material.LoadFromFile(dependency.SourcePath))
                    continue;

                std::filesystem::path shaderPath;
                if (!material.ShaderGuid.empty())
                {
                    const auto shader = dependencyByGuid.find(material.ShaderGuid);
                    if (shader != dependencyByGuid.end())
                        shaderPath = shader->second;
                }
                if (shaderPath.empty())
                {
                    const auto shader = dependencyByRuntimePath.find(Lowercase(std::filesystem::path(material.ShaderPath).lexically_normal().generic_string()));
                    if (shader != dependencyByRuntimePath.end())
                        shaderPath = shader->second;
                }
                if (shaderPath.empty())
                    continue;

                std::vector<std::string> keywords;
                for (const auto& [name, value] : material.ShaderProperties)
                {
                    if (value.Type == ShaderPropertyType::Toggle && value.BoolValue)
                        keywords.push_back(name);
                }
                keywords = ShaderCompiler::NormalizeKeywords(keywords);
                auto& variants = materialVariants[Lowercase(shaderPath.lexically_normal().generic_string())];
                if (std::find(variants.begin(), variants.end(), keywords) == variants.end())
                    variants.push_back(std::move(keywords));
            }

            std::unordered_map<std::string, nlohmann::json> previousShaders;
            if (incremental && incremental->IsEnabled())
            {
                std::ifstream previousInput(incremental->PreviousPath("ShaderBuildManifest.json"));
                try
                {
                    nlohmann::json previousManifest;
                    previousInput >> previousManifest;
                    if (previousManifest.value("CompilerKey", "") == CompilerKey)
                    {
                        for (const auto& shader : previousManifest.value("Shaders", nlohmann::json::array()))
                        {
                            const std::string sourcePath = shader.value("SourcePath", "");
                            std::vector<std::string> keywords;
                            for (const auto& keyword : shader.value("Keywords", nlohmann::json::array()))
                            {
                                if (keyword.is_string())
                                    keywords.push_back(keyword.get<std::string>());
                            }
                            if (!sourcePath.empty())
                                previousShaders[Lowercase(sourcePath) + "|" + ShaderCompiler::MakeVariantKey(keywords)] = shader;
                        }
                    }
                }
                catch (...)
                {
                    previousShaders.clear();
                }
            }

            for (const AssetDependency& dependency : dependencies)
            {
                if (dependency.Kind != AssetKind::Shader || !ShaderCompiler::IsHlslSource(dependency.SourcePath))
                    continue;

                const std::string sourcePath = (std::filesystem::path("assets") / dependency.RelativePath).generic_string();
                const std::string sourceHash = HashFile(dependency.SourcePath);
                std::vector<std::vector<std::string>> variants = { {} };
                const auto usedVariants = materialVariants.find(Lowercase(dependency.SourcePath.lexically_normal().generic_string()));
                if (usedVariants != materialVariants.end())
                {
                    for (const auto& keywords : usedVariants->second)
                    {
                        if (std::find(variants.begin(), variants.end(), keywords) == variants.end())
                            variants.push_back(keywords);
                    }
                }

                for (const auto& keywords : variants)
                {
                    const std::string variantKey = ShaderCompiler::MakeVariantKey(keywords);
                    const std::string identifier = dependency.Guid.empty()
                        ? HashText(dependency.RelativePath.generic_string())
                        : dependency.Guid;
                    const auto relativeDirectory = std::filesystem::path("ShaderCache") / identifier / HashText(variantKey);
                    const auto vertexRelative = relativeDirectory / "VSMain_vs_5_0.cso";
                    const auto pixelRelative = relativeDirectory / "PSMain_ps_5_0.cso";
                    std::string vertexHash;
                    std::string pixelHash;
                    bool reused = false;
                    const auto previous = previousShaders.find(Lowercase(sourcePath) + "|" + variantKey);
                    if (previous != previousShaders.end() && previous->second.value("SourceHash", "") == sourceHash)
                    {
                        vertexHash = previous->second.value("VertexHash", "");
                        pixelHash = previous->second.value("PixelHash", "");
                        if (incremental->CanReuse(vertexRelative, vertexHash) && incremental->CanReuse(pixelRelative, pixelHash))
                        {
                            if (!incremental->Reuse(vertexRelative, vertexHash, report, "Compile Shaders") ||
                                !incremental->Reuse(pixelRelative, pixelHash, report, "Compile Shaders"))
                                return false;
                            reused = true;
                        }
                    }

                    if (!reused)
                    {
                        ShaderCompileResult compiled = ShaderCompiler::CompileHlslFile(dependency.SourcePath, true, keywords);
                        if (!compiled.Success && allowShaderFallback)
                        {
                            const auto fallback = std::find_if(dependencies.begin(), dependencies.end(), [](const AssetDependency& candidate)
                            {
                                return candidate.Kind == AssetKind::Shader && Lowercase(candidate.SourcePath.filename().string()) == "errorshader.hlsl";
                            });
                            if (fallback != dependencies.end())
                            {
                                AddMessage(report, GameBuildMessageSeverity::Warning, "Compile Shaders",
                                    "Shader failed in Development build. Error Shader bytecode was staged for this variant.\n" + compiled.Summary,
                                    dependency.SourcePath);
                                compiled = ShaderCompiler::CompileHlslFile(fallback->SourcePath, true, keywords);
                            }
                        }
                        if (!compiled.Success)
                        {
                            AddMessage(report, GameBuildMessageSeverity::Error, "Compile Shaders", compiled.Summary, dependency.SourcePath);
                            return false;
                        }
                        if (!CopyFileTracked(compiled.Vertex.BytecodePath, staging / vertexRelative, report, "Compile Shaders", true, incremental) ||
                            !CopyFileTracked(compiled.Pixel.BytecodePath, staging / pixelRelative, report, "Compile Shaders", true, incremental))
                            return false;
                        vertexHash = HashFile(staging / vertexRelative);
                        pixelHash = HashFile(staging / pixelRelative);
                    }

                    nlohmann::json shader;
                    shader["Guid"] = dependency.Guid;
                    shader["SourcePath"] = sourcePath;
                    shader["SourceHash"] = sourceHash;
                    shader["Keywords"] = keywords;
                    shader["VariantKey"] = variantKey;
                    shader["VertexBytecode"] = vertexRelative.generic_string();
                    shader["PixelBytecode"] = pixelRelative.generic_string();
                    shader["VertexHash"] = vertexHash;
                    shader["PixelHash"] = pixelHash;
                    manifest["Shaders"].push_back(std::move(shader));
                }
            }

            const auto manifestPath = staging / "ShaderBuildManifest.json";
            std::ofstream output(manifestPath, std::ios::trunc);
            if (!output.is_open())
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Compile Shaders", "Failed to write ShaderBuildManifest.json.", manifestPath);
                return false;
            }
            output << manifest.dump(4);
            ++report.FilesWritten;
            return true;
        }

        bool CompileAndStageManagedRuntime(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& assetsRoot,
            const std::filesystem::path& staging,
            ManagedRuntimeDeployment deployment,
            GameBuildReport& report,
            IncrementalBuildCache* incremental)
        {
            const std::filesystem::path scriptsRoot = assetsRoot / "Scripts";
            const std::string scriptInputHash = HashScriptInputs(scriptsRoot);
            const std::string deploymentName = deployment == ManagedRuntimeDeployment::SelfContained
                ? "SelfContained" : "FrameworkDependent";

            if (incremental && incremental->IsEnabled() && !scriptInputHash.empty())
            {
                nlohmann::json previousRuntime;
                std::ifstream previousInput(incremental->PreviousPath("ManagedRuntime.json"));
                try
                {
                    previousInput >> previousRuntime;
                }
                catch (...)
                {
                    previousRuntime = nlohmann::json{};
                }

                if (previousRuntime.value("InputHash", "") == scriptInputHash &&
                    previousRuntime.value("Deployment", "") == deploymentName)
                {
                    std::vector<std::filesystem::path> reusable = {
                        "assets/Scripts/Build/CCEngine.ScriptCore.dll",
                        "assets/Scripts/Build/CCEngine.ScriptCore.runtimeconfig.json",
                        "assets/Scripts/Build/GameScripts.dll",
                        "assets/Scripts/Build/GameScripts.manifest.json",
                        "ManagedRuntime.json"
                    };
                    if (deployment == ManagedRuntimeDeployment::SelfContained)
                    {
                        auto runtimeFiles = incremental->FilesUnder("Runtime/dotnet");
                        if (runtimeFiles.empty())
                            reusable.clear();
                        else
                            reusable.insert(reusable.end(), runtimeFiles.begin(), runtimeFiles.end());
                    }

                    const bool allValid = !reusable.empty() && std::all_of(reusable.begin(), reusable.end(), [&](const auto& relative)
                    {
                        return incremental->CanReuseManifestFile(relative);
                    });
                    if (allValid)
                    {
                        for (const auto& relative : reusable)
                        {
                            if (!incremental->ReuseManifestFile(relative, report, "Compile Scripts"))
                                return false;
                        }
                        AddMessage(report, GameBuildMessageSeverity::Info, "Compile Scripts", "Reused unchanged managed build outputs.");
                        return true;
                    }
                }
            }

            const ScriptBuildResult scriptBuild = ScriptCompiler::CompileForBuild(projectRoot);
            if (!scriptBuild.Success)
            {
                std::string message = "C# script compilation failed with exit code " + std::to_string(scriptBuild.ExitCode) + ".";
                if (!scriptBuild.Output.empty())
                    message += "\n" + scriptBuild.Output;
                AddMessage(report, GameBuildMessageSeverity::Error, "Compile Scripts", message, assetsRoot / "Scripts");
                return false;
            }

            const auto buildRoot = assetsRoot / "Scripts" / "Build";
            const std::array<const char*, 4> managedFiles = {{
                "CCEngine.ScriptCore.dll",
                "CCEngine.ScriptCore.runtimeconfig.json",
                "GameScripts.dll",
                "GameScripts.manifest.json"
            }};
            for (const char* filename : managedFiles)
            {
                if (!CopyFileTracked(
                        buildRoot / filename,
                        staging / "assets" / "Scripts" / "Build" / filename,
                        report,
                        "Stage Managed Runtime",
                        true,
                        incremental))
                    return false;
            }

            std::string runtimeVersion;
            nlohmann::json runtimeConfig;
            {
                std::ifstream input(buildRoot / "CCEngine.ScriptCore.runtimeconfig.json");
                try
                {
                    input >> runtimeConfig;
                    runtimeVersion = runtimeConfig.at("runtimeOptions").at("framework").value("version", "");
                }
                catch (...)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Compile Scripts", "ScriptCore runtimeconfig has no valid framework version.", buildRoot);
                    return false;
                }
            }

            nlohmann::json runtime;
            runtime["Version"] = 1;
            runtime["Deployment"] = deploymentName;
            runtime["Framework"] = "Microsoft.NETCore.App";
            runtime["RuntimeVersion"] = runtimeVersion;
            runtime["Architecture"] = "x64";
            runtime["RequiresInstalledRuntime"] = deployment != ManagedRuntimeDeployment::SelfContained;
            runtime["InputHash"] = scriptInputHash;

            if (deployment == ManagedRuntimeDeployment::SelfContained)
            {
                std::filesystem::path dotnetRoot;
                if (const char* environmentRoot = std::getenv("DOTNET_ROOT"))
                    dotnetRoot = environmentRoot;
                if (dotnetRoot.empty())
                    dotnetRoot = "C:/Program Files/dotnet";

                const auto sharedRuntimeRoot = dotnetRoot / "shared" / "Microsoft.NETCore.App";
                const auto sharedRuntime = FindCompatibleDotNetRuntime(sharedRuntimeRoot, runtimeVersion);
                const auto fxrRoot = dotnetRoot / "host" / "fxr";
                std::filesystem::path fxrVersion;
                std::error_code ec;
                for (std::filesystem::directory_iterator iterator(fxrRoot, ec), end; !ec && iterator != end; iterator.increment(ec))
                {
                    if (iterator->is_directory(ec) && (fxrVersion.empty() || iterator->path().filename().string() > fxrVersion.filename().string()))
                        fxrVersion = iterator->path();
                    ec.clear();
                }
                if (runtimeVersion.empty() || sharedRuntime.empty() || fxrVersion.empty() ||
                    !CopyDirectoryTracked(sharedRuntime, staging / "Runtime" / "dotnet" / "shared" / "Microsoft.NETCore.App" / sharedRuntime.filename(), report, "Stage .NET Runtime", incremental) ||
                    !CopyDirectoryTracked(fxrVersion, staging / "Runtime" / "dotnet" / "host" / "fxr" / fxrVersion.filename(), report, "Stage .NET Runtime", incremental))
                    return false;

                runtime["RuntimeVersion"] = sharedRuntime.filename().string();
                runtime["HostFxrVersion"] = fxrVersion.filename().string();
            }
            const auto runtimePath = staging / "ManagedRuntime.json";
            std::ofstream output(runtimePath, std::ios::trunc);
            if (!output.is_open())
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Compile Scripts", "Failed to write ManagedRuntime.json.", runtimePath);
                return false;
            }
            output << runtime.dump(4);
            ++report.FilesWritten;
            return true;
        }

        bool ValidatePortableManagedRuntime(
            const std::filesystem::path& staging,
            ManagedRuntimeDeployment deployment,
            GameBuildReport& report)
        {
            nlohmann::json runtime;
            std::ifstream input(staging / "ManagedRuntime.json");
            try { input >> runtime; }
            catch (...)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Portable Runtime", "ManagedRuntime.json could not be parsed.", staging / "ManagedRuntime.json");
                return false;
            }

            if (deployment != ManagedRuntimeDeployment::SelfContained)
            {
                AddMessage(report, GameBuildMessageSeverity::Warning, "Portable Runtime", "Framework-dependent build requires .NET 8 x64 on the target PC.");
                return true;
            }

            const std::string runtimeVersion = runtime.value("RuntimeVersion", "");
            const std::string hostFxrVersion = runtime.value("HostFxrVersion", "");
            const std::array<std::filesystem::path, 3> required = {
                std::filesystem::path("Runtime/dotnet/host/fxr") / hostFxrVersion / "hostfxr.dll",
                std::filesystem::path("Runtime/dotnet/shared/Microsoft.NETCore.App") / runtimeVersion / "coreclr.dll",
                std::filesystem::path("Runtime/dotnet/shared/Microsoft.NETCore.App") / runtimeVersion / "hostpolicy.dll"
            };
            if (runtimeVersion.empty() || hostFxrVersion.empty() || runtime.value("RequiresInstalledRuntime", true))
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Portable Runtime", "Self-contained runtime metadata is incomplete.", staging / "ManagedRuntime.json");
                return false;
            }
            for (const auto& relative : required)
            {
                std::error_code ec;
                if (!std::filesystem::is_regular_file(staging / relative, ec) || ec)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Portable Runtime", "Required self-contained .NET file is missing.", staging / relative);
                    return false;
                }
            }
            AddMessage(report, GameBuildMessageSeverity::Info, "Portable Runtime", "Validated app-local .NET " + runtimeVersion + " without an installed runtime dependency.");
            return true;
        }

        void CollectScriptClasses(const nlohmann::json& value, const std::filesystem::path& source, GameBuildReport& report, const std::unordered_set<std::string>& classes)
        {
            if (value.is_object())
            {
                if (value.contains("ScriptComponent") && value["ScriptComponent"].is_object())
                {
                    const std::string className = value["ScriptComponent"].value("ClassName", "");
                    if (!className.empty() && classes.find(className) == classes.end())
                        AddMessage(report, GameBuildMessageSeverity::Error, "Validate Scripts", "Missing script class: " + className, source);
                }
                for (const auto& [key, child] : value.items())
                    CollectScriptClasses(child, source, report, classes);
            }
            else if (value.is_array())
            {
                for (const auto& child : value)
                    CollectScriptClasses(child, source, report, classes);
            }
        }

        bool ValidateScriptReferences(
            const std::filesystem::path& staging,
            const std::vector<AssetDependency>& dependencies,
            GameBuildReport& report)
        {
            nlohmann::json manifest;
            std::ifstream manifestInput(staging / "assets" / "Scripts" / "Build" / "GameScripts.manifest.json");
            try
            {
                manifestInput >> manifest;
            }
            catch (...)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Validate Scripts", "GameScripts.manifest.json could not be parsed.");
                return false;
            }

            std::unordered_set<std::string> classes;
            for (const auto& entry : manifest.value("Classes", nlohmann::json::array()))
            {
                const std::string className = entry.value("ClassName", "");
                if (!className.empty())
                    classes.insert(className);
            }

            const size_t issueStart = report.Messages.size();
            for (const AssetDependency& dependency : dependencies)
            {
                if (dependency.Kind != AssetKind::Scene && dependency.Kind != AssetKind::Prefab)
                    continue;
                nlohmann::json document;
                std::ifstream input(dependency.SourcePath);
                try
                {
                    input >> document;
                    CollectScriptClasses(document, dependency.SourcePath, report, classes);
                }
                catch (...)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Validate Scripts", "Scene or Prefab could not be parsed for script validation.", dependency.SourcePath);
                }
            }
            for (size_t index = issueStart; index < report.Messages.size(); ++index)
            {
                if (report.Messages[index].Severity == GameBuildMessageSeverity::Error)
                    return false;
            }
            return true;
        }

        uint64_t EstimateDirectoryBytes(const std::filesystem::path& root)
        {
            std::error_code ec;
            uint64_t bytes = 0;
            if (!std::filesystem::is_directory(root, ec) || ec)
                return 0;
            for (std::filesystem::recursive_directory_iterator iterator(
                    root, std::filesystem::directory_options::skip_permission_denied, ec), end;
                 !ec && iterator != end; iterator.increment(ec))
            {
                if (!iterator->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                bytes += iterator->file_size(ec);
                if (ec)
                    ec.clear();
            }
            return bytes;
        }

        bool ValidateOutputCapacityAndLock(
            const std::filesystem::path& outputDirectory,
            const std::string& executableName,
            const std::filesystem::path& binaryDirectory,
            const std::filesystem::path& assetsRoot,
            const std::optional<uint64_t>& availableBytesOverride,
            GameBuildReport& report)
        {
            std::filesystem::path existingParent = outputDirectory.parent_path();
            std::error_code ec;
            while (!existingParent.empty() && !std::filesystem::exists(existingParent, ec))
                existingParent = existingParent.parent_path();
            const auto space = std::filesystem::space(existingParent, ec);
            const uint64_t sourceBytes = EstimateDirectoryBytes(binaryDirectory) + EstimateDirectoryBytes(assetsRoot);
            const uint64_t previousBytes = EstimateDirectoryBytes(outputDirectory);
            // 원자 교체 중에는 이전 출력과 staging이 동시에 존재한다. 입력 예상치에 25% 여유를
            // 더해 디스크 부족이 Finalize에서 뒤늦게 발생하지 않게 한다.
            const uint64_t estimatedBuildBytes = (std::max)(512ull * 1024ull * 1024ull, sourceBytes + sourceBytes / 4ull);
            const uint64_t requiredFreeBytes = estimatedBuildBytes + previousBytes;
            const uint64_t availableBytes = availableBytesOverride.value_or(space.available);
            if ((!availableBytesOverride && ec) || availableBytes < requiredFreeBytes)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Validate", (!availableBytesOverride && ec)
                    ? "Could not query output disk space."
                    : "Insufficient disk space. Estimated staging requirement=" + std::to_string(estimatedBuildBytes) +
                        " bytes, previous build=" + std::to_string(previousBytes) + " bytes, available=" +
                        std::to_string(availableBytes) + " bytes.", existingParent);
                return false;
            }
            AddMessage(report, GameBuildMessageSeverity::Info, "Validate",
                "Disk capacity gate passed. Estimated staging=" + std::to_string(estimatedBuildBytes) +
                " bytes, available=" + std::to_string(availableBytes) + " bytes.", existingParent);

#ifdef CC_PLATFORM_WINDOWS
            std::filesystem::path executable = outputDirectory / executableName;
            if (std::filesystem::is_regular_file(executable, ec) && !ec)
            {
                bool running = false;
                DWORD runningProcessId = 0;
                HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                if (snapshot != INVALID_HANDLE_VALUE)
                {
                    PROCESSENTRY32W entry{};
                    entry.dwSize = sizeof(entry);
                    if (Process32FirstW(snapshot, &entry))
                    {
                        do
                        {
                            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                            if (!process)
                                continue;
                            wchar_t imagePath[32768] = {};
                            DWORD imageLength = static_cast<DWORD>(std::size(imagePath));
                            if (QueryFullProcessImageNameW(process, 0, imagePath, &imageLength))
                            {
                                std::error_code pathError;
                                const auto candidate = std::filesystem::weakly_canonical(std::filesystem::path(imagePath), pathError);
                                const auto expected = std::filesystem::weakly_canonical(executable, pathError);
                                if (!pathError && Lowercase(candidate.generic_string()) == Lowercase(expected.generic_string()))
                                {
                                    running = true;
                                    runningProcessId = entry.th32ProcessID;
                                }
                            }
                            CloseHandle(process);
                        } while (!running && Process32NextW(snapshot, &entry));
                    }
                    CloseHandle(snapshot);
                }

                // 실행 이미지 자체는 읽기 핸들 검사가 통과할 수 있다. 프로세스의 실제 이미지 경로를
                // 비교해야 긴 빌드가 끝난 뒤 Finalize에서 폴더 교체가 실패하는 낭비를 막을 수 있다.
                if (running)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Validate",
                        "Existing game executable is running. Close it before building. PID=" + std::to_string(runningProcessId), executable);
                    return false;
                }
            }

            if (std::filesystem::is_directory(outputDirectory, ec) && !ec)
            {
                for (std::filesystem::recursive_directory_iterator iterator(outputDirectory, ec), end;
                    iterator != end && !ec; iterator.increment(ec))
                {
                    if (!iterator->is_regular_file(ec))
                        continue;
                    const std::string extension = Lowercase(iterator->path().extension().string());
                    if (extension != ".exe" && extension != ".dll" && extension != ".pdb" && extension != ".json")
                        continue;

                    HANDLE file = CreateFileW(
                        iterator->path().c_str(), GENERIC_READ | DELETE, 0, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (file != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(file);
                        continue;
                    }
                    if (GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_LOCK_VIOLATION)
                    {
                        AddMessage(report, GameBuildMessageSeverity::Error, "Validate",
                            "Build output file is locked by another process. Close the Player, debugger, or file viewer before building.",
                            iterator->path());
                        return false;
                    }
                }
            }
#endif
            return true;
        }

#ifdef CC_PLATFORM_WINDOWS
        bool RunPlayerSmokeTest(
            const std::filesystem::path& staging,
            const std::string& executableName,
            const std::filesystem::path& baselinePath,
            const std::string& startSceneGuid,
            const std::shared_ptr<std::atomic_bool>& cancelRequested,
            GameBuildReport& report)
        {
            const std::filesystem::path executable = staging / executableName;
            const std::filesystem::path smokeReport = staging.parent_path() /
                (staging.filename().string() + ".smoke.json");
            std::error_code ec;
            std::filesystem::remove(smokeReport, ec);

            std::wstring commandLine = L"\"" + executable.wstring() + L"\" --smoke-report=\"" + smokeReport.wstring() + L"\"";
            std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
            mutableCommand.push_back(L'\0');

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            const std::wstring workingDirectory = staging.wstring();
            if (!CreateProcessW(
                    executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                    CREATE_NO_WINDOW, nullptr, workingDirectory.c_str(), &startup, &process))
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Player Smoke Test",
                    "Failed to start the staged Player. Win32 error=" + std::to_string(GetLastError()), executable);
                return false;
            }

            constexpr DWORD TimeoutMilliseconds = 30000;
            constexpr DWORD PollMilliseconds = 50;
            DWORD elapsed = 0;
            DWORD waitResult = WAIT_TIMEOUT;
            while (elapsed < TimeoutMilliseconds)
            {
                waitResult = WaitForSingleObject(process.hProcess, PollMilliseconds);
                if (waitResult != WAIT_TIMEOUT)
                    break;
                elapsed += PollMilliseconds;
                if (cancelRequested && cancelRequested->load())
                {
                    TerminateProcess(process.hProcess, 4);
                    WaitForSingleObject(process.hProcess, 5000);
                    CloseHandle(process.hThread);
                    CloseHandle(process.hProcess);
                    AddMessage(report, GameBuildMessageSeverity::Warning, "Cancel",
                        "Player smoke test cancelled by the user.", executable);
                    return false;
                }
            }
            if (waitResult == WAIT_TIMEOUT)
            {
                // 이 프로세스는 검증 단계가 직접 만든 자식이다. 제한 시간을 넘기면 종료해
                // 빌드 폴더 잠금과 백그라운드 Player가 남지 않게 한다.
                TerminateProcess(process.hProcess, 4);
                WaitForSingleObject(process.hProcess, 5000);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                AddMessage(report, GameBuildMessageSeverity::Error, "Player Smoke Test",
                    "Player did not finish the first-frame smoke test within 30 seconds.", executable);
                return false;
            }

            DWORD exitCode = 1;
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);

            nlohmann::json result;
            std::ifstream input(smokeReport);
            try
            {
                if (input.is_open())
                    input >> result;
            }
            catch (...)
            {
                result = nlohmann::json{};
            }
            std::filesystem::remove(smokeReport, ec);

            bool success = exitCode == 0 && result.value("Success", false) &&
                result.value("SceneLoaded", false) && result.value("Readback", false);
            double meanAbsoluteError = 0.0;
            int maximumCellError = 0;
            bool baselineCompared = false;
            if (success && std::filesystem::is_regular_file(baselinePath, ec) && !ec)
            {
                nlohmann::json baseline;
                std::ifstream baselineInput(baselinePath);
                try { baselineInput >> baseline; }
                catch (...) { baseline = nlohmann::json{}; }

                const auto actualGrid = result.value("LuminanceGrid", std::vector<int>{});
                const auto expectedGrid = baseline.value("LuminanceGrid", std::vector<int>{});
                const double allowedMeanError = baseline.value("AllowedMeanAbsoluteError", 12.0);
                const int allowedMaximumError = baseline.value("AllowedMaximumCellError", 48);
                const std::string baselineSceneGuid = baseline.value("StartSceneGuid", std::string{});
                if (!baselineSceneGuid.empty() && baselineSceneGuid != startSceneGuid)
                {
                    AddMessage(report, GameBuildMessageSeverity::Error, "Player Smoke Test",
                        "Visual baseline belongs to a different start scene. Regenerate BuildSmokeBaseline.json.", baselinePath);
                    success = false;
                }
                else if (!actualGrid.empty() && actualGrid.size() == expectedGrid.size())
                {
                    long long totalError = 0;
                    for (size_t index = 0; index < actualGrid.size(); ++index)
                    {
                        const int error = std::abs(actualGrid[index] - expectedGrid[index]);
                        totalError += error;
                        maximumCellError = (std::max)(maximumCellError, error);
                    }
                    meanAbsoluteError = static_cast<double>(totalError) / static_cast<double>(actualGrid.size());
                    baselineCompared = true;
                    success = meanAbsoluteError <= allowedMeanError && maximumCellError <= allowedMaximumError;
                }
                else
                    success = false;
            }
            if (!success)
            {
                std::ostringstream message;
                message << "Staged Player failed first-frame validation. exit=" << exitCode
                    << " sceneLoaded=" << result.value("SceneLoaded", false)
                    << " readback=" << result.value("Readback", false)
                    << " luminance=" << result.value("MinimumLuminance", 0)
                    << ".." << result.value("MaximumLuminance", 0)
                    << " distinct=" << result.value("DistinctLuminance", 0)
                    << " baseline=" << (baselineCompared ? "compared" : "unavailable")
                    << " meanError=" << meanAbsoluteError
                    << " maxCellError=" << maximumCellError;
                AddMessage(report, GameBuildMessageSeverity::Error, "Player Smoke Test", message.str(), executable);
                return false;
            }

            AddMessage(report, GameBuildMessageSeverity::Info, "Player Smoke Test",
                "Start scene loaded and a non-uniform frame was read back from the staged Player (" +
                    std::to_string(result.value("Width", 0)) + "x" + std::to_string(result.value("Height", 0)) +
                    (baselineCompared ? ", baseline mean error=" + std::to_string(meanAbsoluteError) : ", no visual baseline") + ").",
                executable);
            return true;
        }
#endif
    }

    GameBuildReport GameBuildService::Build(const GameBuildRequest& request) const
    {
        GameBuildReport report;
        report.OutputDirectory = request.OutputDirectory;
        AddMessage(report, GameBuildMessageSeverity::Info, "Environment",
            std::string("CCBuild compiler=MSVC ") + CC_STRINGIZE(_MSC_FULL_VER) +
            " process=" + (sizeof(void*) == 8 ? "x64" : "x86") +
            " nativeConfig=" +
#ifdef CC_DEBUG
            "Debug"
#else
            "Release"
#endif
            + ".",
            request.BinaryDirectory);

        auto publishProgress = [&request](const char* stage, float progress, const std::filesystem::path& path = {})
        {
            if (request.Progress)
                request.Progress(stage, progress, path);
        };
        auto cancellationRequested = [&request]()
        {
            return request.CancelRequested && request.CancelRequested->load();
        };

        publishProgress("Validate", 0.02f);

        const std::string validateStage = "Validate";
        std::error_code ec;
        const auto projectRoot = std::filesystem::absolute(request.ProjectRoot, ec).lexically_normal();
        if (ec || !std::filesystem::is_directory(projectRoot, ec) || ec)
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Project root does not exist.", request.ProjectRoot);
            return report;
        }

        const auto outputDirectory = std::filesystem::absolute(request.OutputDirectory, ec).lexically_normal();
        if (ec || outputDirectory.empty())
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Output directory is invalid.", request.OutputDirectory);
            return report;
        }
        report.OutputDirectory = outputDirectory;

        if (outputDirectory == projectRoot || IsInside(outputDirectory, projectRoot / "assets") || IsInside(projectRoot, outputDirectory))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Output directory overlaps the project or its assets.", outputDirectory);
            return report;
        }

        ProjectSettings projectSettings;
        if (!projectSettings.Load((projectRoot / "project.ccproject").string()))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Project settings could not be loaded.", projectRoot / "project.ccproject");
            return report;
        }

        const auto assetsRoot = projectRoot / "assets";
        if (!std::filesystem::is_directory(assetsRoot, ec) || ec)
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Project assets directory is missing.", assetsRoot);
            return report;
        }

        if (projectSettings.Data().StartSceneGuid.empty() && projectSettings.Data().StartScenePath.empty())
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Project Settings has no start scene. Set a start scene before building.", projectRoot / "project.ccproject");
            return report;
        }

        std::filesystem::path startScene;
        if (!projectSettings.Data().StartScenePath.empty())
            startScene = projectSettings.Data().StartScenePath;
        if (startScene.is_relative())
            startScene = projectRoot / startScene;
        startScene = startScene.lexically_normal();

        std::error_code startSceneError;
        const bool storedStartSceneExists = std::filesystem::is_regular_file(startScene, startSceneError) && !startSceneError;
        if (!storedStartSceneExists && !projectSettings.Data().StartSceneGuid.empty())
        {
            // 정상 프로젝트는 Path와 GUID를 함께 저장하므로 빌드 검사는 읽기 전용 Path를 먼저 쓴다.
            // 경로가 낡았을 때만 Asset Database를 스캔해 GUID 복구 비용과 원본 meta 변경 가능성을 줄인다.
            AssetDatabase::Scan(assetsRoot);
            startScene = AssetDatabase::GetPathFromGuid(projectSettings.Data().StartSceneGuid);
            if (startScene.is_relative())
                startScene = projectRoot / startScene;
            startScene = startScene.lexically_normal();
        }

        ec.clear();
        if (!std::filesystem::is_regular_file(startScene, ec) || ec || !IsInside(startScene, assetsRoot))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Start scene is missing or outside the assets directory.", startScene);
            return report;
        }

        const auto startSceneRelativeToAssets = startScene.lexically_relative(assetsRoot);
        if (startSceneRelativeToAssets.empty() || startSceneRelativeToAssets.is_absolute() ||
            ShouldExcludeAsset(startSceneRelativeToAssets, request.ExcludeLicensedSampleAssets))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Start scene is inside a build-excluded asset directory.", startScene);
            return report;
        }

        const auto binaryDirectory = std::filesystem::absolute(request.BinaryDirectory, ec).lexically_normal();
        if (ec || !std::filesystem::is_directory(binaryDirectory, ec) || ec)
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Binary directory does not exist.", request.BinaryDirectory);
            return report;
        }
        if (cancellationRequested())
        {
            AddMessage(report, GameBuildMessageSeverity::Warning, "Cancel", "Build cancelled before staging.");
            return report;
        }

        const std::filesystem::path requestedExecutable = request.ExecutableName.empty() ? "Game" : request.ExecutableName;
        if (requestedExecutable.is_absolute() || requestedExecutable.has_parent_path())
        {
            AddMessage(report, GameBuildMessageSeverity::Error, validateStage, "Executable name must be a file name without a directory.", requestedExecutable);
            return report;
        }
        std::string finalExecutableName = requestedExecutable.string();
        if (std::filesystem::path(finalExecutableName).extension() != ".exe")
            finalExecutableName += ".exe";
        if (!ValidateOutputCapacityAndLock(
                outputDirectory,
                finalExecutableName,
                binaryDirectory,
                assetsRoot,
                request.AvailableDiskBytesOverride,
                report))
            return report;

        const auto staging = outputDirectory.parent_path() / (outputDirectory.filename().string() + ".staging");
        const auto backup = outputDirectory.parent_path() / (outputDirectory.filename().string() + ".previous");
        IncrementalBuildCache incremental(outputDirectory, staging);
        incremental.Load(report);
        std::string fileError;
        if (!RemoveOwnedBuildDirectory(staging, fileError))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, "Stage Files", fileError, staging);
            return report;
        }
        std::filesystem::create_directories(staging, ec);
        if (ec || !WriteBuildMarker(staging, fileError))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, "Stage Files", ec ? ec.message() : fileError, staging);
            return report;
        }

        bool copySucceeded = true;
        publishProgress("Native Runtime", 0.10f, binaryDirectory);
        const std::array<std::pair<const char*, bool>, 7> runtimeFiles = {{
            { "CCEngine.dll", true },
            { "assimp-vc145-mt.dll", true },
            { "zlib1.dll", false },
            { "kubazip.dll", false },
            { "minizip.dll", false },
            { "poly2tri.dll", false },
            { "pugixml.dll", false }
        }};
        copySucceeded &= CopyFileTracked(binaryDirectory / "CCGame.exe", staging / finalExecutableName, report, "Native Runtime", true, &incremental);
        for (const auto& [filename, required] : runtimeFiles)
            copySucceeded &= CopyFileTracked(binaryDirectory / filename, staging / filename, report, "Native Runtime", required, &incremental);

        if (!request.DevelopmentBuild)
            copySucceeded &= StageVisualCppRuntime(staging, report, &incremental);
        else if (request.IncludeDebugSymbols)
        {
            copySucceeded &= CopyFileTracked(binaryDirectory / "CCGame.pdb", staging / "Symbols" / "CCGame.pdb", report, "Debug Symbols", false, &incremental);
            copySucceeded &= CopyFileTracked(binaryDirectory / "CCEngine.pdb", staging / "Symbols" / "CCEngine.pdb", report, "Debug Symbols", false, &incremental);
        }

#ifdef CC_PLATFORM_WINDOWS
        if (copySucceeded)
            copySucceeded = ValidateNativeDependencies(staging, report);
#endif

        if (copySucceeded)
        {
            publishProgress("Compile Scripts", 0.20f, assetsRoot / "Scripts");
            copySucceeded = CompileAndStageManagedRuntime(projectRoot, assetsRoot, staging, request.ManagedRuntime, report, &incremental);
        }
        if (copySucceeded)
            copySucceeded = ValidatePortableManagedRuntime(staging, request.ManagedRuntime, report);

        if (cancellationRequested())
            copySucceeded = false;

        std::vector<std::filesystem::path> alwaysIncludeAssets = request.AlwaysIncludeAssets;
        alwaysIncludeAssets.emplace_back("assets/shaders/Renderer2D.hlsl");
        alwaysIncludeAssets.emplace_back("assets/shaders/Base3D.hlsl");
        alwaysIncludeAssets.emplace_back("assets/shaders/ErrorShader.hlsl");
        alwaysIncludeAssets.emplace_back("assets/fonts/NotoSansKR-VariableFont_wght.ttf");

        AssetDependencyCollector dependencyCollector;
        publishProgress("Collect Dependencies", 0.38f, startScene);
        const AssetDependencyReport dependencies = dependencyCollector.Collect(projectRoot, startScene, alwaysIncludeAssets);
        for (const AssetDependencyIssue& issue : dependencies.Issues)
        {
            std::string message = issue.Message;
            if (!issue.JsonLocation.empty())
                message += " location=" + issue.JsonLocation;
            if (!issue.Guid.empty())
                message += " guid=" + issue.Guid;
            AddMessage(report, GameBuildMessageSeverity::Error, "Collect Dependencies", message, issue.SourceFile);
        }
        if (!dependencies.Success)
            copySucceeded = false;
        if (copySucceeded)
            copySucceeded = ValidateScriptReferences(staging, dependencies.Assets, report);

        for (const AssetDependency& dependency : dependencies.Assets)
        {
            if (cancellationRequested())
            {
                copySucceeded = false;
                break;
            }
            publishProgress("Stage Assets", 0.48f, dependency.SourcePath);
            if (ShouldExcludeAsset(dependency.RelativePath, request.ExcludeLicensedSampleAssets))
            {
                AddMessage(
                    report,
                    GameBuildMessageSeverity::Error,
                    "Collect Dependencies",
                    "A required asset is excluded from this build. Referenced by: " + dependency.ReferencedBy.string(),
                    dependency.SourcePath);
                copySucceeded = false;
                continue;
            }

            if (request.DevelopmentBuild || dependency.Kind != AssetKind::Shader)
            {
                copySucceeded &= CopyFileTracked(
                    dependency.SourcePath,
                    staging / "assets" / dependency.RelativePath,
                    report,
                    "Stage Assets",
                    true,
                    &incremental);
            }

        }

        AddMessage(
            report,
            GameBuildMessageSeverity::Info,
            "Collect Dependencies",
            "Collected " + std::to_string(dependencies.Assets.size()) + " referenced asset(s) from the start scene.");

        RuntimeManifestData runtimeManifest;
        runtimeManifest.ProjectName = request.ProductName.empty() ? projectSettings.Data().ProjectName : request.ProductName;
        runtimeManifest.ContentRoot = ".";
        runtimeManifest.AssetRoot = "assets";
        runtimeManifest.AssetManifest = "RuntimeAssetManifest.json";
        runtimeManifest.ShaderManifest = "ShaderBuildManifest.json";
        runtimeManifest.StartSceneGuid = projectSettings.Data().StartSceneGuid;
        const auto startSceneRelativeToProject = startScene.lexically_relative(projectRoot);
        runtimeManifest.StartScenePath = startSceneRelativeToProject.generic_string();
        runtimeManifest.GameWidth = projectSettings.Data().GameWidth;
        runtimeManifest.GameHeight = projectSettings.Data().GameHeight;
        runtimeManifest.PlayerWindowMode = request.PlayerWindowMode;
        runtimeManifest.DevelopmentBuild = request.DevelopmentBuild;
        if (startSceneRelativeToProject.empty() || startSceneRelativeToProject.is_absolute() ||
            !RuntimeManifest::Save(staging / "GameProject.json", runtimeManifest, fileError))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, "Manifest", fileError.empty() ? "Start scene path is outside the project." : fileError, staging / "GameProject.json");
            copySucceeded = false;
        }
        else
        {
            ++report.FilesWritten;
        }

        if (copySucceeded)
        {
            publishProgress("Compile Shaders", 0.68f);
            copySucceeded = CompileAndStageShaders(
                staging,
                dependencies.Assets,
                report,
                &incremental,
                request.DevelopmentBuild && request.AllowDevelopmentShaderFallback);
        }
        if (cancellationRequested())
            copySucceeded = false;
        if (copySucceeded)
        {
            publishProgress("Write Manifests", 0.84f);
            copySucceeded = WriteRuntimeAssetManifest(staging, dependencies.Assets, report);
        }
        if (copySucceeded)
            copySucceeded = WriteFileManifest(staging, request, report);
        if (copySucceeded && request.VerifyPlayerFrame)
        {
            publishProgress("Player Smoke Test", 0.94f, staging / finalExecutableName);
#ifdef CC_PLATFORM_WINDOWS
            copySucceeded = RunPlayerSmokeTest(
                staging,
                finalExecutableName,
                projectRoot / "BuildSmokeBaseline.json",
                projectSettings.Data().StartSceneGuid,
                request.CancelRequested,
                report);
#else
            AddMessage(report, GameBuildMessageSeverity::Warning, "Player Smoke Test", "First-frame validation is not implemented on this platform.");
#endif
        }
        if (!copySucceeded)
        {
            AddMessage(
                report,
                cancellationRequested() ? GameBuildMessageSeverity::Warning : GameBuildMessageSeverity::Error,
                cancellationRequested() ? "Cancel" : "Finalize",
                cancellationRequested() ? "Build cancelled. Staging was removed and the previous successful build was not changed."
                                        : "Build failed in staging. The previous successful build was not changed.");
            if (!RemoveOwnedBuildDirectory(staging, fileError))
                AddMessage(report, GameBuildMessageSeverity::Warning, "Finalize", fileError, staging);
            return report;
        }

        if (!RemoveOwnedBuildDirectory(backup, fileError))
        {
            AddMessage(report, GameBuildMessageSeverity::Error, "Finalize", fileError, backup);
            return report;
        }

        bool movedPreviousBuild = false;
        if (std::filesystem::exists(outputDirectory, ec) && !ec)
        {
            if (!IsOwnedBuildDirectory(outputDirectory))
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Finalize", "Output exists but is not owned by CCEngine. Choose an empty directory.", outputDirectory);
                return report;
            }
            std::filesystem::rename(outputDirectory, backup, ec);
            if (ec)
            {
                AddMessage(report, GameBuildMessageSeverity::Error, "Finalize", "Failed to preserve previous build: " + ec.message(), outputDirectory);
                return report;
            }
            movedPreviousBuild = true;
        }

        std::filesystem::rename(staging, outputDirectory, ec);
        if (ec)
        {
            if (movedPreviousBuild)
            {
                std::error_code restoreError;
                std::filesystem::rename(backup, outputDirectory, restoreError);
            }
            AddMessage(report, GameBuildMessageSeverity::Error, "Finalize", "Failed to publish staged build: " + ec.message(), outputDirectory);
            return report;
        }

        if (movedPreviousBuild && !RemoveOwnedBuildDirectory(backup, fileError))
            AddMessage(report, GameBuildMessageSeverity::Warning, "Finalize", fileError, backup);

        if (report.FilesReused > 0)
        {
            AddMessage(
                report,
                GameBuildMessageSeverity::Info,
                "Incremental Build",
                "Reused " + std::to_string(report.FilesReused) + " unchanged file(s), " +
                    std::to_string(report.BytesReused) + " byte(s).");
        }
        report.Success = true;
        publishProgress("Complete", 1.0f, outputDirectory);
        AddMessage(report, GameBuildMessageSeverity::Info, "Complete", request.DevelopmentBuild ? "Development game build completed." : "Release game build completed.", outputDirectory);
        return report;
    }

    int RunGameBuildCommand(
        const char* projectRoot,
        const char* outputDirectory,
        const char* binaryDirectory,
        bool developmentBuild,
        uint64_t availableDiskBytesOverride)
    {
        GameBuildRequest request;
        request.ProjectRoot = projectRoot ? projectRoot : "";
        request.OutputDirectory = outputDirectory ? outputDirectory : "";
        request.BinaryDirectory = binaryDirectory ? binaryDirectory : "";
        request.DevelopmentBuild = developmentBuild;
        if (availableDiskBytesOverride > 0)
            request.AvailableDiskBytesOverride = availableDiskBytesOverride;

        GameBuildService service;
        const GameBuildReport report = service.Build(request);
        for (const GameBuildMessage& message : report.Messages)
        {
            const char* severity = "info";
            if (message.Severity == GameBuildMessageSeverity::Warning)
                severity = "warning";
            else if (message.Severity == GameBuildMessageSeverity::Error)
                severity = "error";

            std::cout << '[' << severity << "] [" << message.Stage << "] " << message.Message;
            if (!message.Path.empty())
                std::cout << " (" << message.Path.string() << ')';
            std::cout << '\n';
        }

        if (!report.Success)
            return 1;

        std::cout << "Build output: " << report.OutputDirectory.string() << '\n';
        std::cout << "Files written: " << report.FilesWritten << ", bytes written: " << report.BytesWritten
                  << ", files reused: " << report.FilesReused << ", bytes reused: " << report.BytesReused << '\n';
        return 0;
    }

    int RunGameBuildProfileCommand(
        const char* projectRootText,
        const char* profilePathText,
        const char* binaryDirectoryText,
        const char* outputOverrideText,
        uint64_t availableDiskBytesOverride)
    {
        const std::filesystem::path projectRoot = projectRootText ? projectRootText : "";
        std::filesystem::path profilePath = profilePathText ? profilePathText : "";
        if (profilePath.empty())
            profilePath = projectRoot / "BuildProfile.ccbuildprofile";
        else if (profilePath.is_relative())
            profilePath = projectRoot / profilePath;

        GameBuildProfile profile;
        std::string error;
        if (!profile.Load(profilePath, error))
        {
            std::cerr << "[Build Profile] " << error << '\n';
            return 2;
        }

        const auto& data = profile.Data();
        GameBuildRequest request;
        request.ProjectRoot = projectRoot;
        request.OutputDirectory = outputOverrideText && *outputOverrideText
            ? std::filesystem::path(outputOverrideText)
            : projectRoot / data.OutputDirectory;
        request.BinaryDirectory = binaryDirectoryText ? binaryDirectoryText : "";
        request.ProductName = data.ProductName;
        request.ProductVersion = data.ProductVersion;
        request.UpdateChannel = data.UpdateChannel;
        request.ExecutableName = data.ExecutableName;
        request.DevelopmentBuild = data.Configuration == GameBuildConfiguration::Development;
        request.ManagedRuntime = data.ManagedRuntime;
        request.PlayerWindowMode = data.PlayerWindowMode;
        request.IncludeDebugSymbols = data.IncludeDebugSymbols;
        request.ExcludeLicensedSampleAssets = data.ExcludeLicensedSampleAssets;
        request.VerifyPlayerFrame = data.VerifyPlayerFrame;
        request.AllowDevelopmentShaderFallback = data.AllowDevelopmentShaderFallback;
        request.AlwaysIncludeAssets = data.AlwaysIncludeAssets;
        if (availableDiskBytesOverride > 0)
            request.AvailableDiskBytesOverride = availableDiskBytesOverride;

        GameBuildService service;
        const GameBuildReport report = service.Build(request);
        for (const GameBuildMessage& message : report.Messages)
        {
            const char* severity = message.Severity == GameBuildMessageSeverity::Error
                ? "error" : (message.Severity == GameBuildMessageSeverity::Warning ? "warning" : "info");
            std::cout << '[' << severity << "] [" << message.Stage << "] " << message.Message;
            if (!message.Path.empty())
                std::cout << " (" << message.Path.string() << ')';
            std::cout << '\n';
        }
        return report.Success ? 0 : 1;
    }
}
