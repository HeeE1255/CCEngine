param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,
    [string]$OutputDirectory = "outputs\packages"
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$root = [System.IO.Path]::GetFullPath($BuildRoot)
& (Join-Path $PSScriptRoot "VerifyReleaseCandidate.ps1") -BuildRoot $root

$manifest = Get-Content -LiteralPath (Join-Path $root "BuildManifest.json") -Raw | ConvertFrom-Json
$safeName = ([string]$manifest.ProductName -replace '[^A-Za-z0-9._-]', '_').Trim('_')
if ([string]::IsNullOrWhiteSpace($safeName)) { $safeName = "CCEngineGame" }
$packageRoot = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
$baseName = "$safeName-$($manifest.ProductVersion)-$($manifest.UpdateChannel)-win-x64"
$archive = Join-Path $packageRoot "$baseName.zip"
$checksum = "$archive.sha256"
Remove-Item -LiteralPath $archive, $checksum -Force -ErrorAction SilentlyContinue

# ZIP 내부에 출력 폴더 이름을 넣지 않아 설치 위치가 달라도 동일한 구조로 풀린다.
Compress-Archive -Path (Join-Path $root '*') -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([System.IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath $checksum -Encoding ascii
Write-Host "[PASS] Portable package: $archive"
Write-Host "       SHA256: $hash"

