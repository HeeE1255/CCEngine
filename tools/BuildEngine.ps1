param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("x64")]
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$EnableDX12,
    [switch]$EnableOpenGL,
    [switch]$EnableVulkan,
    [string]$VulkanSDK = $env:VULKAN_SDK
)

$ErrorActionPreference = "Stop"
if ($EnableVulkan) {
    # SDK 누락은 컴파일을 시작하기 전에 알린다. DX11/12와 OpenGL만 빌드할 때는 SDK가 필요 없다.
    if ([string]::IsNullOrWhiteSpace($VulkanSDK)) {
        throw "Vulkan 빌드에는 -VulkanSDK 경로 또는 VULKAN_SDK 환경 변수가 필요합니다."
    }
    $VulkanSDK = [System.IO.Path]::GetFullPath($VulkanSDK)
    if ($VulkanSDK.Contains(';') -or $VulkanSDK.Contains('"')) {
        throw "Vulkan SDK 경로에 MSBuild 속성 구분 문자를 사용할 수 없습니다."
    }
    foreach ($requiredFile in @('Include\vulkan\vulkan.h', 'Lib\vulkan-1.lib')) {
        if (-not (Test-Path -LiteralPath (Join-Path $VulkanSDK $requiredFile) -PathType Leaf)) {
            throw "Vulkan SDK 필수 파일이 없습니다: $requiredFile ($VulkanSDK)"
        }
    }
}
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$solution = Join-Path $repoRoot "CCEngine.sln"
$reportRoot = Join-Path $repoRoot "local\build-reports"
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $installations = @()
    if (Test-Path -LiteralPath $vswhere) {
        $installations = & $vswhere -products * -requires Microsoft.Component.MSBuild -property installationPath
    }

    # vswhere가 없는 개발 PC도 지원하되, x86 Host MSBuild로 조용히 후퇴하지 않는다.
    $installations += Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "Community" }

    $candidates = @()
    foreach ($installation in $installations | Select-Object -Unique) {
        $candidate = Join-Path $installation "MSBuild\Current\Bin\amd64\MSBuild.exe"
        if (Test-Path -LiteralPath $candidate) {
            $versionText = (& $candidate -version -nologo | Select-Object -Last 1).Trim()
            $versionMatch = [regex]::Match($versionText, '^\d+(\.\d+){1,3}')
            if ($versionMatch.Success) {
                $candidates += [pscustomobject]@{
                    Path = [System.IO.Path]::GetFullPath($candidate)
                    Version = [version]$versionMatch.Value
                }
            }
        }
    }
    if ($candidates.Count -gt 0) {
        return ($candidates | Sort-Object Version -Descending | Select-Object -First 1).Path
    }
    throw "amd64 MSBuild를 찾지 못했습니다. Visual Studio C++ 빌드 도구를 설치해야 합니다."
}

$msbuild = Find-MSBuild
$version = (& $msbuild -version -nologo | Select-Object -Last 1).Trim()
$toolchainReport = [ordered]@{
    TimestampUtc = [DateTime]::UtcNow.ToString("o")
    MSBuild = $msbuild
    MSBuildVersion = $version
    Configuration = $Configuration
    Platform = $Platform
    EnableDX12 = [bool]$EnableDX12
    EnableOpenGL = [bool]$EnableOpenGL
    EnableVulkan = [bool]$EnableVulkan
    VulkanSDK = if ($EnableVulkan) { $VulkanSDK } else { $null }
    ProcessArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
}
$toolchainReport | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot "toolchain-$Configuration.json") -Encoding UTF8

$target = if ($Clean) { "Rebuild" } else { "Build" }
$buildArguments = @($solution, "/t:$target", "/p:Configuration=$Configuration", "/p:Platform=$Platform",
    "/p:PreferredToolArchitecture=x64", "/p:CCEnableDX12=$($EnableDX12.IsPresent.ToString().ToLowerInvariant())",
    "/p:CCEnableOpenGL=$($EnableOpenGL.IsPresent.ToString().ToLowerInvariant())",
    "/p:CCEnableVulkan=$($EnableVulkan.IsPresent.ToString().ToLowerInvariant())", "/m:1", "/v:minimal")
if ($EnableVulkan) { $buildArguments += "/p:CCVulkanSDK=$VulkanSDK" }
& $msbuild @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "CCEngine $Configuration $Platform 빌드 실패: exit $LASTEXITCODE"
}

Write-Host "[PASS] $Configuration $Platform - $msbuild ($version)"
