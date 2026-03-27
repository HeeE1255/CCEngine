workspace "CCEngine"
    architecture "x64"
    startproject "Sandbox"

    configurations { "Debug", "Release", "Dist" }

-- 출력 경로 설정
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- [공통] 모든 프로젝트에 적용되는 설정
filter "system:windows"
    systemversion "latest"
    defines { "CC_PLATFORM_WINDOWS" }
    buildoptions { "/utf-8" }

-- 1. 엔진 프로젝트 (CCEngine)
project "CCEngine"
    location "CCEngine"
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("bin/" .. outputdir)
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    defines { "CC_BUILD_DLL" } 

    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }

    includedirs {
        "%{prj.name}/src",
        "%{prj.name}/vendor/spdlog/include"
        "%{prj.name}/src/ImGui"
    }

    links {
        "d3d11",
        "dxgi",
        "d3dcompiler"
    }

    filter "configurations:Debug"
        defines { "CC_DEBUG" } 
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines { "CC_RELEASE" }
        runtime "Release"
        optimize "On"

-- 2. 게임 프로젝트 (Sandbox)
project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("bin/" .. outputdir)
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "%{prj.name}/src/**.h",
        "%{prj.name}/src/**.cpp"
    }

    includedirs {
        "CCEngine/src",
        "CCEngine/vendor/spdlog/include"
    }

    links {
        "CCEngine"
    }

    filter "configurations:Debug"
        defines { "CC_DEBUG" }
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines { "CC_RELEASE" }
        runtime "Release"
        optimize "On"
