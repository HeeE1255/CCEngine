$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $scriptRoot "Build"
$coreSource = Join-Path $scriptRoot "ScriptCore\ScriptCore.cs"
$gameSources = Get-ChildItem (Join-Path $scriptRoot "Game") -Filter *.cs -Recurse
$csc = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
$dotnet = "C:\Program Files\dotnet\dotnet.exe"
$runtimeRoot = "C:\Program Files\dotnet\shared\Microsoft.NETCore.App"

if (-not (Test-Path $csc)) {
    throw "Visual Studio C# compiler was not found."
}
if (-not (Test-Path $dotnet)) {
    throw ".NET host was not found."
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

$manifestSource = Join-Path $buildRoot "ManifestGenerator.cs"
$manifestExe = Join-Path $buildRoot "ManifestGenerator.exe"
$manifestRuntimeConfig = Join-Path $buildRoot "ManifestGenerator.runtimeconfig.json"
$manifestPath = Join-Path $buildRoot "GameScripts.manifest.json"

@'
#nullable enable
using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Collections.Generic;
using CCEngine;

static string FieldTypeName(Type type)
{
    if (type == typeof(float)) return "float";
    if (type == typeof(int)) return "int";
    if (type == typeof(bool)) return "bool";
    if (type == typeof(string)) return "string";
    if (type == typeof(Vector3)) return "Vector3";
    return string.Empty;
}

static object? ReadDefaultValue(Type scriptType, FieldInfo field)
{
    object? instance = null;
    try { instance = Activator.CreateInstance(scriptType); }
    catch { }

    object? value = instance != null ? field.GetValue(instance) : null;
    if (value is Vector3 vector)
    {
        return new Dictionary<string, float>
        {
            ["X"] = vector.X,
            ["Y"] = vector.Y,
            ["Z"] = vector.Z
        };
    }
    return value;
}

static object ReadEditorInfo(FieldInfo field)
{
    object? range = field.GetCustomAttributes(false).FirstOrDefault(attribute => attribute.GetType().Name == "RangeAttribute");
    object? drag = field.GetCustomAttributes(false).FirstOrDefault(attribute => attribute.GetType().Name == "DragAttribute");
    object? step = field.GetCustomAttributes(false).FirstOrDefault(attribute => attribute.GetType().Name == "StepAttribute");
    bool readOnly = field.GetCustomAttributes(false).Any(attribute => attribute.GetType().Name == "ReadOnlyAttribute");

    string display = "Input";
    float? min = null;
    float? max = null;
    float? stepValue = null;

    if (range != null)
    {
        display = "Range";
        min = Convert.ToSingle(range.GetType().GetProperty("Min")!.GetValue(range), CultureInfo.InvariantCulture);
        max = Convert.ToSingle(range.GetType().GetProperty("Max")!.GetValue(range), CultureInfo.InvariantCulture);
    }
    else if (step != null)
    {
        display = "Step";
        stepValue = Convert.ToSingle(step.GetType().GetProperty("Value")!.GetValue(step), CultureInfo.InvariantCulture);
    }
    else if (drag != null)
    {
        display = "Drag";
        stepValue = Convert.ToSingle(drag.GetType().GetProperty("Speed")!.GetValue(drag), CultureInfo.InvariantCulture);
    }

    if (readOnly)
        display = "ReadOnly";

    return new { Display = display, Min = min, Max = max, Step = stepValue, ReadOnly = readOnly };
}

string outputPath = args[0];
string scriptAssemblyPath = args[1];
Assembly assembly = Assembly.LoadFrom(scriptAssemblyPath);

var classes = assembly.GetTypes()
    .Where(type => !type.IsAbstract && typeof(GameScript).IsAssignableFrom(type))
    .OrderBy(type => type.FullName)
    .Select(type => new
    {
        ClassName = type.FullName ?? type.Name,
        Fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public)
            .Where(field => !field.IsInitOnly && !field.IsLiteral)
            .Select(field => new
            {
                Name = field.Name,
                Type = FieldTypeName(field.FieldType),
                DefaultValue = ReadDefaultValue(type, field),
                Editor = ReadEditorInfo(field)
            })
            .Where(field => !string.IsNullOrWhiteSpace(field.Type))
            .OrderBy(field => field.Name)
            .ToArray()
    })
    .ToArray();

var options = new JsonSerializerOptions { WriteIndented = true };
File.WriteAllText(outputPath, JsonSerializer.Serialize(new { Classes = classes }, options), new UTF8Encoding(false));
'@ | Set-Content -LiteralPath $manifestSource -Encoding UTF8

@"
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "$($runtimeVersion.Name)"
    }
  }
}
"@ | Set-Content -LiteralPath $manifestRuntimeConfig -Encoding UTF8

# 스크립트 DLL을 직접 읽어서 필드 목록을 만든다. 소스 텍스트를 파싱하지 않아 실제 컴파일 결과와 항상 맞는다.
& $csc /nologo /target:exe /langversion:latest /nostdlib+ `
    @references "/reference:$buildRoot\CCEngine.ScriptCore.dll" `
    "/out:$manifestExe" $manifestSource
if ($LASTEXITCODE -ne 0) { throw "Failed to build the C# script manifest generator." }

& $dotnet $manifestExe $manifestPath "$buildRoot\GameScripts.dll"
if ($LASTEXITCODE -ne 0) { throw "Failed to write GameScripts.manifest.json." }

Write-Host "C# scripts built: $buildRoot"
