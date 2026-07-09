$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $scriptRoot "Build"
$coreSource = Join-Path $scriptRoot "ScriptCore\ScriptCore.cs"
$gameSources = Get-ChildItem (Join-Path $scriptRoot "Game") -Filter *.cs -Recurse
$csc = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
$runtimeRoot = "C:\Program Files\dotnet\shared\Microsoft.NETCore.App"

if (-not (Test-Path $csc)) {
    throw "Visual Studio C# compiler was not found."
}

$runtimeVersion = Get-ChildItem $runtimeRoot -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if (-not $runtimeVersion) {
    throw ".NET 8 x64 runtime was not found."
}

New-Item -ItemType Directory -Force $buildRoot | Out-Null
$references = Get-ChildItem $runtimeVersion.FullName -Filter *.dll |
    Where-Object {
        ($_.Name -like "System.*.dll" -and $_.Name -ne "System.IO.Compression.Native.dll") -or
        $_.Name -in @("System.Private.CoreLib.dll", "mscorlib.dll", "netstandard.dll")
    } |
    ForEach-Object { "/reference:$($_.FullName)" }

# SDK가 없는 개발 PC에서도 같은 참조 어셈블리 집합으로 재현 가능하게 컴파일 옵션을 고정한다.
& $csc /nologo /target:library /unsafe /langversion:latest /nostdlib+ `
    @references "/out:$buildRoot\CCEngine.ScriptCore.dll" $coreSource
if ($LASTEXITCODE -ne 0) { throw "Failed to build CCEngine.ScriptCore.dll." }

& $csc /nologo /target:library /langversion:latest /nostdlib+ `
    @references "/reference:$buildRoot\CCEngine.ScriptCore.dll" `
    "/out:$buildRoot\GameScripts.dll" @($gameSources.FullName)
if ($LASTEXITCODE -ne 0) { throw "Failed to build GameScripts.dll." }

Write-Host "C# scripts built: $buildRoot"
