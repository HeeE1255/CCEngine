param(
    [Parameter(Mandatory = $true)] [string]$BaseBuildRoot,
    [Parameter(Mandatory = $true)] [string]$TargetBuildRoot,
    [Parameter(Mandatory = $true)] [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$baseRoot = [System.IO.Path]::GetFullPath($BaseBuildRoot)
$targetRoot = [System.IO.Path]::GetFullPath($TargetBuildRoot)
$patchRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$patchMarker = Join-Path $patchRoot '.ccengine-patch'
$base = Get-Content -LiteralPath (Join-Path $baseRoot 'BuildManifest.json') -Raw | ConvertFrom-Json
$target = Get-Content -LiteralPath (Join-Path $targetRoot 'BuildManifest.json') -Raw | ConvertFrom-Json
if ($base.ProductName -ne $target.ProductName) { throw "서로 다른 제품 사이에는 패치를 만들 수 없습니다." }
if ($base.UpdateChannel -ne $target.UpdateChannel) { throw "업데이트 채널이 다릅니다." }
if ($base.BuildFingerprint -eq $target.BuildFingerprint) { throw "두 빌드가 동일합니다." }

$baseFiles = @{}
foreach ($file in $base.Files) { $baseFiles[[string]$file.Path] = $file }
$targetFiles = @{}
foreach ($file in $target.Files) { $targetFiles[[string]$file.Path] = $file }

if (Test-Path -LiteralPath $patchRoot) {
    if (-not (Test-Path -LiteralPath $patchMarker -PathType Leaf)) {
        throw "CCEngine 소유권 마커가 없는 폴더는 삭제하지 않습니다: $patchRoot"
    }
    Remove-Item -LiteralPath $patchRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $patchRoot 'Files') | Out-Null
Set-Content -LiteralPath $patchMarker -Value 'CCEngine delta patch staging' -Encoding ascii
$operations = @()
foreach ($path in ($targetFiles.Keys | Sort-Object)) {
    $targetFile = $targetFiles[$path]
    $kind = if (-not $baseFiles.ContainsKey($path)) { 'Add' } elseif ($baseFiles[$path].Hash -ne $targetFile.Hash) { 'Replace' } else { $null }
    if (-not $kind) { continue }
    $destination = Join-Path (Join-Path $patchRoot 'Files') $path
    New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $targetRoot $path) -Destination $destination
    $operations += [ordered]@{ Operation=$kind; Path=$path; Size=$targetFile.Size; Hash=$targetFile.Hash }
}
foreach ($path in ($baseFiles.Keys | Sort-Object)) {
    if (-not $targetFiles.ContainsKey($path)) {
        $operations += [ordered]@{ Operation='Delete'; Path=$path }
    }
}

$patch = [ordered]@{
    Version = 1
    ProductName = $target.ProductName
    UpdateChannel = $target.UpdateChannel
    BaseVersion = $base.ProductVersion
    TargetVersion = $target.ProductVersion
    BaseFingerprint = $base.BuildFingerprint
    TargetFingerprint = $target.BuildFingerprint
    Operations = $operations
}
$patch | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $patchRoot 'PatchManifest.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $targetRoot 'BuildManifest.json') -Destination (Join-Path $patchRoot 'TargetBuildManifest.json')
Write-Host "[PASS] Delta patch staged: $patchRoot ($($operations.Count) operations)"



