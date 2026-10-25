import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("shader_backends", Path(__file__).parents[1] / "CompileShaderBackends.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


def fixture():
    spirv = {"types": {"_1": {"members": [{"name": "Color", "type": "vec4", "offset": 0}]}},
             "ubos": [{"name": "type.ColorBuffer", "type": "_1", "binding": 0, "set": 0, "block_size": 16}]}
    native = {"schema": 1, "resources": [{"name": "ColorBuffer", "type": 0, "binding": 0,
               "count": 1, "space": 0, "dimension": 0}],
              "buffers": [{"name": "ColorBuffer", "size": 16, "binding": 0, "space": 0,
                "members": [{"name": "Color", "offset": 0, "size": 16,
                  "type": {"class": 1, "scalar": 3, "rows": 1, "columns": 4, "elements": 0, "members": []}}]}]}
    return spirv, native


class CrossReflection(unittest.TestCase):
    def test_missing_dxcompiler_preflight(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        args = SimpleNamespace(dxc="dxc.exe", spirv_cross="spirv-cross.exe", reflector="CCBuild.exe", target=["dx12"])
        with patch.object(backend, "tool_path", side_effect=Path), patch.object(Path, "is_file", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "complete DXC installation"):
                backend.resolve_backend_tools(args)

    def test_dxcompiler_preflight_success(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        args = SimpleNamespace(dxc="dxc.exe", spirv_cross="spirv-cross.exe", reflector="CCBuild.exe", target=["dx12"])
        with patch.object(backend, "tool_path", side_effect=Path), patch.object(Path, "is_file", return_value=True):
            self.assertEqual((Path("dxc.exe"), Path("spirv-cross.exe")), backend.resolve_backend_tools(args))

    def test_vulkan_does_not_require_dxil_reflector(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        args = SimpleNamespace(dxc="dxc.exe", spirv_cross="spirv-cross.exe", reflector="missing.exe", target=["vulkan"])
        with patch.object(backend, "tool_path", side_effect=Path) as resolve, patch.object(Path, "is_file") as exists:
            backend.resolve_backend_tools(args)
            self.assertEqual(2, resolve.call_count)
            exists.assert_not_called()
            self.assertIsNone(args.reflector)

    def test_matching_layout(self):
        backend.compare_dxil_reflection(*fixture())

    def test_native_member_offset_mismatch(self):
        spirv, native = fixture()
        native["buffers"][0]["members"][0]["offset"] = 4
        with self.assertRaisesRegex(ValueError, "field layout"):
            backend.compare_dxil_reflection(spirv, native)

    def test_native_member_type_mismatch(self):
        spirv, native = fixture()
        native["buffers"][0]["members"][0]["type"]["scalar"] = 2
        with self.assertRaisesRegex(ValueError, "field type"):
            backend.compare_dxil_reflection(spirv, native)

    def test_resource_slot_mismatch(self):
        spirv, native = fixture()
        native["resources"][0]["binding"] = 1
        with self.assertRaisesRegex(ValueError, "resource mismatch"):
            backend.compare_dxil_reflection(spirv, native)

    def test_native_array_length_mismatch(self):
        spirv, native = fixture()
        native["buffers"][0]["members"][0]["type"]["elements"] = 2
        with self.assertRaisesRegex(ValueError, "array length"):
            backend.compare_dxil_reflection(spirv, native)

    def test_native_buffer_padding(self):
        spirv, native = fixture()
        spirv["types"]["_1"]["members"][0]["type"] = "float"
        spirv["ubos"][0]["block_size"] = 4
        member = native["buffers"][0]["members"][0]
        member["size"] = 4
        member["type"].update({"class": 0, "columns": 1})
        backend.compare_dxil_reflection(spirv, native)

    def test_duplicate_native_buffers(self):
        spirv, native = fixture()
        native["buffers"].append(native["buffers"][0])
        with self.assertRaisesRegex(ValueError, "buffer list"):
            backend.compare_dxil_reflection(spirv, native)

    def test_reflection_library_changes_invalidate_cache(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        args = SimpleNamespace(reflector=Path("CCBuild.exe"))
        with patch.object(backend, "digest", return_value="before"), patch.object(Path, "is_file", return_value=True):
            before = backend.reflection_tool_hashes(args, Path("dxc.exe"))
        with patch.object(backend, "digest", side_effect=lambda p: "after" if p.name == "dxcompiler.dll" else "before"), patch.object(Path, "is_file", return_value=True):
            self.assertNotEqual(before, backend.reflection_tool_hashes(args, Path("dxc.exe")))


if __name__ == "__main__":
    unittest.main()
