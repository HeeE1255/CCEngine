param(
    [Parameter(Mandatory = $true)] [string]$InstallRoot,
    [Parameter(Mandatory = $true)] [string]$PatchRoot
)

$ErrorActionPreference = 'Stop'
$install = [System.IO.Path]::GetFullPath($InstallRoot)
$patch = [System.IO.Path]::GetFullPath($PatchRoot)
$installMarker = Join-Path $install '.ccengine-build'
if (-not (Test-Path -LiteralPath $installMarker -PathType Leaf)) {
    throw "CCEngine 소유권 마커가 없는 설치 폴더는 수정하지 않습니다: $install"
}
$currentManifest = Get-Content -LiteralPath (Join-Path $install 'BuildManifest.json') -Raw | ConvertFrom-Json
$patchManifest = Get-Content -LiteralPath (Join-Path $patch 'PatchManifest.json') -Raw | ConvertFrom-Json
if ($currentManifest.BuildFingerprint -ne $patchManifest.BaseFingerprint) {
    throw "설치된 빌드가 패치 기준 버전과 다릅니다. 현재=$($currentManifest.BuildFingerprint), 필요=$($patchManifest.BaseFingerprint)"
}

$staging = "$install.patch-staging"
$backup = "$install.patch-backup"
foreach ($temporary in @($staging, $backup)) {
    if (Test-Path -LiteralPath $temporary) {
        if (-not (Test-Path -LiteralPath (Join-Path $temporary '.ccengine-build') -PathType Leaf)) {
            throw "소유권 마커가 없는 임시 폴더는 삭제하지 않습니다: $temporary"
        }
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $staging | Out-Null
Set-Content -LiteralPath (Join-Path $staging '.ccengine-build') -Value 'CCEngine game build' -Encoding ascii
Get-ChildItem -LiteralPath $install -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $staging -Recurse -Force
}

function Resolve-SafePatchPath([string]$root, [string]$relative) {
    if ([System.IO.Path]::IsPathRooted($relative) -or ($relative -split '[\\/]' -contains '..')) {
        throw "안전하지 않은 패치 경로: $relative"
    }
    $resolved = [System.IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $resolved.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "패치 경로가 설치 폴더 밖을 가리킵니다: $relative"
    }
    return $resolved
}

foreach ($operation in $patchManifest.Operations) {
    $destination = Resolve-SafePatchPath $staging ([string]$operation.Path)
    if ($operation.Operation -eq 'Delete') {
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        continue
    }
    if ($operation.Operation -notin 'Add', 'Replace') { throw "지원하지 않는 패치 작업: $($operation.Operation)" }
    $source = Resolve-SafePatchPath (Join-Path $patch 'Files') ([string]$operation.Path)
    New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $patch 'TargetBuildManifest.json') -Destination (Join-Path $staging 'BuildManifest.json') -Force

# staging 전체를 검증한 뒤에만 설치 폴더 이름을 바꾼다. 중간 실패는 실행 가능한 기존 버전을 건드리지 않는다.
& (Join-Path $PSScriptRoot 'VerifyReleaseCandidate.ps1') -BuildRoot $staging
Rename-Item -LiteralPath $install -NewName ([System.IO.Path]::GetFileName($backup))
try {
    Rename-Item -LiteralPath $staging -NewName ([System.IO.Path]::GetFileName($install))
} catch {
    Rename-Item -LiteralPath $backup -NewName ([System.IO.Path]::GetFileName($install))
    throw
}
if (-not (Test-Path -LiteralPath (Join-Path $backup '.ccengine-build') -PathType Leaf)) {
    throw "교체 후 백업 소유권 검증에 실패했습니다: $backup"
}
Remove-Item -LiteralPath $backup -Recurse -Force
Write-Host "[PASS] Patch applied: $($patchManifest.BaseVersion) -> $($patchManifest.TargetVersion)"
