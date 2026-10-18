param(
    [ValidateRange(1, 50)]
    [int]$Runs = 10,
    [string]$Project = "Sandbox"
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Project))
$binaryRoot = Join-Path $repoRoot "bin\Release-windows-x86_64"
$builder = Join-Path $binaryRoot "CCBuild.exe"
$runId = [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmssfff")
$output = Join-Path $repoRoot "local\qa\build-stability\run-$runId"
$results = @()
$firstManifestHash = $null

& (Join-Path $PSScriptRoot "BuildEngine.ps1") -Configuration Release
if ($LASTEXITCODE -ne 0) { throw "Release native 빌드 실패" }

for ($run = 1; $run -le $Runs; $run++) {
    $started = [DateTime]::UtcNow
    & $builder "--project=$projectRoot" "--binaries=$binaryRoot" "--output=$output" "--configuration=Release"
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw "Release 안정성 빌드 $run/$Runs 실패" }

    $manifest = Join-Path $output "BuildManifest.json"
    $manifestHash = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash
    if ($null -eq $firstManifestHash) { $firstManifestHash = $manifestHash }
    if ($manifestHash -ne $firstManifestHash) {
        throw "입력이 같은데 BuildManifest가 $run 회차에서 달라졌습니다."
    }

    $results += [ordered]@{
        Run = $run
        Success = $true
        ElapsedMilliseconds = [int]([DateTime]::UtcNow - $started).TotalMilliseconds
        ManifestSha256 = $manifestHash
    }
}

$report = [ordered]@{
    TimestampUtc = [DateTime]::UtcNow.ToString("o")
    Success = $true
    Runs = $Runs
    Output = $output
    Results = $results
}
$reportPath = Join-Path $output "StabilityReport.json"
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "[PASS] Release Clean/Incremental 안정성 $Runs/$Runs, manifest=$firstManifestHash"
