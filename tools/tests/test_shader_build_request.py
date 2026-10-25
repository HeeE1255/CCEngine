import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "build_request_backend", Path(__file__).parents[1] / "CompileShaderBackends.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


class BuildRequest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / "sample.hlsl").write_text("// shader", encoding="utf-8")
        self.tool = self.root / "tool.exe"
        self.tool.write_bytes(b"tool")
        self.request = self.root / "request.json"
        self.request.write_text(json.dumps({"schema": 1, "shaders": [
            {"source": "sample.hlsl", "defines": ["B", "A", "A"]}]}), encoding="utf-8")
        self.output = self.root / "output"
        self.args = SimpleNamespace(build_request=self.request, target=["vulkan"],
                                    define=["ORIGINAL"], reflector=None)

    def compile_fixture(self, source, root, folder, args, dxc, cross):
        folder.mkdir()
        target = args.target[0]
        suffix = {"dx12": "dxil", "opengl": "glsl", "vulkan": "spv"}[target]
        stages = {}
        for stage in ("vertex", "pixel"):
            path = folder / f"{stage}.{suffix}"
            path.write_bytes(stage.encode())
            stages[stage] = {"outputs": {target: {
                "file": path.name, "sha256": backend.digest(path)}}, "reflection": "reflection.json"}
        (folder / "reflection.json").write_text("{}", encoding="utf-8")
        (folder / "intermediate.spv").write_bytes(b"intermediate")
        return {"stages": stages, "materialPropertySource": ""}

    def run_request(self, compile_fn=None):
        with patch.object(backend, "compile_cached_source", side_effect=compile_fn or self.compile_fixture), \
                patch.object(backend, "check_backend_tools", return_value={
                    "scope": "offline-compile-reflection-not-gpu", "targets": self.args.target,
                    "stages": {"vertex": self.args.target, "pixel": self.args.target}}):
            return backend.compile_build_request(
                self.args, self.root, self.output, self.tool, self.tool)

    def test_each_api_retains_only_referenced_outputs(self):
        for target, api in (("dx12", "DirectX12"), ("opengl", "OpenGL"), ("vulkan", "Vulkan")):
            with self.subTest(target=target):
                self.args.target = [target]
                self.output = self.root / target
                self.assertEqual(0, self.run_request())
                index = json.loads((self.output / "packages.json").read_text())
                self.assertEqual(api, index["graphicsAPI"])
                manifest_path = self.output / index["packages"][0]
                manifest = json.loads(manifest_path.read_text())
                self.assertEqual([target], manifest["targets"])
                self.assertEqual(["A", "B"], manifest["defines"])
                self.assertEqual(["ORIGINAL"], self.args.define)
                stages = manifest["shaders"][0]["stages"]
                expected = {stage["outputs"][target]["file"] for stage in stages.values()}
                self.assertEqual(expected, {p.name for p in (manifest_path.parent / "bytecode").iterdir()})
                self.assertTrue(all("reflection" not in stage for stage in stages.values()))
                diagnostic = json.loads((self.output / "tool-check.json").read_text())
                self.assertEqual([target], diagnostic["targets"])
                self.assertNotIn(str(self.root), json.dumps(diagnostic))

    def test_preflight_failure_creates_no_output(self):
        with patch.object(backend, "check_backend_tools", side_effect=RuntimeError("probe failed")), \
                patch.object(backend, "compile_cached_source") as compile_shader:
            with self.assertRaisesRegex(RuntimeError, "probe failed"):
                backend.compile_build_request(self.args, self.root, self.output, self.tool, self.tool)
            compile_shader.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_multi_target_rejected_before_output_creation(self):
        self.args.target = ["opengl", "vulkan"]
        with self.assertRaisesRegex(ValueError, "exactly one target"):
            self.run_request()
        self.assertFalse(self.output.exists())

    def test_invalid_artifact_never_publishes_completion(self):
        for mutation in ("hash", "path", "missing", "duplicate", "target"):
            with self.subTest(mutation=mutation):
                self.output = self.root / mutation

                def compile_invalid(*args):
                    result = self.compile_fixture(*args)
                    vertex = result["stages"]["vertex"]["outputs"]["vulkan"]
                    if mutation == "hash":
                        vertex["sha256"] = "incorrect"
                    elif mutation == "path":
                        vertex["file"] = "../sample.hlsl"
                    elif mutation == "missing":
                        (args[2] / vertex["file"]).unlink()
                    elif mutation == "duplicate":
                        result["stages"]["pixel"]["outputs"]["vulkan"] = dict(vertex)
                    else:
                        result["stages"]["vertex"]["outputs"] = {"opengl": vertex}
                    return result

                with self.assertRaises(ValueError):
                    self.run_request(compile_invalid)
                self.assertFalse((self.output / "packages.json").exists())

    def test_existing_output_is_not_overwritten(self):
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_text("keep", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "never overwritten"):
            self.run_request()
        self.assertEqual("keep", sentinel.read_text())


if __name__ == "__main__":
    unittest.main()
