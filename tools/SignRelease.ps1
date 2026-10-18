param(
    [Parameter(Mandatory = $true)] [string]$BuildRoot,
    [Parameter(Mandatory = $true)] [string]$CertificateThumbprint,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath($BuildRoot)
$manifestPath = Join-Path $root 'BuildManifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.DevelopmentBuild) { throw 'Development Build에는 출시 서명을 적용하지 않습니다.' }

$certificate = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Thumbprint -eq ($CertificateThumbprint -replace '\s', '').ToUpperInvariant() } |
    Select-Object -First 1
if (-not $certificate) { throw '지정한 코드 서명 인증서를 Windows 인증서 저장소에서 찾지 못했습니다.' }
if (-not $certificate.HasPrivateKey) { throw '코드 서명 인증서에 개인 키가 없습니다.' }
if ($certificate.NotAfter -le (Get-Date)) { throw '코드 서명 인증서가 만료되었습니다.' }

$kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$signtool = Get-ChildItem -LiteralPath $kitsRoot -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $signtool) { throw 'Windows SDK x64 signtool.exe를 찾지 못했습니다.' }

$targets = Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' }
if (-not $targets) { throw '서명할 실행 파일이 없습니다.' }
foreach ($target in $targets) {
    & $signtool.FullName sign /sha1 $certificate.Thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $target.FullName
    if ($LASTEXITCODE -ne 0) { throw "코드 서명 실패: $($target.FullName)" }
    & $signtool.FullName verify /pa /all $target.FullName
    if ($LASTEXITCODE -ne 0) { throw "서명 검증 실패: $($target.FullName)" }
}

if (-not ('CCEngine.ReleaseHash' -as [type])) {
    Add-Type -TypeDefinition @"
using System; using System.IO;
namespace CCEngine { public static class ReleaseHash {
    public static string Fnv1A64(string path) { ulong h=14695981039346656037UL; byte[] b=new byte[65536];
        using(FileStream s=File.OpenRead(path)){ int n; while((n=s.Read(b,0,b.Length))>0) for(int i=0;i<n;++i){h^=b[i];h*=1099511628211UL;} }
        return h.ToString("x16"); }
    public static string Fnv1A64Text(string text) { ulong h=14695981039346656037UL;
        foreach(byte v in System.Text.Encoding.UTF8.GetBytes(text)){h^=v;h*=1099511628211UL;} return h.ToString("x16"); }
} }
"@
}

$fingerprint = "$($manifest.ProductName)`n$($manifest.ProductVersion)`n$($manifest.UpdateChannel)`nRelease`n"
[uint64]$totalBytes = 0
foreach ($file in $manifest.Files) {
    $fullPath = Join-Path $root ([string]$file.Path)
    $file.Size = (Get-Item -LiteralPath $fullPath).Length
    $file.Hash = [CCEngine.ReleaseHash]::Fnv1A64($fullPath)
    $totalBytes += [uint64]$file.Size
    $fingerprint += "$($file.Path)`n$($file.Size)`n$($file.Hash)`n"
}
$manifest.TotalBytes = $totalBytes
$manifest.BuildFingerprint = [CCEngine.ReleaseHash]::Fnv1A64Text($fingerprint)
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "[PASS] $($targets.Count)개 바이너리 서명 및 Manifest 갱신 완료"
