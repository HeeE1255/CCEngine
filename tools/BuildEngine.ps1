param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("x64")]
    [string]$Platform = "x64",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
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
    ProcessArchitecture = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
}
$toolchainReport | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot "toolchain-$Configuration.json") -Encoding UTF8

$target = if ($Clean) { "Rebuild" } else { "Build" }
& $msbuild $solution "/t:$target" "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:PreferredToolArchitecture=x64" "/m:1" "/v:minimal"
if ($LASTEXITCODE -ne 0) {
    throw "CCEngine $Configuration $Platform 빌드 실패: exit $LASTEXITCODE"
}

Write-Host "[PASS] $Configuration $Platform - $msbuild ($version)"
