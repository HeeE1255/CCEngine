#include "Scripting/ScriptEngine.h"
#include "Core/ConsoleLog.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    namespace
    {
        using HostHandle = void*;
        using HostfxrInitializeFn = int(__cdecl*)(const wchar_t*, const void*, HostHandle*);
        using HostfxrGetDelegateFn = int(__cdecl*)(HostHandle, int, void**);
        using HostfxrCloseFn = int(__cdecl*)(HostHandle);
        using LoadAssemblyFn = int(__cdecl*)(const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, void*, void**);

        using ManagedInitializeFn = int(__cdecl*)(void*, void*, void*, const wchar_t*);
        using ManagedShutdownFn = void(__cdecl*)();
        using ManagedCreateFn = int(__cdecl*)(uint32_t, const char*);
        using ManagedDestroyFn = void(__cdecl*)(uint32_t);
        using ManagedUpdateFn = void(__cdecl*)(uint32_t, float);

        using GetTranslationFn = int(__cdecl*)(uint32_t, float*, float*, float*);
        using SetTranslationFn = void(__cdecl*)(uint32_t, float, float, float);
        using LogFn = void(__cdecl*)(const char*);

        struct ScriptEngineData
        {
            HMODULE HostfxrModule = nullptr;
            Scene* ActiveScene = nullptr;
            ManagedInitializeFn Initialize = nullptr;
            ManagedShutdownFn Shutdown = nullptr;
            ManagedCreateFn Create = nullptr;
            ManagedDestroyFn Destroy = nullptr;
            ManagedUpdateFn Update = nullptr;
            bool RuntimeLoaded = false;
            bool Running = false;
        };

        ScriptEngineData s_Data;

        std::filesystem::path FindHostfxr()
        {
            std::vector<std::filesystem::path> roots;
            if (const wchar_t* dotnetRoot = _wgetenv(L"DOTNET_ROOT"))
                roots.emplace_back(dotnetRoot);
            roots.emplace_back(L"C:\\Program Files\\dotnet");

            for (const auto& root : roots)
            {
                std::filesystem::path fxrRoot = root / "host" / "fxr";
                std::error_code ec;
                if (!std::filesystem::exists(fxrRoot, ec))
                    continue;

                std::vector<std::filesystem::path> versions;
                for (const auto& entry : std::filesystem::directory_iterator(fxrRoot, ec))
                {
                    if (entry.is_directory())
                        versions.push_back(entry.path());
                }
                std::sort(versions.begin(), versions.end());
                if (!versions.empty())
                    return versions.back() / "hostfxr.dll";
            }
            return {};
        }

        bool LoadManagedFunction(
            LoadAssemblyFn loadAssembly,
            const std::filesystem::path& assemblyPath,
            const wchar_t* methodName,
            void** function)
        {
            // UnmanagedCallersOnly 메서드는 별도 delegate 형식 없이 네이티브 함수 포인터로 꺼낸다.
            const wchar_t* unmanagedCallersOnly = reinterpret_cast<const wchar_t*>(-1);
            constexpr const wchar_t* typeName = L"CCEngine.Internal.ScriptHost, CCEngine.ScriptCore";
            int result = loadAssembly(
                assemblyPath.c_str(),
                typeName,
                methodName,
                unmanagedCallersOnly,
                nullptr,
                function);
            return result == 0 && *function != nullptr;
        }

        int __cdecl GetTranslation(uint32_t entityID, float* x, float* y, float* z)
        {
            if (!s_Data.ActiveScene || !s_Data.ActiveScene->GetRegistry().valid(static_cast<entt::entity>(entityID)))
                return 0;

            Entity entity{ static_cast<entt::entity>(entityID), s_Data.ActiveScene };
            if (!entity.HasComponent<TransformComponent>())
                return 0;

            const auto& value = entity.GetComponent<TransformComponent>().Translation;
            *x = value.x;
            *y = value.y;
            *z = value.z;
            return 1;
        }

        void __cdecl SetTranslation(uint32_t entityID, float x, float y, float z)
        {
            if (!s_Data.ActiveScene || !s_Data.ActiveScene->GetRegistry().valid(static_cast<entt::entity>(entityID)))
                return;

            Entity entity{ static_cast<entt::entity>(entityID), s_Data.ActiveScene };
            if (entity.HasComponent<TransformComponent>())
                entity.GetComponent<TransformComponent>().Translation = { x, y, z };
        }

        void __cdecl LogFromManaged(const char* message)
        {
            if (message)
                ConsoleLog::Info(std::string("[C#] ") + message);
        }

        bool LoadRuntime()
        {
            if (s_Data.RuntimeLoaded)
                return true;

            const auto scriptsRoot = std::filesystem::current_path() / "assets" / "Scripts";
            const auto buildRoot = scriptsRoot / "Build";
            const auto runtimeConfig = buildRoot / "CCEngine.ScriptCore.runtimeconfig.json";
            const auto coreAssembly = buildRoot / "CCEngine.ScriptCore.dll";

            if (!std::filesystem::exists(runtimeConfig) || !std::filesystem::exists(coreAssembly))
            {
                ConsoleLog::Error("C# runtime files are missing. Run assets/Scripts/BuildScripts.ps1.");
                return false;
            }

            const auto hostfxrPath = FindHostfxr();
            if (hostfxrPath.empty())
            {
                ConsoleLog::Error(".NET hostfxr was not found. Install the .NET 8 x64 runtime.");
                return false;
            }

            s_Data.HostfxrModule = LoadLibraryW(hostfxrPath.c_str());
            if (!s_Data.HostfxrModule)
            {
                ConsoleLog::Error("Failed to load hostfxr.dll.");
                return false;
            }

            auto initializeForConfig = reinterpret_cast<HostfxrInitializeFn>(
                GetProcAddress(s_Data.HostfxrModule, "hostfxr_initialize_for_runtime_config"));
            auto getRuntimeDelegate = reinterpret_cast<HostfxrGetDelegateFn>(
                GetProcAddress(s_Data.HostfxrModule, "hostfxr_get_runtime_delegate"));
            auto closeHost = reinterpret_cast<HostfxrCloseFn>(
                GetProcAddress(s_Data.HostfxrModule, "hostfxr_close"));

            if (!initializeForConfig || !getRuntimeDelegate || !closeHost)
            {
                ConsoleLog::Error("The installed hostfxr does not expose the required hosting API.");
                return false;
            }

            HostHandle host = nullptr;
            if (initializeForConfig(runtimeConfig.c_str(), nullptr, &host) != 0 || !host)
            {
                ConsoleLog::Error("Failed to initialize the .NET runtime.");
                return false;
            }

            // hostfxr delegate type 5는 어셈블리에서 정적 진입점을 찾는 함수다.
            LoadAssemblyFn loadAssembly = nullptr;
            int delegateResult = getRuntimeDelegate(host, 5, reinterpret_cast<void**>(&loadAssembly));
            closeHost(host);
            if (delegateResult != 0 || !loadAssembly)
            {
                ConsoleLog::Error("Failed to acquire the .NET assembly loader.");
                return false;
            }

            bool loaded =
                LoadManagedFunction(loadAssembly, coreAssembly, L"Initialize", reinterpret_cast<void**>(&s_Data.Initialize)) &&
                LoadManagedFunction(loadAssembly, coreAssembly, L"Shutdown", reinterpret_cast<void**>(&s_Data.Shutdown)) &&
                LoadManagedFunction(loadAssembly, coreAssembly, L"CreateInstance", reinterpret_cast<void**>(&s_Data.Create)) &&
                LoadManagedFunction(loadAssembly, coreAssembly, L"DestroyInstance", reinterpret_cast<void**>(&s_Data.Destroy)) &&
                LoadManagedFunction(loadAssembly, coreAssembly, L"UpdateInstance", reinterpret_cast<void**>(&s_Data.Update));

            if (!loaded)
            {
                ConsoleLog::Error("Failed to bind the C# script entry points.");
                return false;
            }

            s_Data.RuntimeLoaded = true;
            return true;
        }
    }

    bool ScriptEngine::Start(Scene* scene)
    {
        if (!scene || !LoadRuntime())
            return false;

        const auto gameAssembly = std::filesystem::current_path() / "assets" / "Scripts" / "Build" / "GameScripts.dll";
        if (!std::filesystem::exists(gameAssembly))
        {
            ConsoleLog::Error("GameScripts.dll is missing. Run assets/Scripts/BuildScripts.ps1.");
            return false;
        }

        s_Data.ActiveScene = scene;
        int result = s_Data.Initialize(
            reinterpret_cast<void*>(static_cast<GetTranslationFn>(&GetTranslation)),
            reinterpret_cast<void*>(static_cast<SetTranslationFn>(&SetTranslation)),
            reinterpret_cast<void*>(static_cast<LogFn>(&LogFromManaged)),
            gameAssembly.c_str());
        s_Data.Running = result == 0;
        if (!s_Data.Running)
        {
            s_Data.ActiveScene = nullptr;
            ConsoleLog::Error("Failed to load the C# game script assembly.");
        }
        return s_Data.Running;
    }

    void ScriptEngine::Stop()
    {
        if (s_Data.Running && s_Data.Shutdown)
            s_Data.Shutdown();
        s_Data.Running = false;
        s_Data.ActiveScene = nullptr;
    }

    bool ScriptEngine::CreateInstance(uint32_t entityID, const char* className)
    {
        return s_Data.Running && className && s_Data.Create(entityID, className) == 0;
    }

    void ScriptEngine::DestroyInstance(uint32_t entityID)
    {
        if (s_Data.Running)
            s_Data.Destroy(entityID);
    }

    void ScriptEngine::UpdateInstance(uint32_t entityID, float deltaTime)
    {
        if (s_Data.Running)
            s_Data.Update(entityID, deltaTime);
    }

    bool ScriptEngine::IsRunning()
    {
        return s_Data.Running;
    }
}
