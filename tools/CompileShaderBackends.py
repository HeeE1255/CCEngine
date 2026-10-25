"""Build dormant RHI shader packages. Requires DXC and SPIRV-Cross on PATH.

No engine settings, HLSL sources, or existing DX11 caches are modified.
Example:
  python tools/CompileShaderBackends.py --source-root Sandbox/assets/shaders
      --output local/shader-packages --dxc C:/Tools/dxc.exe
      --spirv-cross C:/Tools/spirv-cross.exe

DX12 also requires --reflector <built CCBuild.exe>. Reflection uses the
dxcompiler.dll beside the selected dxc.exe, never the current directory.

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
import tempfile
import copy
import time
import stat


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def executable_version(path):
    # 도구마다 다른 CLI 버전 옵션을 추측하지 않고 PE 버전 리소스를 읽는다.
    # 리소스 없는 배포본도 있으므로 버전 부재를 컴파일 실패로 취급하지 않는다.
    if os.name != "nt":
        return "Unavailable"
    try:
        import ctypes
        from ctypes import wintypes
        api = ctypes.WinDLL("version", use_last_error=True)
        api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
        api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
        api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
        api.GetFileVersionInfoW.restype = wintypes.BOOL
        api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR,
                                     ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
        api.VerQueryValueW.restype = wintypes.BOOL
        ignored = wintypes.DWORD()
        size = api.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
        if not size or size > 1024 * 1024:
            return "Unavailable"
        data = ctypes.create_string_buffer(size)
        if not api.GetFileVersionInfoW(str(path), 0, size, data):
            return "Unavailable"
        value, length = ctypes.c_void_p(), wintypes.UINT()
        if not api.VerQueryValueW(data, "\\", ctypes.byref(value), ctypes.byref(length)) or length.value < 52:
            return "Unavailable"
        fixed = ctypes.cast(value, ctypes.POINTER(wintypes.DWORD))
        if fixed[0] != 0xFEEF04BD:
            return "Unavailable"
        return f"{fixed[2] >> 16}.{fixed[2] & 65535}.{fixed[3] >> 16}.{fixed[3] & 65535}"
    except (OSError, ValueError):
        return "Unavailable"


def tool_path(value):
    found = shutil.which(value)
    if not found:
        raise RuntimeError(f"Required tool not found: {value}. Supply its executable path.")
    return Path(found).resolve()


def resolve_backend_tools(args):
    dxc, cross = tool_path(args.dxc), tool_path(args.spirv_cross)
    args.reflector = tool_path(args.reflector) if "dx12" in args.target else None
    if args.reflector:
        library = dxc.with_name("dxcompiler.dll")
        # 출력 폴더를 만들기 전에 누락을 알린다. 컴파일 후 reflection에서 뒤늦게 실패하지 않게 한다.
        if not library.is_file():
            raise RuntimeError(f"Required DXIL reflection library not found: {library}. "
                               "Select dxc.exe from a complete DXC installation containing dxcompiler.dll.")
    return dxc, cross


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
        diagnostic = (result.stdout + result.stderr).decode("utf-8", errors="replace")[-8192:]
        raise RuntimeError(f"Tool exited with {result.returncode}. See {log}\n{diagnostic}")
    return result.stdout


def validate_reflection(reflection, stage, *, cpu_layout=True):
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
    validate_buffer_layouts(reflection, cpu_layout=cpu_layout)


def validate_render_contract(reflection, stage):
    # 슬롯 번호만 맞아도 정수 피킹 버퍼에 실수 색상을 쓰면 화면/선택 결과가 달라진다.
    if stage == "pixel":
        seen = set()
        for output in reflection.get("outputs", []):
            location = output.get("location")
            expected = {0: "vec4", 1: "int"}.get(location)
            if (expected is None or location in seen or output.get("type") != expected or
                    output.get("array") or output.get("component", 0) != 0):
                raise ValueError(f"Unsupported scene render target output: {output}")
            seen.add(location)
        if 0 not in seen:
            raise ValueError("Scene pixel shader must write float4 color at target 0")
        reserved = {"AlbedoTexture": 8, "g_AlbedoMap": 8,
                    "NormalTexture": 9, "g_NormalMap": 9}
        for image in reflection.get("separate_images", []):
            if image["name"] in reserved and (
                    image["binding"] != reserved[image["name"]] or image["type"] != "texture2D"):
                raise ValueError(f"Material texture slot/type mismatch: {image['name']}")
    else:
        buffers = {b.get("name", "").removeprefix("type.") for b in reflection.get("ubos", [])}
        if not buffers.intersection({"Camera", "CameraCB", "CameraBuffer", "TransformBuffer",
                                     "TransformCB", "ObjectBuffer", "BoneBuffer"}):
            return
        mesh = {"POSITION": "vec3", "NORMAL": "vec3", "TEXCOORD": "vec2",
                "BONEIDS": "ivec4", "WEIGHTS": "vec4"}
        quad = {"POSITION": "vec3", "COLOR": "vec4", "TEXCOORD": "vec2",
                "TEXINDEX": "float", "ENTITY_ID": "int"}
        inputs = {}
        for item in reflection.get("inputs", []):
            semantic = item.get("name", "").removeprefix("in.var.").upper()
            semantic = re.sub(r"0$", "", semantic)
            if semantic in inputs or item.get("array") or item.get("component", 0) != 0:
                raise ValueError(f"Unsupported CPU vertex input: {item}")
            inputs[semantic] = item.get("type")
        # Gizmo처럼 일부 속성만 사용하는 선언은 허용하되 서로 다른 CPU 레이아웃을 섞지 않는다.
        if not any(all(layout.get(name) == kind for name, kind in inputs.items())
                   for layout in (mesh, quad)):
            raise ValueError(f"CPU vertex input layout mismatch: {inputs}")


def texture_bindings(reflection):
    bindings = {}
    occupied = set()
    for image in reflection.get("separate_images", []):
        name, slot = image["name"], image["binding"] - 8
        if not name or name in bindings or slot in occupied or not 0 <= slot < 32:
            raise ValueError(f"Invalid texture name/slot: {name}, {slot}")
        bindings[name] = slot
        occupied.add(slot)
    return bindings


def vertex_inputs(reflection):
    result, semantics, locations = [], set(), set()
    supported = {"float", "vec2", "vec3", "vec4", "int", "ivec2", "ivec3", "ivec4"}
    for item in reflection.get("inputs", []):
        name = item.get("name", "")
        if not name.startswith("in.var."):
            raise ValueError(f"Missing HLSL semantic in vertex input: {name}")
        semantic = re.sub(r"(?<![0-9])0$", "", name[7:].upper())
        location, kind = item.get("location"), item.get("type")
        if (not semantic or semantic in semantics or type(location) is not int or
                not 0 <= location < 32 or location in locations or kind not in supported or
                item.get("array") or item.get("component", 0) != 0):
            raise ValueError(f"Unsupported or duplicate vertex input: {item}")
        semantics.add(semantic)
        locations.add(location)
        result.append({"semantic": semantic, "type": kind, "location": location})
    return result


def spirv_interpolation(data, storage):
    # SPIRV-Cross JSON에 없는 보간 설정은 SPIR-V 명령에서 읽는다.
    # opcode/decoration 값은 Vulkan SDK spirv/unified1/spirv.hpp 기준이다.
    if len(data) < 20 or len(data) % 4:
        raise ValueError("Invalid SPIR-V byte length")
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    if words[0] != 0x07230203:
        raise ValueError("Invalid SPIR-V magic")
    variables, decorations = {}, {}
    cursor = 5
    while cursor < len(words):
        count, opcode = words[cursor] >> 16, words[cursor] & 0xffff
        if count == 0 or cursor + count > len(words):
            raise ValueError("Truncated SPIR-V instruction")
        args = words[cursor + 1:cursor + count]
        if opcode == 59:  # OpVariable
            if len(args) < 3:
                raise ValueError("Invalid OpVariable")
            variables[args[1]] = args[2]
        elif opcode == 71:  # OpDecorate
            if len(args) < 2:
                raise ValueError("Invalid OpDecorate")
            target, decoration = args[:2]
            if decoration in (13, 14, 16, 17, 30):
                expected = 3 if decoration == 30 else 2
                if len(args) != expected or decoration in decorations.setdefault(target, {}):
                    raise ValueError("Invalid/duplicate interpolation decoration")
                decorations[target][decoration] = args[2] if decoration == 30 else True
        elif opcode in (73, 74, 75) or (opcode == 72 and len(args) >= 3 and args[2] in (13, 14, 16, 17, 30)):
            raise ValueError("Grouped/member stage interpolation is not supported")
        cursor += count
    result = {}
    for variable, variable_storage in variables.items():
        flags = decorations.get(variable, {})
        if variable_storage != storage or 30 not in flags:
            continue
        location = flags[30]
        if location in result or (14 in flags and any(flag in flags for flag in (13, 16, 17))):
            raise ValueError("Duplicate location or conflicting interpolation")
        result[location] = frozenset(flag for flag in (13, 14, 16, 17) if flag in flags)
    return result


def validate_interpolation(vertex_data, pixel_data, pixel_reflection):
    outputs = spirv_interpolation(vertex_data, 3)
    inputs = spirv_interpolation(pixel_data, 1)
    for item in pixel_reflection.get("inputs", []):
        location = item["location"]
        if location not in inputs or location not in outputs:
            raise ValueError(f"Missing interpolation location {location}")
        producer, consumer = outputs[location], inputs[location]
        if producer.intersection((13, 14)) != consumer.intersection((13, 14)):
            raise ValueError(f"Interpolation mismatch at location {location}")
        if item["type"].startswith(("ivec", "uvec")) or item["type"] in ("int", "uint"):
            if 14 not in consumer:
                raise ValueError(f"Integer pixel input requires flat at location {location}")
        # 현재 씬 파이프라인은 단일 샘플이다. sample-rate 보간을 조용히 무시하지 않는다.
        if 17 in consumer:
            raise ValueError("Sample interpolation requires an MSAA pipeline")


def validate_cpu_buffer(buffer, types):
    name = buffer.get("name", "").removeprefix("type.")
    contracts = {
        "Camera": (0, (64, 80), [(0, "mat4"), (64, "vec3"), (76, "float")]),
        "CameraCB": (0, (64, 80), [(0, "mat4"), (64, "vec3"), (76, "float")]),
        "CameraBuffer": (0, (64, 80), [(0, "mat4"), (64, "vec3"), (76, "float")]),
        "ObjectBuffer": (1, (64, 96), [(0, "mat4"), (64, "vec4"), (80, "int"), (84, "int"), (88, "vec2")]),
        "TransformCB": (1, (64, 96), [(0, "mat4"), (64, "vec4"), (80, "int"), (84, "int"), (88, "vec2")]),
        "TransformBuffer": (1, (64, 96), [(0, "mat4"), (64, "vec4"), (80, "int"), (84, "int"), (88, "vec2")]),
        "BoneBuffer": (2, (32768,), [(0, "mat4")]),
        "SceneBuffer": (3, (144,), [(0, None), (128, "int"), (132, "vec3")]),
    }
    if name not in contracts:
        return
    binding, sizes, fields = contracts[name]
    members = types[buffer["type"]]["members"]
    size = buffer["block_size"]

    def require(valid):
        if not valid:
            raise ValueError(f"{name} CPU upload contract mismatch (b{binding}, sizes {sizes})")

    require(buffer["binding"] == binding and size in sizes)
    # 행렬만 사용하는 셰이더는 CPU 버퍼의 앞부분만 선언해도 안전하다.
    expected = fields[:1] if size == 64 else fields
    # std140에서 132바이트 위치의 float3는 표현할 수 없다. 같은 CPU 패딩을 float 3개로 풀어 쓸 수 있다.
    if name == "SceneBuffer" and len(members) == 5:
        expected = [(0, None), (128, "int"), (132, "float"), (136, "float"), (140, "float")]
    require(len(members) == len(expected))
    for member, (offset, kind) in zip(members, expected):
        require(member["offset"] == offset and (kind is None or member["type"] == kind))
        if kind == "mat4":
            # DXC SPIR-V reflection은 HLSL 기본 column-major를 row_major로 표현한다.
            require(member.get("matrix_stride") == 16 and member.get("row_major", False))
        if name not in ("BoneBuffer", "SceneBuffer") or offset != 0:
            require("array" not in member)
    if name == "BoneBuffer":
        require(members[0].get("array") == [512] and members[0].get("array_stride") == 64)
    if name == "SceneBuffer":
        lights = members[0]
        require(lights.get("array") == [4] and lights.get("array_stride") == 32)
        light_fields = types.get(lights["type"], {}).get("members", [])
        require([(f["offset"], f["type"]) for f in light_fields] ==
                [(0, "vec3"), (12, "float"), (16, "vec3"), (28, "float")])


def validate_buffer_layouts(reflection, *, cpu_layout=True):
    types = reflection.get("types", {})
    layouts = {}

    def integer(value, label, minimum=0):
        if type(value) is not int or value < minimum:
            raise ValueError(f"Invalid {label}: {value}")
        return value

    def members_size(type_id, ancestors):
        if type_id in ancestors or type_id not in types:
            raise ValueError(f"Unknown or recursive buffer type: {type_id}")
        end = 0
        names = set()
        members = types[type_id].get("members", [])
        if not members:
            raise ValueError(f"Empty buffer type: {type_id}")
        for member in members:
            name = member.get("name")
            if not isinstance(name, str) or not name or name in names:
                raise ValueError(f"Invalid/duplicate buffer member: {name}")
            names.add(name)
            offset = integer(member.get("offset"), f"{name} offset")
            if offset < end:
                raise ValueError(f"Overlapping buffer member: {name}")
            end = offset + member_size(member, ancestors | {type_id})
        return end

    def member_size(member, ancestors):
        kind = member.get("type", "")
        if kind in types:
            size = members_size(kind, ancestors)
        elif kind in ("float", "int", "uint", "bool"):
            size = 4
        elif re.fullmatch(r"[iub]?vec[234]", kind):
            size = int(kind[-1]) * 4
        elif re.fullmatch(r"mat[234](x[234])?", kind):
            columns = int(kind[3])
            rows = int(kind[-1])
            vectors, components = (rows, columns) if member.get("row_major", False) else (columns, rows)
            stride = integer(member.get("matrix_stride"), "matrix stride", components * 4)
            size = (vectors - 1) * stride + components * 4
        else:
            raise ValueError(f"Unsupported buffer field type: {kind}")
        if "array" in member:
            dimensions = member["array"]
            # 다차원 배열은 reflection에 각 차원의 stride가 없으므로 추측하지 않는다.
            if not isinstance(dimensions, list) or len(dimensions) != 1 or member.get("array_size_is_literal") != [True]:
                raise ValueError("Only fixed one-dimensional buffer arrays are supported")
            count = integer(dimensions[0], "array count", 1)
            stride = integer(member.get("array_stride"), "array stride", size)
            size = (count - 1) * stride + size
        return size

    for buffer in reflection.get("ubos", []):
        size = integer(buffer.get("block_size"), "buffer size", 1)
        if size > 65536 or members_size(buffer.get("type"), set()) > size:
            raise ValueError(f"Buffer members exceed storage: {buffer.get('name')}")
        layouts[buffer["binding"]] = {member["name"]: member_size(member, {buffer["type"]})
                                     for member in types[buffer["type"]]["members"]}
        if cpu_layout:
            validate_cpu_buffer(buffer, types)
        if cpu_layout and buffer.get("name", "").removeprefix("type.") == "MaterialPropertyBuffer":
            # Renderer3D의 CPU 업로드 구조와 같은 순서/간격이어야 슬롯이 맞아도 값이 뒤섞이지 않는다.
            expected = [("AlbedoColor", 0, None), ("PropertyColors", 16, 8),
                        ("PropertyScalars", 144, 4), ("PropertyToggles", 208, 4),
                        ("SurfaceValues", 272, None)]
            members = types[buffer["type"]]["members"]
            if buffer["binding"] != 4 or size != 288 or len(members) != len(expected):
                raise ValueError("MaterialPropertyBuffer must match CPU layout: b4, 288 bytes")
            for member, (name, offset, count) in zip(members, expected):
                if (member["name"] not in (name, "g_" + name) or member["type"] != "vec4" or
                        member["offset"] != offset or member.get("array") != ([count] if count else None) or
                        (count and member.get("array_stride") != 16)):
                    raise ValueError(f"MaterialPropertyBuffer CPU layout mismatch: {name}")
    return layouts


def compare_dxil_reflection(spirv, dxil):
    if dxil.get("schema") != 1:
        raise ValueError("Unsupported DXIL reflection schema")
    sizes = validate_buffer_layouts(spirv)
    expected_slots = set()
    for category, shift in (("ubos", 0), ("separate_images", 8), ("separate_samplers", 40)):
        expected_slots.update((category, r["binding"] - shift) for r in spirv.get(category, []))
    actual_slots = set()
    categories = {0: ("ubos", 8), 2: ("separate_images", 32), 3: ("separate_samplers", 2)}
    for resource in dxil["resources"]:
        category, limit = categories.get(resource["type"], (None, 0))
        start, count = resource["binding"], resource["count"]
        if (category is None or resource["space"] != 0 or count < 1 or start < 0 or start + count > limit or
                (category == "separate_images" and resource["dimension"] != 4)):
            raise ValueError(f"Unsupported DXIL resource: {resource['name']}")
        for slot in range(start, start + count):
            if (category, slot) in actual_slots:
                raise ValueError("Duplicate DXIL resource slot")
            actual_slots.add((category, slot))
    if expected_slots != actual_slots:
        raise ValueError(f"DXIL/SPIR-V resource mismatch: {expected_slots ^ actual_slots}")

    def compare_type(member, native, path):
        if native["elements"] != (member.get("array") or [0])[0]:
            raise ValueError(f"DXIL/SPIR-V array length mismatch: {path}")
        if member.get("array") and member["array_stride"] != (native_extent(native, False) + 15) // 16 * 16:
            raise ValueError(f"DXIL/SPIR-V array stride mismatch: {path}")
        kind = member["type"]
        if kind in spirv.get("types", {}):
            children = spirv["types"][kind]["members"]
            if native["class"] != 5 or len(children) != len(native["members"]):
                raise ValueError(f"DXIL/SPIR-V structure mismatch: {path}")
            for child, other in zip(children, native["members"]):
                if child["name"] != other["name"] or child["offset"] != other["offset"]:
                    raise ValueError(f"DXIL/SPIR-V member offset mismatch: {path}.{child['name']}")
                compare_type(child, other, path + "." + child["name"])
            return
        scalar = {"float": 3, "int": 2, "uint": 19, "bool": 1}
        if kind in scalar:
            expected = (0, scalar[kind], 1, 1)
        elif re.fullmatch(r"[iub]?vec[234]", kind):
            expected = (1, {"i": 2, "u": 19, "b": 1}.get(kind[0], 3), 1, int(kind[-1]))
        elif re.fullmatch(r"mat[234](x[234])?", kind):
            # DXC는 HLSL 행렬을 SPIR-V에서 전치된 타입으로 표현한다.
            # 따라서 행/열 개수와 major 표기를 동시에 변환해야 같은 저장 배치를 비교한다.
            expected = (3 if member.get("row_major", False) else 2, 3, int(kind[3]), int(kind[-1]))
            if member["matrix_stride"] != 16:
                raise ValueError(f"DXIL/SPIR-V matrix stride mismatch: {path}")
        else:
            raise ValueError(f"Unsupported reflected field: {path}")
        if (native["class"], native["scalar"], native["rows"], native["columns"]) != expected:
            raise ValueError(f"DXIL/SPIR-V field type mismatch: {path}")

    def native_extent(native, include_array=True):
        if native["class"] == 5:
            size = max(child["offset"] + native_extent(child) for child in native["members"])
        elif native["class"] in (2, 3):
            vectors, components = ((native["rows"], native["columns"]) if native["class"] == 2
                                   else (native["columns"], native["rows"]))
            size = (vectors - 1) * 16 + components * 4
        else:
            size = native["rows"] * native["columns"] * 4
        if include_array and native["elements"]:
            size = (native["elements"] - 1) * ((size + 15) // 16 * 16) + size
        return size

    native_buffers = {buffer["binding"]: buffer for buffer in dxil["buffers"]}
    if len(native_buffers) != len(dxil["buffers"]) or set(native_buffers) != set(sizes):
        raise ValueError("DXIL/SPIR-V constant buffer list mismatch")
    for buffer in spirv.get("ubos", []):
        native = native_buffers[buffer["binding"]]
        members = spirv["types"][buffer["type"]]["members"]
        if (native["space"] != 0 or native["name"] != buffer["name"].removeprefix("type.") or
                native["size"] != (buffer["block_size"] + 15) // 16 * 16 or len(native["members"]) != len(members)):
            raise ValueError(f"DXIL/SPIR-V buffer mismatch: {buffer['name']}")
        for member, other in zip(members, native["members"]):
            path = buffer["name"] + "." + member["name"]
            if (member["name"] != other["name"] or member["offset"] != other["offset"] or
                    sizes[buffer["binding"]][member["name"]] != other["size"]):
                raise ValueError(f"DXIL/SPIR-V field layout mismatch: {path}")
            compare_type(member, other["type"], path)


def reflection_tool_hashes(args, dxc):
    reflector = getattr(args, "reflector", None)
    if not reflector:
        return {}
    result = {"reflectorSha256": digest(reflector), "dxcompilerSha256": digest(dxc.with_name("dxcompiler.dll"))}
    validator = dxc.with_name("dxil.dll")
    if validator.is_file():
        result["dxilSha256"] = digest(validator)
    return result


def validate_dxil_stage_linkage(vertex, pixel):
    def components(item):
        mask = item["mask"]
        if type(mask) is not int or mask < 1 or mask > 15:
            raise ValueError("Invalid stage signature component mask")
        # 레지스터 내부 패킹 위치는 단계마다 다를 수 있다. semantic 내부의
        # 첫 성분을 기준으로 비교해야 같은 vec2의 xy/zw 배치를 오류로 오인하지 않는다.
        shift = (mask & -mask).bit_length() - 1
        return (item["activeMask"] & mask) >> shift

    outputs = {}
    for item in vertex["outputs"]:
        key = (item["semantic"].upper(), item["index"])
        if key in outputs or item["stream"] != 0:
            raise ValueError(f"Unsupported/duplicate vertex output: {key}")
        outputs[key] = item
    position = outputs.get(("SV_POSITION", 0))
    if not position or position["componentType"] != 3 or position["activeMask"] != 15:
        raise ValueError("Vertex shader must write float4 SV_POSITION")
    seen = set()
    for item in pixel["inputs"]:
        key = (item["semantic"].upper(), item["index"])
        if key in seen:
            raise ValueError(f"Duplicate pixel input: {key}")
        seen.add(key)
        if not item["activeMask"]:
            continue
        # 이 값들은 정점 varying이 아니라 rasterizer가 제공한다.
        if key[0] in ("SV_ISFRONTFACE", "SV_PRIMITIVEID", "SV_SAMPLEINDEX", "SV_COVERAGE", "SV_INNERCOVERAGE"):
            continue
        producer = outputs.get(key)
        if producer is None:
            raise ValueError(f"Pixel input has no vertex output: {key}")
        if producer["componentType"] != item["componentType"]:
            raise ValueError(f"Vertex/pixel component type mismatch: {key}")
        if components(item) & ~components(producer):
            raise ValueError(f"Pixel input reads unwritten vertex components: {key}")
        if producer["precision"] != item["precision"]:
            raise ValueError(f"Vertex/pixel minimum precision mismatch: {key}")


def validate_spirv_stage_linkage(vertex, pixel):
    def slots(items):
        result = {}
        for item in items:
            location = item.get("location")
            # 현재 VS/PS 경계는 32비트 scalar/vector만 지원한다. 다중 location을
            # 차지하는 배열/행렬을 한 슬롯으로 간주해 검사에서 빠뜨리지 않는다.
            if (type(location) is not int or location < 0 or item.get("array") or
                    not re.fullmatch(r"float|int|uint|[iu]?vec[234]", item["type"]) or item.get("component", 0) != 0):
                raise ValueError(f"Unsupported SPIR-V stage interface: {item}")
            if location in result:
                raise ValueError(f"Duplicate SPIR-V interface location: {location}")
            result[location] = item["type"]
        return result
    outputs, inputs = slots(vertex.get("outputs", [])), slots(pixel.get("inputs", []))
    for location, kind in inputs.items():
        if outputs.get(location) != kind:
            raise ValueError(f"SPIR-V vertex/pixel interface mismatch at location {location}: {outputs.get(location)} -> {kind}")


def build_uniform_repack(source, destination):
    """Build checked scalar-copy ranges between two reflections of the same shader."""
    validate_buffer_layouts(source)
    validate_buffer_layouts(destination, cpu_layout=False)
    result = []
    src_types, dst_types = source.get("types", {}), destination.get("types", {})

    def blocks(reflection):
        mapped = {}
        for block in reflection.get("ubos", []):
            binding = block["binding"]
            if type(binding) is not int or binding < 0 or binding >= 8 or binding in mapped:
                raise ValueError("Invalid or duplicate uniform repack binding")
            mapped[binding] = block
        return mapped

    src_blocks, dst_blocks = blocks(source), blocks(destination)
    if set(src_blocks) != set(dst_blocks):
        raise ValueError("Uniform repack buffer bindings differ")
    for binding in sorted(src_blocks):
        src, dst = src_blocks[binding], dst_blocks[binding]
        if src["name"] != dst["name"]:
            raise ValueError("Uniform repack buffer names differ")
        copies, occupied = [], set()

        def scalar(src_offset, dst_offset):
            if (src_offset % 4 or dst_offset % 4 or src_offset < 0 or dst_offset < 0 or
                    src_offset + 4 > src["block_size"] or dst_offset + 4 > dst["block_size"]):
                raise ValueError("Uniform repack copy exceeds buffer storage")
            if dst_offset in occupied:
                raise ValueError("Uniform repack destination overlap")
            occupied.add(dst_offset)
            copies.append({"source": src_offset, "destination": dst_offset, "size": 4})

        def fields(src_type, dst_type, src_base, dst_base):
            left, right = src_types[src_type]["members"], dst_types[dst_type]["members"]
            if [m["name"] for m in left] != [m["name"] for m in right]:
                raise ValueError("Uniform repack member names/order differ")
            for a, b in zip(left, right):
                if a.get("array") != b.get("array"):
                    raise ValueError("Uniform repack array dimensions differ")
                count = a["array"][0] if "array" in a else 1
                for index in range(count):
                    x = src_base + a["offset"] + index * a.get("array_stride", 0)
                    y = dst_base + b["offset"] + index * b.get("array_stride", 0)
                    a_struct, b_struct = a["type"] in src_types, b["type"] in dst_types
                    if a_struct != b_struct:
                        raise ValueError("Uniform repack field kind differs")
                    if a_struct:
                        fields(a["type"], b["type"], x, y)
                        continue
                    kind = a["type"]
                    if kind != b["type"]:
                        raise ValueError("Uniform repack scalar/vector/matrix type differs")
                    if re.fullmatch(r"mat[234](x[234])?", kind):
                        columns, rows = int(kind[3]), int(kind[-1])
                        # 행렬의 의미상 (row, column)을 유지한다. row_major 플래그가
                        # 다르더라도 단순 블록 복사가 아닌 성분 위치 대응으로 처리한다.
                        for column in range(columns):
                            for row in range(rows):
                                a_vector, a_component = (row, column) if a.get("row_major") else (column, row)
                                b_vector, b_component = (row, column) if b.get("row_major") else (column, row)
                                scalar(x + a_vector * a["matrix_stride"] + a_component * 4,
                                       y + b_vector * b["matrix_stride"] + b_component * 4)
                    else:
                        vector = re.fullmatch(r"[iub]?vec([234])", kind)
                        for component in range(int(vector[1]) if vector else 1):
                            scalar(x + component * 4, y + component * 4)

        fields(src["type"], dst["type"], 0, 0)
        # 패딩은 복사하지 않는다. 업로드 측은 목적 버퍼를 0으로 채운 뒤 이 표만 적용한다.
        merged = []
        for copy_range in sorted(copies, key=lambda item: item["destination"]):
            if (merged and merged[-1]["source"] + merged[-1]["size"] == copy_range["source"] and
                    merged[-1]["destination"] + merged[-1]["size"] == copy_range["destination"]):
                merged[-1]["size"] += 4
            else:
                merged.append(copy_range)
        result.append({"binding": binding, "sourceSize": src["block_size"],
                       "destinationSize": (dst["block_size"] + 15) // 16 * 16, "copies": merged})
    return result


def merge_uniform_repack(stages):
    merged, contracts = {}, {}
    for source, destination in stages:
        # 컴파일 단계마다 type ID가 달라진다. ID 대신 필드 의미와 물리 배치를 비교한다.
        def signature(reflection, block):
            types = reflection.get("types", {})

            def members(type_id):
                return [{**{key: value for key, value in field.items() if key != "type"},
                         "type": members(field["type"]) if field["type"] in types else field["type"]}
                        for field in types[type_id]["members"]]

            return (block["name"], block["block_size"], members(block["type"]))

        mappings = build_uniform_repack(source, destination)
        src = {block["binding"]: block for block in source.get("ubos", [])}
        dst = {block["binding"]: block for block in destination.get("ubos", [])}
        for mapping in mappings:
            binding = mapping["binding"]
            contract = (signature(source, src[binding]), signature(destination, dst[binding]))
            if binding in merged and (merged[binding] != mapping or contracts[binding] != contract):
                raise ValueError(f"OpenGL vertex/pixel uniform layout mismatch at binding {binding}")
            merged[binding], contracts[binding] = mapping, contract
    return {"contract": "cc-uniform-repack-v1", "buffers": [merged[key] for key in sorted(merged)]}


def describe_gl_layout_conflicts(reflection):
    # 변환 실패의 원인 후보만 설명한다. 이 검사로 CPU 배치를 수정하거나
    # GLSL 호환성을 확정하지 않는다. 실제 변환기의 판단을 그대로 유지한다.
    types = reflection.get("types", {})
    conflicts = []

    def visit(type_id, prefix, ancestors):
        if type_id in ancestors or type_id not in types:
            return
        for member in types[type_id].get("members", []):
            path = prefix + "." + member["name"]
            kind, offset = member["type"], member["offset"]
            structured = kind in types
            matrix = bool(re.fullmatch(r"mat[234](x[234])?", kind))
            vector = re.fullmatch(r"[iub]?vec([234])", kind)
            alignment = 16 if structured or matrix or "array" in member else (
                (8 if vector[1] == "2" else 16) if vector else 4)
            if offset % alignment:
                conflicts.append(f"{path}: offset={offset}, std140 alignment={alignment}")
            if "array" in member and member["array_stride"] % 16:
                conflicts.append(f"{path}: array_stride={member['array_stride']}, std140 requires a multiple of 16")
            if matrix and member["matrix_stride"] != 16:
                conflicts.append(f"{path}: matrix_stride={member['matrix_stride']}, std140 requires 16 for 32-bit matrices")
            if structured:
                visit(kind, path + ("[]" if "array" in member else ""), ancestors | {type_id})

    for buffer in reflection.get("ubos", []):
        visit(buffer["type"], f"{buffer['name']} (b{buffer['binding']})", set())
    return conflicts


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
    result = {"source": source.relative_to(root).as_posix(), "sourceSha256": digest(source), "stages": {},
              "materialPropertySource": "\n".join(line for line in source.read_text(encoding="utf-8-sig").splitlines()
                  if re.match(r"^\s*//\s*@property\b", line))}
    spirv_stages, dxil_stages, gl_stages = {}, {}, {}
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
        spirv_args = ["-spirv", "-fspv-target-env=vulkan1.1",
                          "-fvk-stage-io-order=decl", "-fspv-flatten-resource-arrays",
                          "-fspv-entrypoint-name=main", "-fvk-t-shift", "8", "0",
                          "-fvk-s-shift", "40", "0", "-fvk-u-shift", "64", "0"]
        run(stage_args + spirv_args + ["-fvk-use-dx-layout", "-Fo", spirv],
            folder / f"{stage}.spirv.log", args.timeout)
        raw = run([cross, spirv, "--reflect"], folder / f"{stage}.reflection.log", args.timeout)
        reflection = json.loads(raw)
        validate_reflection(reflection, stage)
        validate_render_contract(reflection, stage)
        spirv_stages[stage] = reflection
        if "dx12" in args.target:
            raw_dxil = run([args.reflector, f"--reflect-dxil={folder / (stage + '.dxil')}",
                            f"--dxcompiler={dxc.with_name('dxcompiler.dll')}"],
                           folder / f"{stage}.dxil-reflection.log", args.timeout)
            dxil_reflection = json.loads(raw_dxil)
            dxil_stages[stage] = dxil_reflection
            compare_dxil_reflection(reflection, dxil_reflection)
            (folder / f"{stage}.dxil-reflection.json").write_text(json.dumps(dxil_reflection, indent=2), encoding="utf-8")
        reflection_file = folder / f"{stage}.reflection.json"
        reflection_file.write_text(json.dumps(reflection, indent=2), encoding="utf-8")
        if "vulkan" in args.target:
            outputs["vulkan"] = {"file": spirv.name, "entry": "main", "environment": "vulkan1.1"}
        if "opengl" in args.target:
            # 현재 GL 텍스처는 자체 filtering을 사용한다. s1/복수 sampler는 조용히 무시하지 않고 거절한다.
            if any(item["binding"] != 40 for item in reflection.get("separate_samplers", [])):
                raise ValueError("OpenGL package currently supports the common s0 sampler only")
            # CPU/Vulkan 배치는 유지한다. GL 전용 std140 배치와의 차이는 업로드 복사표로 흡수한다.
            gl_spirv = folder / f"{stage}.gl-layout.spv"
            run(stage_args + spirv_args + ["-fvk-use-gl-layout", "-Fo", gl_spirv],
                folder / f"{stage}.gl-layout.log", args.timeout)
            gl_reflection = json.loads(run([cross, gl_spirv, "--reflect"],
                                          folder / f"{stage}.gl-reflection.log", args.timeout))
            validate_reflection(gl_reflection, stage, cpu_layout=False)
            validate_render_contract(gl_reflection, stage)
            for category in ("inputs", "outputs", "separate_images", "separate_samplers"):
                if reflection.get(category, []) != gl_reflection.get(category, []):
                    raise ValueError(f"OpenGL layout compilation changed {category} in {stage}")
            gl_stages[stage] = gl_reflection
            intermediate = folder / f"{stage}.gl-intermediate.spv"
            gl_intermediate(gl_spirv, intermediate)
            glsl = folder / f"{stage}.{suffix}.glsl"
            try:
                run([cross, intermediate, "--version", "450", "--no-es", "--separate-shader-objects",
                     "--combined-samplers-inherit-bindings", "--output", glsl],
                    folder / f"{stage}.glsl.log", args.timeout)
            except RuntimeError as error:
                conflicts = describe_gl_layout_conflicts(gl_reflection)
                if not conflicts:
                    raise
                # 원래 도구 오류를 보존한다. 타임아웃 등 다른 원인일 수도 있으므로
                # 충돌 후보를 확정 원인이라고 표시하지 않는다.
                detail = "\n".join(conflicts[:24])
                if len(conflicts) > 24:
                    detail += f"\n... {len(conflicts) - 24} more layout candidates"
                raise RuntimeError(f"{error}\nOpenGL layout candidates ({source.name}, {stage}):\n{detail}\n"
                                   "GL-layout conversion failed; no usable repack package was published.") from error
            # GL context가 이미 ZERO_TO_ONE을 사용하므로 clip-space fixup이나 Y 반전을 추가하지 않는다.
            outputs["opengl"] = {"file": glsl.name, "entry": "main", "version": 450}
        for output in outputs.values():
            path = folder / output["file"]
            if not path.is_file() or not path.stat().st_size:
                raise ValueError(f"Compiler did not produce {path}")
            output["sha256"] = digest(path)
        result["stages"][stage] = {"outputs": outputs, "reflection": reflection_file.name}
    validate_spirv_stage_linkage(spirv_stages["vertex"], spirv_stages["pixel"])
    validate_interpolation((folder / "vertex.spv").read_bytes(),
                           (folder / "pixel.spv").read_bytes(), spirv_stages["pixel"])
    if "dx12" in args.target:
        validate_dxil_stage_linkage(dxil_stages["vertex"], dxil_stages["pixel"])
    if "opengl" in args.target:
        validate_spirv_stage_linkage(gl_stages["vertex"], gl_stages["pixel"])
        validate_interpolation((folder / "vertex.gl-layout.spv").read_bytes(),
                               (folder / "pixel.gl-layout.spv").read_bytes(), gl_stages["pixel"])
        result["openglUniformRepack"] = merge_uniform_repack(
            [(spirv_stages[stage], gl_stages[stage]) for stage in ("vertex", "pixel")])
    result["stageLinkContract"] = "cc-vs-ps-link-v1"
    result["cpuBufferContract"] = "cc-renderer-buffers-v1"
    result["renderContract"] = "cc-scene-io-v1"
    result["textureBindings"] = texture_bindings(spirv_stages["pixel"])
    result["interpolationContract"] = "cc-stage-interpolation-v1"
    result["vertexInputs"] = vertex_inputs(spirv_stages["vertex"])
    if digest(source) != result["sourceSha256"]:
        raise ValueError("HLSL changed during compilation; discard this package and retry")
    return result


def check_backend_tools(args, dxc, cross):
    # 버전 문자열만으로 호환성을 판단하지 않는다. 실제 게시 경로와 같은 변환/검증을 실행한다.
    probe = copy.copy(args)
    probe.include, probe.define = [], []
    probe.vertex_entry, probe.pixel_entry = "VSMain", "PSMain"
    with tempfile.TemporaryDirectory(prefix="cc-rhi-tools-") as temporary:
        root = Path(temporary)
        source = root / "ToolProbe.hlsl"
        source.write_text(
            "cbuffer Probe : register(b0) { float4 Tint; };\n"
            "float4 VSMain(float3 p : POSITION) : SV_POSITION { return float4(p, 1); }\n"
            "float4 PSMain() : SV_Target0 { return Tint; }\n", encoding="utf-8")
        try:
            result = compile_source(source, root, root / "compiled", probe, dxc, cross)
        except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
            # 임시 디렉터리는 종료 시 지워지므로 실패 로그의 내용도 호출자에게 남긴다.
            logs = []
            for path in sorted(root.rglob("*.log")):
                logs.append(path.name + ": " + path.read_bytes()[-4096:].decode("utf-8", errors="replace"))
            raise RuntimeError("Shader tool capability check failed: " + str(error) +
                               "\n" + "\n".join(logs)) from error
        return {"schema": 1, "success": True, "targets": list(args.target),
                "scope": "offline-compile-reflection-not-gpu",
                "tools": {"python": {"version": sys.version.split()[0]},
                          "dxc": {"path": str(dxc), "sha256": digest(dxc), "version": executable_version(dxc)},
                          "spirvCross": {"path": str(cross), "sha256": digest(cross), "version": executable_version(cross)},
                          "reflection": reflection_tool_hashes(args, dxc)},
                "stages": {name: list(stage["outputs"]) for name, stage in result["stages"].items()}}


def shader_input_hash(source, root, includes):
    visited = {}
    def visit(path):
        path = path.resolve(strict=True)
        if str(path) in visited:
            return
        visited[str(path)] = digest(path)
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            if not re.match(r"\s*#\s*include\b", line):
                continue
            match = re.match(r'\s*#\s*include\s*[<"]([^>"]+)[>"]', line)
            if not match:
                raise ValueError(f"Macro include cannot be tracked safely: {path}: {line}")
            candidates = [path.parent / match[1], root / match[1], *(directory / match[1] for directory in includes)]
            dependency = next((candidate for candidate in candidates if candidate.is_file()), None)
            if dependency is None:
                raise ValueError(f"Missing include: {match[1]} in {path}")
            visit(dependency)
    visit(source)
    return sorted(visited.items())


def prune_shader_cache(root, max_bytes=2 * 1024**3, max_age_days=30):
    # 완료 캐시와 소유 표식이 있는 임시 캐시의 평면 구조만 정리한다.
    # 심볼릭 링크, junction 및 정체를 알 수 없는 파일은 보존한다.
    def plain(path, directory=False):
        info = path.lstat()
        if getattr(info, "st_file_attributes", 0) & 0x400:
            return False
        return stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)

    entries = []
    for entry in root.iterdir():
        try:
            temporary = re.fullmatch(r"([0-9a-f]{64})\.[0-9a-f]{32}\.tmp", entry.name)
            if temporary and plain(entry, True):
                marker = entry / "owner.json"
                if not plain(marker) or marker.stat().st_size > 1024:
                    continue
                owner = json.loads(marker.read_text(encoding="utf-8"))
                if owner != {"schema": 1, "kind": "cc-shader-cache-staging", "key": temporary[1]}:
                    continue
                top = list(entry.iterdir())
                if not {p.name for p in top}.issubset({"owner.json", "cache.json", "files"}):
                    continue
                files = entry / "files"
                has_files = any(p.name == "files" for p in top)
                if has_files and not plain(files, True):
                    continue
                children = list(files.iterdir()) if has_files else []
                if not all(plain(p) for p in children) or not all(plain(p) for p in top if p.name != "files"):
                    continue
                # 복사 도중 중단돼 cache.json이 없을 수 있다. 대신 알려진 산출물만 허용한다.
                if any(not re.fullmatch(r"(?:vertex|pixel)\.(?:dxil|spv|gl-intermediate\.spv|gl-layout\.(?:spv|log)|gl-reflection\.log|"
                                       r"vert\.glsl|frag\.glsl|dx12\.log|spirv\.log|"
                                       r"reflection\.(?:json|log)|dxil-reflection\.(?:json|log)|glsl\.log)", p.name)
                       for p in children):
                    continue
                if max(p.stat().st_mtime for p in [entry, *top, *children]) >= time.time() - 7 * 86400:
                    continue
                for child in children:
                    child.unlink()
                if files.exists():
                    files.rmdir()
                for path in top:
                    if path.name != "files":
                        path.unlink()
                entry.rmdir()
                continue
            if not re.fullmatch(r"[0-9a-f]{64}", entry.name) or not plain(entry, True):
                continue
            if {p.name for p in entry.iterdir()} != {"cache.json", "files"}:
                continue
            metadata, files = entry / "cache.json", entry / "files"
            if not plain(metadata) or not plain(files, True):
                continue
            record = json.loads(metadata.read_text(encoding="utf-8"))
            children = list(files.iterdir())
            if (record.get("key") != entry.name or not isinstance(record.get("files"), dict) or
                    {p.name for p in children} != set(record["files"]) or
                    not all(plain(p) for p in children)):
                continue
            size = metadata.stat().st_size + sum(p.stat().st_size for p in children)
            entries.append((entry.stat().st_mtime, entry, children, size))
        except (OSError, ValueError, TypeError, AttributeError):
            continue
    total = sum(item[3] for item in entries)
    cutoff = time.time() - max_age_days * 86400
    for modified, entry, children, size in sorted(entries):
        if modified >= cutoff and total <= max_bytes:
            continue
        try:
            # 재귀 삭제 없이 확인한 파일만 제거한다. 캐시 root 잠금이 유지된 상태다.
            for child in children:
                child.unlink()
            (entry / "files").rmdir()
            (entry / "cache.json").unlink()
            entry.rmdir()
            total -= size
        except OSError:
            pass


def compile_cached_source(source, root, folder, args, dxc, cross):
    if not args.cache:
        return compile_cached_source_locked(source, root, folder, args, dxc, cross)
    cache = args.cache.resolve()
    lock = None
    locked = False
    try:
        cache.mkdir(parents=True, exist_ok=True)
        lock = (cache / ".maintenance.lock").open("a+b")
        if os.name == "nt":
            import msvcrt
            if lock.seek(0, os.SEEK_END) == 0:
                lock.write(b"0")
                lock.flush()
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        locked = True
    except OSError:
        pass
    try:
        if not locked:
            # 다른 빌드가 읽거나 쓰는 동안 정리하지 않는다. 잠금 대기 때문에
            # 전체 빌드가 멈추지 않도록 이번 셰이더만 캐시를 우회한다.
            uncached = copy.copy(args)
            uncached.cache = None
            return compile_cached_source_locked(source, root, folder, uncached, dxc, cross)
        result = compile_cached_source_locked(source, root, folder, args, dxc, cross)
        try:
            prune_shader_cache(cache)
        except OSError:
            pass  # 캐시 관리 실패가 정상 컴파일 결과를 폐기하지 않게 한다.
        return result
    finally:
        if lock:
            lock.close()  # 프로세스 종료 시에도 OS가 잠금을 해제한다.


def compile_cached_source_locked(source, root, folder, args, dxc, cross):
    # include가 바뀌었는데 원본 HLSL만 같다고 캐시를 재사용하면 오래된 화면이 나온다.
    # 코드/도구/옵션/재귀 include를 함께 키에 넣고 캐시 파일 해시도 다시 확인한다.
    inputs = shader_input_hash(source, root, args.include)
    reflection_hashes = reflection_tool_hashes(args, dxc)
    identity = {"inputs": inputs, "script": digest(Path(__file__)), "dxc": digest(dxc),
                "reflectionTools": reflection_hashes,
                "cross": digest(cross), "defines": sorted(args.define), "targets": sorted(args.target),
                "entries": [args.vertex_entry, args.pixel_entry], "root": str(root),
                "includes": [str(path) for path in args.include]}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    entry = args.cache.resolve() / key if args.cache else None
    if entry and entry.is_dir():
        cached = None
        try:
            cached = json.loads((entry / "cache.json").read_text(encoding="utf-8"))
            if cached["key"] != key or not isinstance(cached["files"], dict) or not cached["files"] or not isinstance(cached["shader"], dict):
                raise ValueError("Invalid cache entry")
            for name, expected in cached["files"].items():
                if not name or Path(name).name != name or name in (".", "..") or (entry / "files" / name).resolve().parent != (entry / "files").resolve():
                    raise ValueError("Invalid cache filename")
                if digest(entry / "files" / name) != expected:
                    raise ValueError("Corrupt shader cache")
            if inputs != shader_input_hash(source, root, args.include):
                raise ValueError("Inputs changed while checking cache")
        except (OSError, ValueError, KeyError, TypeError):
            cached = None
            # 읽는 쪽과 재생성하는 쪽이 반쪽짜리 캐시를 공유하지 않도록 먼저 격리한다.
            quarantine = entry.with_name(key + "." + uuid.uuid4().hex + ".invalid")
            try:
                entry.rename(quarantine)
            except OSError:
                pass
            else:
                shutil.rmtree(quarantine)
        if cached is not None:
            staging = folder.with_name(folder.name + "." + uuid.uuid4().hex + ".cache-tmp")
            try:
                staging.mkdir(parents=True)
                for name, expected in cached["files"].items():
                    shutil.copyfile(entry / "files" / name, staging / name)
                    if digest(staging / name) != expected:
                        raise ValueError("Cache changed during copy")
                if inputs != shader_input_hash(source, root, args.include):
                    raise ValueError("Inputs changed during cache copy")
                if reflection_hashes != reflection_tool_hashes(args, dxc):
                    raise ValueError("Reflection tools changed during cache copy")
                staging.rename(folder)
                try:
                    os.utime(entry, None)
                except OSError:
                    pass
                print(f"CACHE HIT {source.name}", flush=True)
                return cached["shader"]
            except (OSError, ValueError):
                # 복사 실패는 컴파일 출력에 흔적을 남기지 않는다. 기존 출력은 덮지 않는다.
                pass
            finally:
                if staging.exists():
                    shutil.rmtree(staging)
    result = compile_source(source, root, folder, args, dxc, cross)
    if inputs != shader_input_hash(source, root, args.include):
        raise ValueError("Shader/include changed during compilation")
    if reflection_hashes != reflection_tool_hashes(args, dxc):
        raise ValueError("Reflection tools changed during compilation")
    if entry and not entry.exists():
        entry.parent.mkdir(parents=True, exist_ok=True)
        temporary = entry.parent / (key + "." + uuid.uuid4().hex + ".tmp")
        try:
            temporary.mkdir()
            marker = temporary / "owner.json"
            marker.write_text(json.dumps({"schema": 1, "kind": "cc-shader-cache-staging", "key": key}), encoding="utf-8")
            shutil.copytree(folder, temporary / "files")
            files = {path.name: digest(path) for path in (temporary / "files").iterdir() if path.is_file()}
            (temporary / "cache.json").write_text(json.dumps({"key": key, "files": files, "shader": result}), encoding="utf-8")
            marker.unlink()
            try:
                temporary.rename(entry)
            except FileExistsError:
                pass
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    return result


def compile_build_request(args, root, output, dxc, cross):
    request = json.loads(args.build_request.read_text(encoding="utf-8"))
    api_names = {"dx12": "DirectX12", "opengl": "OpenGL", "vulkan": "Vulkan"}
    if (not isinstance(request, dict) or request.get("schema") != 1 or
            len(args.target) != 1 or args.target[0] not in api_names):
        raise ValueError("Build request requires schema 1 and exactly one target: dx12, opengl or vulkan")
    target = args.target[0]
    jobs = request.get("shaders")
    if not isinstance(jobs, list) or not jobs:
        raise ValueError("Build request has no shaders")
    if output.exists():
        raise ValueError("Build package output must be new; existing output is never overwritten")
    validated = []
    seen = set()
    for job in jobs:
        if not isinstance(job, dict) or not isinstance(job.get("source"), str):
            raise ValueError("Shader job requires a source string")
        raw_defines = job.get("defines")
        if not isinstance(raw_defines, list) or any(
                not isinstance(value, str) or not re.fullmatch(r"[A-Za-z_]\w*", value) for value in raw_defines):
            raise ValueError("Shader defines must be a list of keyword names")
        relative = Path(job["source"])
        source = (root / relative).resolve(strict=True)
        if (relative.is_absolute() or relative.drive or ".." in relative.parts or
                not source.is_relative_to(root) or source.is_relative_to(output) or
                not source.is_file() or source.suffix.lower() != ".hlsl"):
            raise ValueError("Shader source escapes project or refers to build output")
        defines = sorted(set(raw_defines))
        physical_identity = (os.path.normcase(str(source)), tuple(defines))
        if physical_identity in seen:
            raise ValueError("Duplicate shader variant")
        seen.add(physical_identity)
        identity = json.dumps([relative.as_posix(), defines], separators=(",", ":"))
        validated.append((identity, relative, source, defines))

    # 요청 전체를 먼저 검사한다. 후반부의 잘못된 항목 때문에 앞부분만 빌드된 결과를
    # 성공 산출물로 오인하지 않도록 새 디렉터리와 최종 완료 목록을 분리한다.
    tool_hashes = {"dxcSha256": digest(dxc), "spirvCrossSha256": digest(cross), **reflection_tool_hashes(args, dxc)}
    # 실제 빌드 대상만 검사한다. Vulkan 배포에 불필요한 DXIL 도구까지
    # 요구하지 않으며, 실패하면 출력 폴더나 완료 목록을 만들지 않는다.
    print(f"PREFLIGHT {target}", flush=True)
    capability = check_backend_tools(args, dxc, cross)
    if tool_hashes != {"dxcSha256": digest(dxc), "spirvCrossSha256": digest(cross), **reflection_tool_hashes(args, dxc)}:
        raise ValueError("Shader compiler tools changed during preflight; retry with stable tools")
    output.mkdir(parents=True, exist_ok=False)
    # 진단의 절대 도구 경로는 로컬 로그에만 남긴다. 배포에는 검증 범위와
    # 도구 해시만 기록해 개발 PC 경로가 공개되지 않게 한다.
    print(json.dumps(capability), flush=True)
    (output / "tool-check.json").write_text(json.dumps({
        "schema": 1, "success": True, "scope": capability["scope"],
        "targets": capability["targets"], "stages": capability["stages"], "tools": tool_hashes
    }, indent=2), encoding="utf-8")
    packages = []
    for identity, relative, source, defines in sorted(validated, key=lambda item: item[0]):
        key = hashlib.sha256(identity.encode("utf-8")).hexdigest()
        folder = output / key
        folder.mkdir(parents=True, exist_ok=False)
        job_args = copy.copy(args)
        job_args.define = defines
        shader = compile_cached_source(source, root, folder / "bytecode", job_args, dxc, cross)
        shader["source"] = relative.as_posix()
        shader["directory"] = "bytecode"
        # 배포 manifest에는 개발 PC 절대 경로나 도구 위치를 넣지 않는다.
        manifest = {"schema": 1, "success": True, "bindingContract": "cc-hlsl-rhi-v1",
                    "reflectionContract": "cc-dxil-spirv-layout-v1" if target == "dx12" else None,
                    "targets": [target], "defines": defines, "shaders": [shader],
                    "tools": tool_hashes}
        # 확장자로 정리하면 GL/Vulkan 실행 파일까지 삭제된다. 실제 manifest가
        # 참조하는 파일과 해시를 먼저 확인하고, 검증용 중간 파일만 제거한다.
        retained = set()
        if set(shader["stages"]) != {"vertex", "pixel"}:
            raise ValueError("Build package requires vertex and pixel stages")
        for stage in shader["stages"].values():
            if set(stage["outputs"]) != {target}:
                raise ValueError("Build package output target mismatch")
            artifact = stage["outputs"][target]
            name = artifact.get("file")
            if (not isinstance(name, str) or not name or name in (".", "..") or
                    any(char in name for char in "/\\:") or name in retained):
                raise ValueError("Invalid or duplicate build artifact filename")
            path = folder / "bytecode" / name
            if (path.is_symlink() or not path.is_file() or not path.stat().st_size or
                    digest(path) != artifact.get("sha256")):
                raise ValueError(f"Build artifact missing or hash mismatch: {name}")
            retained.add(name)
        for artifact in (folder / "bytecode").iterdir():
            if artifact.name not in retained:
                artifact.unlink()
        for stage in shader["stages"].values():
            stage.pop("reflection", None)
        (folder / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        packages.append(f"{key}/manifest.json")
        print(f"COMPILED {relative.as_posix()} {defines}", flush=True)
    if tool_hashes != {"dxcSha256": digest(dxc), "spirvCrossSha256": digest(cross), **reflection_tool_hashes(args, dxc)}:
        raise ValueError("Shader compiler tools changed during build; discard this package")
    # 모든 조합이 성공한 경우에만 빌드 서비스가 읽을 완료 목록을 게시한다.
    index = output / "packages.json"
    temporary = output / "packages.json.tmp"
    temporary.write_text(json.dumps({"schema": 1, "graphicsAPI": api_names[target], "packages": packages}), encoding="utf-8")
    os.replace(temporary, index)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check-tools", action="store_true",
                        help="Compile an isolated probe and validate tool compatibility; publish no package")
    parser.add_argument("--build-request", type=Path)
    parser.add_argument("--build-log", type=Path)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--target", nargs="+", choices=("dx12", "vulkan", "opengl"), default=["dx12", "vulkan", "opengl"])
    parser.add_argument("--dxc", default="dxc")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--reflector", default="CCBuild.exe", help="CCBuild executable with --reflect-dxil support (required for dx12)")
    parser.add_argument("--include", type=Path, action="append", default=[])
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--vertex-entry", default="VSMain")
    parser.add_argument("--pixel-entry", default="PSMain")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.timeout < 1 or args.timeout > 600:
        parser.error("timeout must be 1..600 seconds")
    if args.check_tools:
        if any((args.source_root, args.output, args.build_request, args.cache,
                args.include, args.define)) or args.vertex_entry != "VSMain" or args.pixel_entry != "PSMain":
            parser.error("--check-tools accepts only tool paths, targets, timeout and build-log")
        if args.build_log:
            import contextlib
            with args.build_log.open("w", encoding="utf-8") as log:
                with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
                    try:
                        dxc, cross = resolve_backend_tools(args)
                        print(json.dumps(check_backend_tools(args, dxc, cross), indent=2))
                        return 0
                    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
                        print(f"FAILED: {error}", flush=True)
                        return 1
        dxc, cross = resolve_backend_tools(args)
        print(json.dumps(check_backend_tools(args, dxc, cross), indent=2))
        return 0
    if not args.source_root or not args.output:
        parser.error("--source-root and --output are required unless --check-tools is used")
    root, output = args.source_root.resolve(), args.output.resolve()
    if not root.is_dir() or output == root or (not args.build_request and root in output.parents) or output in root.parents:
        parser.error("Source must exist; output must be outside source-root")
    if any(not re.fullmatch(r"[A-Za-z_]\w*(=.*)?", value) for value in args.define):
        parser.error("Invalid shader define")
    args.include = [path.resolve(strict=True) for path in args.include]
    if args.build_request:
        if args.build_log:
            import contextlib
            with args.build_log.open("w", encoding="utf-8") as log:
                with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
                    try:
                        dxc, cross = resolve_backend_tools(args)
                        return compile_build_request(args, root, output, dxc, cross)
                    except (OSError, RuntimeError, ValueError, KeyError, TypeError) as error:
                        print(f"FAILED: {error}", flush=True)
                        return 1
        dxc, cross = resolve_backend_tools(args)
        return compile_build_request(args, root, output, dxc, cross)
    dxc, cross = resolve_backend_tools(args)
    sources = sorted(root.rglob("*.hlsl"))
    if not sources:
        parser.error("No HLSL files found")
    output.mkdir(parents=True, exist_ok=True)
    # 이전 패키지를 덮어쓰지 않는다. 전체 성공 때만 current.json을 원자적으로 교체한다.
    job = output / ("run-" + uuid.uuid4().hex)
    job.mkdir()
    manifest = {"schema": 1, "success": False, "bindingContract": "cc-hlsl-rhi-v1",
                "reflectionContract": "cc-dxil-spirv-layout-v1" if "dx12" in args.target else None,
                "targets": args.target, "defines": args.define,
                "sourceRoot": str(root), "includeDirectories": [str(path) for path in args.include],
                "entryPoints": {"vertex": args.vertex_entry, "pixel": args.pixel_entry},
                "tools": {"dxc": {"path": str(dxc), "sha256": digest(dxc)},
                          "spirvCross": {"path": str(cross), "sha256": digest(cross)},
                          "reflection": reflection_tool_hashes(args, dxc)},
                "shaders": [], "failures": []}
    for source in sources:
        relative = source.relative_to(root).as_posix()
        key = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:24]
        try:
            shader = compile_cached_source(source, root, job / key, args, dxc, cross)
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
