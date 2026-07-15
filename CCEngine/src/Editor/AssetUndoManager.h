#pragma once

#include "Core.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    class CC_API AssetUndoManager
    {
    public:
        enum class Kind
        {
            CreateFolder = 0,
            Rename,
            Move,
            Import,
            RecycleDelete
        };

        struct Item
        {
            std::filesystem::path FromPath;
            std::filesystem::path ToPath;
            std::filesystem::path BackupPath;
            std::filesystem::path BackupMetaPath;
            bool IsDirectory = false;
        };

        struct Command
        {
            Kind Operation = Kind::Move;
            std::string Label;
            std::vector<Item> Items;
        };

        void Push(const Command& command);
        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }
        bool Undo(std::filesystem::path* preferredDirectory = nullptr);
        bool Redo(std::filesystem::path* preferredDirectory = nullptr);
        bool Seek(size_t targetAppliedCount, std::filesystem::path* preferredDirectory = nullptr);
        size_t GetAppliedCount() const { return m_UndoStack.size(); }
        std::vector<std::string> GetHistoryLabels() const;

        bool PrepareDeleteBackup(const std::filesystem::path& source, Item& item) const;
        bool PrepareImportBackup(const std::filesystem::path& importedPath, Item& item) const;

    private:
        bool ApplyCommand(const Command& command, bool undo, std::filesystem::path* preferredDirectory);
        bool MoveAssetBundle(const std::filesystem::path& from, const std::filesystem::path& to, bool isDirectory) const;
        bool RemoveCreatedAssetForUndo(const Item& item) const;
        bool RestoreAssetFromBackupTo(const Item& item, const std::filesystem::path& targetPath, const std::string& logPrefix) const;
        bool RestoreDeletedAssetFromBackup(const Item& item) const;
        void CleanupCommandBackups(const Command& command) const;
        std::filesystem::path MakeDeleteBackupPath(const std::filesystem::path& source) const;
        std::string FormatLabel(const Command& command) const;
        std::filesystem::path GetPreferredDirectory(const Command& command) const;

        // 에셋 히스토리는 디스크 상태를 움직이므로 패널별로 나누지 않는다.
        // 여러 에셋 브라우저가 열려도 같은 프로젝트 히스토리를 바라봐야 한다.
        std::vector<Command> m_UndoStack;
        std::vector<Command> m_RedoStack;
        static constexpr size_t s_MaxCommands = 100;
    };
}
