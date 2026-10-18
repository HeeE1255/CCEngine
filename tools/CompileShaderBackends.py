"""Build dormant RHI shader packages. Requires DXC and SPIRV-Cross on PATH.

No engine settings, HLSL sources, or existing DX11 caches are modified.
Example:
  python tools/CompileShaderBackends.py --source-root Sandbox/assets/shaders
      --output local/shader-packages --dxc C:/Tools/dxc.exe
      --spirv-cross C:/Tools/spirv-cross.exe

Binding flags follow DXC docs/SPIR-V.rst; GLSL conversion uses the
SPIRV-Cross combined-samplers-inherit-bindings option. This is an offline
conversion, not a driver compile, runtime rendering test, or engine build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import uuid


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tool_path(value):
    found = shutil.which(value)
    if not found:
        raise RuntimeError(f"Required tool not found: {value}. Supply its executable path.")
    return Path(found).resolve()


def run(argv, log, timeout):
    # 문자열 명령 조합을 피해야 공백 경로나 셰이더 define이 셸 명령으로 해석되지 않는다.
    try:
        result = subprocess.run([str(arg) for arg in argv], capture_output=True,
                                timeout=timeout, shell=False)
    except subprocess.TimeoutExpired as exc:
        log.write_bytes((exc.stdout or b"") + (exc.stderr or b"") + b"\nTIMEOUT\n")
        raise RuntimeError(f"Compiler timeout. See {log}") from exc
    log.write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"Tool exited with {result.returncode}. See {log}")
    return result.stdout


def validate_reflection(reflection, stage):
    forbidden = ("ssbos", "images", "subpass_inputs", "push_constants", "acceleration_structures", "textures")
    for category in forbidden:
        if reflection.get(category):
            raise ValueError(f"Current RHI does not support {category}")
    seen = set()
    for category, low, high in (("ubos", 0, 8), ("separate_images", 8, 40), ("separate_samplers", 40, 42)):
        for resource in reflection.get(category, []):
            binding = resource.get("binding", -1)
            if resource.get("set", -1) != 0 or not low <= binding < high:
                raise ValueError(f"Resource outside common slot contract: {resource}")
            if resource.get("array"):
                raise ValueError(f"Resource array was not flattened: {resource['name']}")
            if binding in seen:
                raise ValueError(f"Duplicate resource binding {binding}")
            seen.add(binding)
            if stage == "vertex" and category != "ubos":
                raise ValueError("Current RHI supports texture sampling only in pixel shaders")
            if category == "ubos" and resource.get("block_size", 0) > 65536:
                raise ValueError("Constant buffer exceeds 64 KiB")
            if category == "separate_images" and resource.get("type") not in ("texture2D", "itexture2D", "utexture2D"):
                raise ValueError(f"Only non-multisampled Texture2D is supported: {resource}")


def gl_intermediate(source, destination):
    data = source.read_bytes()
    if len(data) < 20 or len(data) % 4:
        raise ValueError("Invalid SPIR-V size")
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V magic")
    offset = 5
    while offset < len(words):
        length, opcode = words[offset] >> 16, words[offset] & 0xffff
        if not length or offset + length > len(words):
            raise ValueError("Malformed SPIR-V instruction")
        # OpDecorate / Binding. GL은 UBO와 texture unit의 번호 공간이 별개다.
        # Vulkan의 tN -> 8+N 이동만 되돌린다. 이 중간 파일은 Vulkan에서 실행하지 않는다.
        if opcode == 71 and length == 4 and words[offset + 2] == 33:
            binding = words[offset + 3]
            if 8 <= binding < 40:
                words[offset + 3] -= 8
        offset += length
    destination.write_bytes(struct.pack(f"<{len(words)}I", *words))


def compile_source(source, root, folder, args, dxc, cross):
    folder.mkdir(parents=True)
    common = [dxc, source, "-HV", "2021", "-Ges"]
    for include in [root, *args.include]:
        common += ["-I", include]
    for define in args.define:
        common += ["-D", define]
    result = {"source": source.relative_to(root).as_posix(), "sourceSha256": digest(source), "stages": {}}
    for stage, profile, entry, suffix in (("vertex", "vs_6_0", args.vertex_entry, "vert"),
                                          ("pixel", "ps_6_0", args.pixel_entry, "frag")):
        stage_args = common + ["-T", profile, "-E", entry]
        outputs = {}
        if "dx12" in args.target:
            output = folder / f"{stage}.dxil"
            run(stage_args + ["-Fo", output], folder / f"{stage}.dx12.log", args.timeout)
            outputs["dx12"] = {"file": output.name, "entry": entry, "profile": profile}
        # DX12도 동일한 슬롯 계약 검사를 거친다. DXIL 생성 성공만으로 이식 성공으로 판단하지 않는다.
        spirv = folder / f"{stage}.spv"
        run(stage_args + ["-spirv", "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout",
                          "-fvk-stage-io-order=decl", "-fspv-flatten-resource-arrays",
                          "-fspv-entrypoint-name=main", "-fvk-t-shift", "8", "0",
                          "-fvk-s-shift", "40", "0", "-fvk-u-shift", "64", "0", "-Fo", spirv],
            folder / f"{stage}.spirv.log", args.timeout)
        raw = run([cross, spirv, "--reflect"], folder / f"{stage}.reflection.log", args.timeout)
        reflection = json.loads(raw)
        validate_reflection(reflection, stage)
        reflection_file = folder / f"{stage}.reflection.json"
        reflection_file.write_text(json.dumps(reflection, indent=2), encoding="utf-8")
        if "vulkan" in args.target:
            outputs["vulkan"] = {"file": spirv.name, "entry": "main", "environment": "vulkan1.1"}
        if "opengl" in args.target:
            # 현재 GL 텍스처는 자체 filtering을 사용한다. s1/복수 sampler는 조용히 무시하지 않고 거절한다.
            if any(item["binding"] != 40 for item in reflection.get("separate_samplers", [])):
                raise ValueError("OpenGL package currently supports the common s0 sampler only")
            intermediate = folder / f"{stage}.gl-intermediate.spv"
            gl_intermediate(spirv, intermediate)
            glsl = folder / f"{stage}.{suffix}.glsl"
            run([cross, intermediate, "--version", "450", "--no-es", "--separate-shader-objects",
                 "--combined-samplers-inherit-bindings", "--output", glsl],
                folder / f"{stage}.glsl.log", args.timeout)
            # GL context가 이미 ZERO_TO_ONE을 사용하므로 clip-space fixup이나 Y 반전을 추가하지 않는다.
            outputs["opengl"] = {"file": glsl.name, "entry": "main", "version": 450}
        for output in outputs.values():
            path = folder / output["file"]
            if not path.is_file() or not path.stat().st_size:
                raise ValueError(f"Compiler did not produce {path}")
            output["sha256"] = digest(path)
        result["stages"][stage] = {"outputs": outputs, "reflection": reflection_file.name}
    if digest(source) != result["sourceSha256"]:
        raise ValueError("HLSL changed during compilation; discard this package and retry")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", nargs="+", choices=("dx12", "vulkan", "opengl"), default=["dx12", "vulkan", "opengl"])
    parser.add_argument("--dxc", default="dxc")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--include", type=Path, action="append", default=[])
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--vertex-entry", default="VSMain")
    parser.add_argument("--pixel-entry", default="PSMain")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    root, output = args.source_root.resolve(), args.output.resolve()
    if not root.is_dir() or output == root or root in output.parents:
        parser.error("Source must exist; output must be outside source-root")
    if args.timeout < 1 or args.timeout > 600:
        parser.error("timeout must be 1..600 seconds")
    if any(not re.fullmatch(r"[A-Za-z_]\w*(=.*)?", value) for value in args.define):
        parser.error("Invalid shader define")
    args.include = [path.resolve(strict=True) for path in args.include]
    dxc, cross = tool_path(args.dxc), tool_path(args.spirv_cross)
    sources = sorted(root.rglob("*.hlsl"))
    if not sources:
        parser.error("No HLSL files found")
    output.mkdir(parents=True, exist_ok=True)
    # 이전 패키지를 덮어쓰지 않는다. 전체 성공 때만 current.json을 원자적으로 교체한다.
    job = output / ("run-" + uuid.uuid4().hex)
    job.mkdir()
    manifest = {"schema": 1, "success": False, "bindingContract": "cc-hlsl-rhi-v1",
                "targets": args.target, "defines": args.define,
                "sourceRoot": str(root), "includeDirectories": [str(path) for path in args.include],
                "entryPoints": {"vertex": args.vertex_entry, "pixel": args.pixel_entry},
                "tools": {"dxc": {"path": str(dxc), "sha256": digest(dxc)},
                          "spirvCross": {"path": str(cross), "sha256": digest(cross)}},
                "shaders": [], "failures": []}
    for source in sources:
        relative = source.relative_to(root).as_posix()
        key = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:24]
        try:
            shader = compile_source(source, root, job / key, args, dxc, cross)
            shader["directory"] = key
            manifest["shaders"].append(shader)
            print(f"COMPILED {relative}", flush=True)
        except (OSError, RuntimeError, ValueError, KeyError) as error:
            manifest["failures"].append({"source": relative, "error": str(error)})
            print(f"FAILED {relative}: {error}", file=sys.stderr, flush=True)
        # 중단돼도 완료한 파일과 실패 원인을 확인할 수 있도록 매 파일마다 기록한다.
        (job / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    manifest["success"] = not manifest["failures"]
    (job / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    if manifest["success"]:
        pointer = output / (job.name + ".json.tmp")
        pointer.write_text(json.dumps({"schema": 1, "manifest": f"{job.name}/manifest.json"}), encoding="utf-8")
        os.replace(pointer, output / "current.json")
    print(f"Manifest: {job / 'manifest.json'}", flush=True)
    return 0 if manifest["success"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
