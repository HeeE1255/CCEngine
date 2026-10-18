param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,
    [switch]$AllowDevelopmentBuild
)

$ErrorActionPreference = "Stop"
$root = [System.IO.Path]::GetFullPath($BuildRoot)
$manifestPath = Join-Path $root "BuildManifest.json"
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "BuildManifest.json이 없습니다: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.Version -lt 3) { throw "BuildManifest Version 3 이상이 필요합니다." }
if ($manifest.DevelopmentBuild -and -not $AllowDevelopmentBuild) { throw "Release 후보가 Development Build입니다." }
if ([string]::IsNullOrWhiteSpace($manifest.ProductVersion)) { throw "ProductVersion이 비어 있습니다." }
if ([string]::IsNullOrWhiteSpace($manifest.BuildFingerprint)) { throw "BuildFingerprint가 비어 있습니다." }

if (-not ("CCEngine.ReleaseHash" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.IO;
namespace CCEngine {
    public static class ReleaseHash {
        public static string Fnv1A64(string path) {
            ulong hash = 14695981039346656037UL;
            byte[] buffer = new byte[65536];
            using (FileStream stream = File.OpenRead(path)) {
                int count;
                while ((count = stream.Read(buffer, 0, buffer.Length)) > 0) {
                    for (int i = 0; i < count; ++i) { hash ^= buffer[i]; hash *= 1099511628211UL; }
                }
            }
            return hash.ToString("x16");
        }
        public static string Fnv1A64Text(string text) {
            ulong hash = 14695981039346656037UL;
            foreach (byte value in System.Text.Encoding.UTF8.GetBytes(text)) {
                hash ^= value;
                hash *= 1099511628211UL;
            }
            return hash.ToString("x16");
        }
    }
}
"@
}

$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$totalBytes = [uint64]0
$buildKind = if ($manifest.DevelopmentBuild) { 'Development' } else { 'Release' }
$fingerprintInput = "$($manifest.ProductName)`n$($manifest.ProductVersion)`n$($manifest.UpdateChannel)`n$buildKind`n"
foreach ($file in $manifest.Files) {
    $relative = [string]$file.Path
    if ([System.IO.Path]::IsPathRooted($relative) -or ($relative -split '[\\/]' -contains '..')) {
        throw "Manifest에 안전하지 않은 경로가 있습니다: $relative"
    }
    if (-not $seen.Add($relative)) { throw "Manifest 중복 경로: $relative" }
    $fullPath = [System.IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $fullPath.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "빌드 루트 밖의 경로입니다: $relative"
    }
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { throw "배포 파일 누락: $relative" }
    $actualSize = (Get-Item -LiteralPath $fullPath).Length
    if ($actualSize -ne [int64]$file.Size) { throw "파일 크기 불일치: $relative" }
    $actualHash = [CCEngine.ReleaseHash]::Fnv1A64($fullPath)
    if ($actualHash -ne [string]$file.Hash) { throw "파일 해시 불일치: $relative" }
    $totalBytes += [uint64]$actualSize
    $fingerprintInput += "$relative`n$actualSize`n$actualHash`n"
}

if ($seen.Count -ne [int]$manifest.FileCount -or $totalBytes -ne [uint64]$manifest.TotalBytes) {
    throw "Manifest 합계가 실제 파일 목록과 일치하지 않습니다."
}

$actualFingerprint = [CCEngine.ReleaseHash]::Fnv1A64Text($fingerprintInput)
if ($actualFingerprint -ne [string]$manifest.BuildFingerprint) {
    throw "BuildFingerprint 불일치: manifest=$($manifest.BuildFingerprint), calculated=$actualFingerprint"
}

$unexpected = Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $relative = [System.IO.Path]::GetRelativePath($root, $_.FullName).Replace([char]92, [char]47)
    $relative -notin @('BuildManifest.json', '.ccengine-build') -and -not $seen.Contains($relative)
}
if ($unexpected) { throw "Manifest에 없는 파일이 있습니다: $($unexpected[0].FullName)" }

Write-Host "[PASS] Release Candidate 검증: $($manifest.ProductName) $($manifest.ProductVersion) [$($manifest.UpdateChannel)]"
Write-Host "       Files=$($seen.Count), Bytes=$totalBytes, Fingerprint=$($manifest.BuildFingerprint)"

