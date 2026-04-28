workspace "CCEngine"
    architecture "x64"
    startproject "Sandbox"

    configurations { "Debug", "Release", "Dist" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- ============================================================================
-- [공통] 
-- ============================================================================
filter "system:windows"
    systemversion "latest"
    defines { "CC_PLATFORM_WINDOWS" }
    buildoptions { "/utf-8" }
filter {}

-- ============================================================================
-- 1. 외부 라이브러리 (CCEngine/vendor 폴더 내부)
-- ============================================================================
project "Box2D"
    location "CCEngine/vendor/box2d"
    kind "StaticLib"
    language "C"
    cdialect "C17"
    cppdialect "C++20"
    staticruntime "off"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
    
    files { 
        "CCEngine/vendor/box2d/**.cpp",
        "CCEngine/vendor/box2d/**.c",
        "CCEngine/vendor/box2d/**.h"
    }
    includedirs { "CCEngine/vendor/box2d/include" }

    filter "configurations:Debug"
        defines { "CC_DEBUG" } 
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines { "CC_RELEASE" }
        runtime "Release"
        optimize "On"
        
    filter "configurations:Dist"
        defines { "CC_DIST" }
        runtime "Release"
        optimize "On"
    filter {}

-- ============================================================================
-- 2. 엔진 프로젝트 (CCEngine)
-- ============================================================================
project "CCEngine"
    location "CCEngine"
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("bin/" .. outputdir)
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    defines { 
        "CC_BUILD_DLL", 
        "_CRT_SECURE_NO_WARNINGS"
    } 

    files {
        "CCEngine/src/**.h",
        "CCEngine/src/**.cpp",
	"CCEngine/vendor/stb_truetype/**.h",
        "CCEngine/vendor/stb_truetype/**.cpp",
    }

    includedirs {
        "CCEngine/src",
        "CCEngine/vendor/spdlog/include",
        "CCEngine/vendor/box2d/include",
        "CCEngine/vendor/Assimp/include",
	"CCEngine/vendor/stb_truetype"
    }

    -- [Assimp 추가] 라이브러리(.lib) 파일이 있는 폴더 경로
    libdirs {
        "CCEngine/vendor/Assimp/lib" 
    }

    links {
        "d3d11",
        "dxgi",
        "d3dcompiler",
        "Box2D",
        "assimp-vc145-mt.lib" -- [Assimp 추가] 라이브러리 링크 (파일명이 일치하는지 꼭 확인하세요!)
    }

    postbuildcommands {
	("{COPY} %{prj.location}/vendor/Assimp/bin/*.dll %{cfg.targetdir}")
    }

    filter "configurations:Debug"
        defines { "CC_DEBUG" } 
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines { "CC_RELEASE" }
        runtime "Release"
        optimize "On"
        
    filter "configurations:Dist"
        defines { "CC_DIST" }
        runtime "Release"
        optimize "On"
    filter {}

-- ============================================================================
-- 3. 게임 프로젝트 (Sandbox)
-- ============================================================================
project "Sandbox"
    location "Sandbox"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("bin/" .. outputdir)
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "Sandbox/src/**.h",
        "Sandbox/src/**.cpp"
    }

    includedirs {
        "CCEngine/src",
        "CCEngine/vendor/spdlog/include",
        "CCEngine/vendor/box2d/include",
        "CCEngine/vendor/Assimp/include"
    }

    defines {        
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

    filter "configurations:Dist"
        defines { "CC_DIST" }
        runtime "Release"
        optimize "On"
    filter {}