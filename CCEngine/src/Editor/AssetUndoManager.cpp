#include "Editor/AssetUndoManager.h"

#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace CCEngine
{
    void AssetUndoManager::Push(const Command& command)
    {
        if (command.Items.empty())
            return;

        m_UndoStack.push_back(command);
        if (m_UndoStack.size() > s_MaxCommands)
        {
            Command oldCommand = m_UndoStack.front();
            m_UndoStack.erase(m_UndoStack.begin());

            CleanupCommandBackups(oldCommand);
        }

        for (const Command& redoCommand : m_RedoStack)
            CleanupCommandBackups(redoCommand);
        m_RedoStack.clear();
    }

    bool AssetUndoManager::Undo(std::filesystem::path* preferredDirectory)
    {
        if (m_UndoStack.empty())
            return false;

        Command command = m_UndoStack.back();
        if (!ApplyCommand(command, true, preferredDirectory))
            return false;

        m_UndoStack.pop_back();
        m_RedoStack.push_back(command);
        ConsoleLog::Info("Asset Undo: " + command.Label);
        return true;
    }

    bool AssetUndoManager::Redo(std::filesystem::path* preferredDirectory)
    {
        if (m_RedoStack.empty())
            return false;

        Command command = m_RedoStack.back();
        if (!ApplyCommand(command, false, preferredDirectory))
            return false;

        m_RedoStack.pop_back();
        m_UndoStack.push_back(command);
        ConsoleLog::Info("Asset Redo: " + command.Label);
        return true;
    }

    bool AssetUndoManager::Seek(size_t targetAppliedCount, std::filesystem::path* preferredDirectory)
    {
        const size_t totalCommands = m_UndoStack.size() + m_RedoStack.size();
        targetAppliedCount = (std::min)(targetAppliedCount, totalCommands);

        bool changed = false;
        while (m_UndoStack.size() > targetAppliedCount)
        {
            if (!Undo(preferredDirectory))
                break;
            changed = true;
        }

        while (m_UndoStack.size() < targetAppliedCount)
        {
            if (!Redo(preferredDirectory))
                break;
            changed = true;
        }

        return changed;
    }

    std::vector<std::string> AssetUndoManager::GetHistoryLabels() const
    {
        std::vector<std::string> labels;
        labels.reserve(m_UndoStack.size() + m_RedoStack.size());

        for (const Command& command : m_UndoStack)
            labels.push_back(FormatLabel(command));

        for (auto it = m_RedoStack.rbegin(); it != m_RedoStack.rend(); ++it)
            labels.push_back(FormatLabel(*it));

        return labels;
    }

    bool AssetUndoManager::PrepareDeleteBackup(const std::filesystem::path& source, Item& item) const
    {
        std::error_code ec;
        if (!std::filesystem::exists(source, ec) || ec)
            return false;

        item.BackupPath = MakeDeleteBackupPath(source);
        item.BackupMetaPath = AssetDatabase::GetMetaPath(item.BackupPath);
        std::filesystem::create_directories(item.BackupPath.parent_path(), ec);
        if (ec)
            return false;

        // 삭제 Undo는 OS 휴지통만 믿지 않고 엔진 내부 백업을 하나 둔다.
        // 그래야 사용자가 Ctrl+Z를 누를 때 원래 프로젝트 경로로 즉시 복구할 수 있다.
        if (item.IsDirectory)
        {
            std::filesystem::copy(
                source,
                item.BackupPath,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing,
                ec);
        }
        else
        {
            std::filesystem::copy_file(source, item.BackupPath, std::filesystem::copy_options::none, ec);
            if (!ec)
            {
                std::filesystem::path sourceMeta = AssetDatabase::GetMetaPath(source);
                if (std::filesystem::exists(sourceMeta, ec) && !ec)
                    std::filesystem::copy_file(sourceMeta, item.BackupMetaPath, std::filesystem::copy_options::none, ec);
            }
        }

        if (ec)
        {
            std::error_code cleanupEc;
            std::filesystem::remove_all(item.BackupPath, cleanupEc);
            std::filesystem::remove(item.BackupMetaPath, cleanupEc);
            ConsoleLog::Warning("Failed to prepare delete undo backup: " + source.string());
            return false;
        }

        return true;
    }

    bool AssetUndoManager::PrepareImportBackup(const std::filesystem::path& importedPath, Item& item) const
    {
        std::error_code ec;
        if (!std::filesystem::exists(importedPath, ec) || ec)
            return false;

        item.ToPath = importedPath;
        item.IsDirectory = std::filesystem::is_directory(importedPath, ec) && !ec;
        item.BackupPath = MakeDeleteBackupPath(importedPath);
        item.BackupMetaPath = AssetDatabase::GetMetaPath(item.BackupPath);

        // Import Undo는 이 시점에 파일을 복사하지 않는다.
        // Undo 순간의 프로젝트 파일을 백업 위치로 옮겨야, 사용자가 import 직후 바꾼 내용도 Redo 때 유지된다.

        return true;
    }

    bool AssetUndoManager::ApplyCommand(const Command& command, bool undo, std::filesystem::path* preferredDirectory)
    {
        if (command.Items.empty())
            return false;

        bool changed = false;
        auto applyItem = [&](const Item& item) -> bool
        {
            switch (command.Operation)
            {
                case Kind::CreateFolder:
                {
                    const std::filesystem::path& folderPath = item.ToPath;
                    std::error_code ec;
                    if (undo)
                    {
                        if (!std::filesystem::exists(folderPath, ec) || ec)
                            return false;

                        // 생성 Undo는 빈 폴더만 제거한다.
                        // 사용자가 그 안에 파일을 넣은 뒤라면 데이터 손실을 막기 위해 실패로 처리한다.
                        if (!std::filesystem::is_empty(folderPath, ec) || ec)
                        {
                            ConsoleLog::Warning("Create Folder Undo skipped non-empty folder: " + folderPath.string());
                            return false;
                        }

                        std::filesystem::remove(folderPath, ec);
                        return !ec;
                    }

                    if (std::filesystem::exists(folderPath, ec) && !ec)
                        return false;

                    std::filesystem::create_directories(folderPath, ec);
                    return !ec;
                }
                case Kind::Rename:
                case Kind::Move:
                {
                    const std::filesystem::path& from = undo ? item.ToPath : item.FromPath;
                    const std::filesystem::path& to = undo ? item.FromPath : item.ToPath;
                    return MoveAssetBundle(from, to, item.IsDirectory);
                }
                case Kind::Import:
                {
                    if (undo)
                        return RemoveCreatedAssetForUndo(item);

                    // Import Redo는 삭제 복구와 비슷하지만 원래 위치가 FromPath가 아니라 ToPath다.
                    // Import 명령에서 ToPath는 프로젝트 안에 새로 생긴 파일의 위치를 뜻한다.
                    return RestoreAssetFromBackupTo(item, item.ToPath, "Import Redo");
                }
                case Kind::RecycleDelete:
                {
                    if (undo)
                        return RestoreDeletedAssetFromBackup(item);

                    return MoveAssetBundle(item.FromPath, item.BackupPath, item.IsDirectory);
                }
                default:
                    return false;
            }
        };

        if (undo)
        {
            for (auto it = command.Items.rbegin(); it != command.Items.rend(); ++it)
                changed = applyItem(*it) || changed;
        }
        else
        {
            for (const Item& item : command.Items)
                changed = applyItem(item) || changed;
        }

        if (!changed)
            return false;

        if (preferredDirectory)
            *preferredDirectory = GetPreferredDirectory(command);
        return true;
    }

    bool AssetUndoManager::MoveAssetBundle(const std::filesystem::path& from, const std::filesystem::path& to, bool isDirectory) const
    {
        std::error_code ec;
        if (!std::filesystem::exists(from, ec) || ec)
            return false;

        if (std::filesystem::exists(to, ec) && !ec)
        {
            ConsoleLog::Warning("Asset Undo blocked by existing path: " + to.string());
            return false;
        }

        if (!to.parent_path().empty())
            std::filesystem::create_directories(to.parent_path(), ec);
        if (ec)
            return false;

        // Undo/Redo는 에셋 파일과 sidecar meta를 항상 한 묶음으로 움직인다.
        // meta가 따로 남으면 GUID는 살아 있어도 sourcePath가 어긋나 참조 검증에서 깨진다.
        std::filesystem::rename(from, to, ec);
        if (ec)
            return false;

        if (!isDirectory)
        {
            std::filesystem::path fromMeta = AssetDatabase::GetMetaPath(from);
            std::filesystem::path toMeta = AssetDatabase::GetMetaPath(to);
            if (std::filesystem::exists(fromMeta, ec) && !ec)
            {
                std::filesystem::rename(fromMeta, toMeta, ec);
                if (ec)
                {
                    std::error_code rollbackEc;
                    std::filesystem::rename(to, from, rollbackEc);
                    return false;
                }
            }
        }

        return true;
    }

    bool AssetUndoManager::RemoveCreatedAssetForUndo(const Item& item) const
    {
        std::error_code ec;
        if (item.ToPath.empty() || item.BackupPath.empty())
            return false;

        if (!std::filesystem::exists(item.ToPath, ec) || ec)
            return false;

        if (std::filesystem::exists(item.BackupPath, ec) && !ec)
        {
            ConsoleLog::Warning("Import Undo blocked by existing backup: " + item.BackupPath.string());
            return false;
        }

        if (!item.BackupPath.parent_path().empty())
            std::filesystem::create_directories(item.BackupPath.parent_path(), ec);
        if (ec)
            return false;

        // Import Undo는 방금 만든 프로젝트 에셋을 백업 위치로 옮긴다.
        // Redo 때 같은 백업을 다시 원래 위치로 돌려놓기 위해 삭제하지 않는다.
        std::filesystem::rename(item.ToPath, item.BackupPath, ec);
        if (ec)
            return false;

        if (!item.IsDirectory)
        {
            std::filesystem::path sourceMeta = AssetDatabase::GetMetaPath(item.ToPath);
            if (std::filesystem::exists(sourceMeta, ec) && !ec)
            {
                std::filesystem::rename(sourceMeta, item.BackupMetaPath, ec);
                if (ec)
                    ConsoleLog::Warning("Import Undo moved asset but failed to move meta: " + item.ToPath.string());
            }
        }

        return true;
    }

    bool AssetUndoManager::RestoreDeletedAssetFromBackup(const Item& item) const
    {
        return RestoreAssetFromBackupTo(item, item.FromPath, "Delete Undo");
    }

    bool AssetUndoManager::RestoreAssetFromBackupTo(const Item& item, const std::filesystem::path& targetPath, const std::string& logPrefix) const
    {
        std::error_code ec;
        if (targetPath.empty() || item.BackupPath.empty())
            return false;

        if (std::filesystem::exists(targetPath, ec) && !ec)
        {
            ConsoleLog::Warning(logPrefix + " blocked by existing path: " + targetPath.string());
            return false;
        }

        if (!std::filesystem::exists(item.BackupPath, ec) || ec)
        {
            ConsoleLog::Warning(logPrefix + " backup is missing: " + item.BackupPath.string());
            return false;
        }

        if (!targetPath.parent_path().empty())
            std::filesystem::create_directories(targetPath.parent_path(), ec);
        if (ec)
            return false;

        std::filesystem::rename(item.BackupPath, targetPath, ec);
        if (ec)
            return false;

        if (!item.IsDirectory && !item.BackupMetaPath.empty() && std::filesystem::exists(item.BackupMetaPath, ec) && !ec)
        {
            std::filesystem::path restoredMeta = AssetDatabase::GetMetaPath(targetPath);
            std::filesystem::rename(item.BackupMetaPath, restoredMeta, ec);
            if (ec)
                ConsoleLog::Warning(logPrefix + " restored asset but failed to restore meta: " + targetPath.string());
        }

        return true;
    }

    void AssetUndoManager::CleanupCommandBackups(const Command& command) const
    {
        // Redo가 버려지거나 히스토리 제한으로 명령이 밀리면 그 백업은 더 이상 사용자에게 닿지 않는다.
        // 이때만 지워야 Undo/Redo 중간에 필요한 복구 파일을 잃지 않는다.
        for (const Item& item : command.Items)
        {
            std::error_code ec;
            if (!item.BackupPath.empty())
                std::filesystem::remove_all(item.BackupPath, ec);
            if (!item.BackupMetaPath.empty())
                std::filesystem::remove(item.BackupMetaPath, ec);
        }
    }

    std::filesystem::path AssetUndoManager::MakeDeleteBackupPath(const std::filesystem::path& source) const
    {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path backupRoot = std::filesystem::current_path() / ".ccengine" / "AssetUndoTrash";

        std::ostringstream name;
        name << now << "_" << source.filename().string();
        std::filesystem::path candidate = backupRoot / name.str();

        int suffix = 1;
        std::error_code ec;
        while (std::filesystem::exists(candidate, ec) && !ec)
        {
            std::ostringstream retryName;
            retryName << now << "_" << suffix++ << "_" << source.filename().string();
            candidate = backupRoot / retryName.str();
        }

        return candidate;
    }

    std::string AssetUndoManager::FormatLabel(const Command& command) const
    {
        std::string label = command.Label.empty() ? "Asset Operation" : command.Label;
        if (command.Items.size() == 1)
        {
            const Item& item = command.Items.front();
            std::filesystem::path displayPath = item.ToPath.empty() ? item.FromPath : item.ToPath;
            if (!displayPath.empty())
                label += " " + displayPath.filename().string();
        }
        else
        {
            label += " (" + std::to_string(command.Items.size()) + ")";
        }

        return label;
    }

    std::filesystem::path AssetUndoManager::GetPreferredDirectory(const Command& command) const
    {
        const Item& item = command.Items.front();
        return item.FromPath.empty() ? item.ToPath.parent_path() : item.FromPath.parent_path();
    }
}
