param(
    [string]$Project = "Sandbox"
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Project))
$runId = [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmssfff")
$matrixRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local\qa\build-matrix\run-$runId"))
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "local\qa"))
if (-not $matrixRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "검증 출력이 local/qa 밖을 가리킵니다: $matrixRoot"
}

New-Item -ItemType Directory -Force -Path $matrixRoot | Out-Null

$results = @()
foreach ($configuration in @("Debug", "Release")) {
    & (Join-Path $PSScriptRoot "BuildEngine.ps1") -Configuration $configuration
    if ($LASTEXITCODE -ne 0) { throw "$configuration Editor/도구 빌드 실패" }

    $binaryRoot = Join-Path $repoRoot "bin\$configuration-windows-x86_64"
    $playerConfiguration = if ($configuration -eq "Release") { "Release" } else { "Development" }
    $output = Join-Path $matrixRoot $playerConfiguration
    $builder = Join-Path $binaryRoot "CCBuild.exe"
    & $builder "--project=$projectRoot" "--binaries=$binaryRoot" "--output=$output" "--configuration=$playerConfiguration"
    $exitCode = $LASTEXITCODE
    $results += [ordered]@{
        NativeConfiguration = $configuration
        PlayerConfiguration = $playerConfiguration
        Success = $exitCode -eq 0
        Output = $output
    }
    if ($exitCode -ne 0) { throw "$playerConfiguration Player 패키지/첫 프레임 검증 실패" }
}

# 게시된 Release 폴더를 다시 옮겨 실행한다. 개발 프로젝트의 현재 경로를 우연히 읽는 문제를 잡는다.
$releaseOutput = Join-Path $matrixRoot "Release"
$relocated = Join-Path $matrixRoot "RelocatedRelease"
Copy-Item -LiteralPath $releaseOutput -Destination $relocated -Recurse
$smokeReport = Join-Path $matrixRoot "relocated-smoke.json"
$player = Join-Path $relocated "Game.exe"
$process = Start-Process -FilePath $player -ArgumentList "--smoke-report=`"$smokeReport`"" -WorkingDirectory $relocated -Wait -PassThru -WindowStyle Hidden
$smoke = if (Test-Path -LiteralPath $smokeReport) { Get-Content -LiteralPath $smokeReport -Raw | ConvertFrom-Json } else { $null }
$relocationSuccess = $process.ExitCode -eq 0 -and $null -ne $smoke -and $smoke.Success
$results += [ordered]@{
    NativeConfiguration = "Release"
    PlayerConfiguration = "RelocatedRelease"
    Success = $relocationSuccess
    Output = $relocated
}
if (-not $relocationSuccess) { throw "이동된 Release Player 첫 프레임 검증 실패" }

$matrixReport = [ordered]@{
    TimestampUtc = [DateTime]::UtcNow.ToString("o")
    Success = $true
    Results = $results
}
$matrixReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $matrixRoot "matrix-report.json") -Encoding UTF8
Write-Host "[PASS] Debug Editor / Release Editor / Development Player / Release Player / Relocated Release"
