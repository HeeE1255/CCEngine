#include "Scripting/ScriptEngine.h"
#include "Core/ConsoleLog.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptMetadata.h"
#include "json.hpp"

#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
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
        using ManagedCreateFn = int(__cdecl*)(uint32_t, const char*, const char*);
        using ManagedDestroyFn = void(__cdecl*)(uint32_t);
        using ManagedLifecycleFn = void(__cdecl*)(uint32_t, int, float);
        using ManagedPhysicsEventFn = void(__cdecl*)(uint32_t, int, uint32_t);
        using ManagedUpdateFn = void(__cdecl*)(uint32_t, float);

        using GetTranslationFn = int(__cdecl*)(uint32_t, float*, float*, float*);
        using SetTranslationFn = void(__cdecl*)(uint32_t, float, float, float);
        using LogFn = void(__cdecl*)(const char*, int);

        struct ScriptEngineData
        {
            HMODULE HostfxrModule = nullptr;
            Scene* ActiveScene = nullptr;
            ManagedInitializeFn Initialize = nullptr;
            ManagedShutdownFn Shutdown = nullptr;
            ManagedCreateFn Create = nullptr;
            ManagedDestroyFn Destroy = nullptr;
            ManagedLifecycleFn InvokeLifecycle = nullptr;
            ManagedPhysicsEventFn InvokePhysicsEvent = nullptr;
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

        void __cdecl LogFromManaged(const char* message, int level)
        {
            if (!message)
                return;

            // 스크립트 로그는 사용자가 의도적으로 남긴 기록이다.
            // 엔진 내부 이벤트를 자동으로 뿌리지 않고, C# Debug API가 넘긴 레벨만 Console에 반영한다.
            const std::string text = std::string("[C#] ") + message;
            switch (level)
            {
            case 1:
                ConsoleLog::Warning(text);
                break;
            case 2:
                ConsoleLog::Error(text);
                break;
            default:
                ConsoleLog::Info(text);
                break;
            }
        }

        std::vector<float> ParseFloatList(const std::string& text)
        {
            std::vector<float> values;
            std::string token;
            std::stringstream stream(text);
            while (std::getline(stream, token, ','))
            {
                try { values.push_back(std::stof(token)); }
                catch (...) { values.push_back(0.0f); }
            }
            return values;
        }

        nlohmann::json BuildFieldOverrideJson(const ScriptComponent& script)
        {
            nlohmann::json fields = nlohmann::json::object();
            const ScriptClassInfo* classInfo = ScriptMetadata::FindClass(script.ClassName);
            if (!classInfo)
                return fields;

            for (const auto& field : classInfo->Fields)
            {
                auto overrideIt = script.FieldOverrides.find(field.Name);
                if (overrideIt == script.FieldOverrides.end())
                    continue;

                const std::string& value = overrideIt->second;
                try
                {
                    // 저장 값은 문자열이지만, C#에 넘길 때는 원래 필드 타입으로 되돌린다.
                    switch (field.Type)
                    {
                    case ScriptFieldType::Float:
                        fields[field.Name] = std::stof(value);
                        break;
                    case ScriptFieldType::Int:
                        fields[field.Name] = std::stoi(value);
                        break;
                    case ScriptFieldType::Bool:
                        fields[field.Name] = (value == "true" || value == "1" || value == "True");
                        break;
                    case ScriptFieldType::String:
                        fields[field.Name] = value;
                        break;
                    case ScriptFieldType::Vector3:
                    {
                        auto values = ParseFloatList(value);
                        fields[field.Name] = {
                            { "X", values.size() > 0 ? values[0] : 0.0f },
                            { "Y", values.size() > 1 ? values[1] : 0.0f },
                            { "Z", values.size() > 2 ? values[2] : 0.0f }
                        };
                        break;
                    }
                    default:
                        break;
                    }
                }
                catch (...)
                {
                    ConsoleLog::Warning("Invalid script field value: " + script.ClassName + "." + field.Name);
                }
            }

            return fields;
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
                LoadManagedFunction(loadAssembly, coreAssembly, L"InvokeLifecycleInstance", reinterpret_cast<void**>(&s_Data.InvokeLifecycle)) &&
                LoadManagedFunction(loadAssembly, coreAssembly, L"InvokePhysicsEventInstance", reinterpret_cast<void**>(&s_Data.InvokePhysicsEvent)) &&
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

    void ScriptEngine::Shutdown()
    {
        Stop();

        // hostfxr.dll은 C# 런타임을 붙이기 위해 명시적으로 로드한 모듈이다.
        // 엔진 종료 시 핸들을 놓아야 다음 실행과 누수 검사에서 스크립트 호스트 상태가 남지 않는다.
        if (s_Data.HostfxrModule)
        {
            FreeLibrary(s_Data.HostfxrModule);
            s_Data.HostfxrModule = nullptr;
        }

        s_Data.Initialize = nullptr;
        s_Data.Shutdown = nullptr;
        s_Data.Create = nullptr;
        s_Data.Destroy = nullptr;
        s_Data.InvokeLifecycle = nullptr;
        s_Data.InvokePhysicsEvent = nullptr;
        s_Data.Update = nullptr;
        s_Data.RuntimeLoaded = false;
    }

    bool ScriptEngine::CreateInstance(uint32_t entityID, const ScriptComponent& script)
    {
        if (!s_Data.Running || script.ClassName.empty())
            return false;

        const std::string fieldsJson = BuildFieldOverrideJson(script).dump();
        return s_Data.Create(entityID, script.ClassName.c_str(), fieldsJson.c_str()) == 0;
    }

    void ScriptEngine::DestroyInstance(uint32_t entityID)
    {
        if (s_Data.Running)
            s_Data.Destroy(entityID);
    }

    void ScriptEngine::InvokeLifecycle(uint32_t entityID, ScriptLifecycleEvent eventType, float deltaTime)
    {
        if (s_Data.Running && s_Data.InvokeLifecycle)
            s_Data.InvokeLifecycle(entityID, static_cast<int>(eventType), deltaTime);
    }

    void ScriptEngine::InvokePhysicsEvent(uint32_t entityID, ScriptPhysicsEvent eventType, uint32_t otherEntityID)
    {
        if (s_Data.Running && s_Data.InvokePhysicsEvent)
            s_Data.InvokePhysicsEvent(entityID, static_cast<int>(eventType), otherEntityID);
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
