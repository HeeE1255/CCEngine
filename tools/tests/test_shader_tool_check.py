import importlib.util
from pathlib import Path
from types import SimpleNamespace
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).parents[1] / "CompileShaderBackends.py"
spec = importlib.util.spec_from_file_location("tool_check_backend", SCRIPT)
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


class ToolCheck(unittest.TestCase):
    def args(self):
        return SimpleNamespace(target=["vulkan"], include=["private"], define=["PRIVATE"],
                               vertex_entry="CustomVS", pixel_entry="CustomPS", timeout=5,
                               reflector=None)

    def test_isolated_probe_and_cleanup(self):
        args = self.args()
        visited = []

        def compile_probe(source, root, folder, probe, dxc, cross):
            visited.append(root)
            self.assertTrue(source.is_file())
            self.assertEqual([], probe.include)
            self.assertEqual([], probe.define)
            self.assertEqual("VSMain", probe.vertex_entry)
            self.assertEqual(5, probe.timeout)
            return {"stages": {"vertex": {"outputs": {"vulkan": {}}},
                               "pixel": {"outputs": {"vulkan": {}}}}}

        with patch.object(backend, "compile_source", side_effect=compile_probe), \
                patch.object(backend, "digest", return_value="hash"), \
                patch.object(backend, "reflection_tool_hashes", return_value=None):
            result = backend.check_backend_tools(args, Path("dxc"), Path("cross"))
        self.assertTrue(result["success"])
        self.assertEqual(["vulkan"], result["stages"]["pixel"])
        self.assertFalse(visited[0].exists())
        self.assertEqual(["PRIVATE"], args.define)
        self.assertEqual("CustomVS", args.vertex_entry)

    def test_failure_retains_diagnostic_and_cleans_up(self):
        visited = []

        def fail(source, root, folder, *unused):
            visited.append(root)
            folder.mkdir()
            (folder / "vertex.log").write_text("missing dependent DLL", encoding="utf-8")
            raise RuntimeError("tool exited")

        with patch.object(backend, "compile_source", side_effect=fail):
            with self.assertRaisesRegex(RuntimeError, "missing dependent DLL"):
                backend.check_backend_tools(self.args(), Path("dxc"), Path("cross"))
        self.assertFalse(visited[0].exists())

    def test_probe_rejects_publication_arguments(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--check-tools", "--output", "unused"],
                                capture_output=True, timeout=10)
        self.assertEqual(2, result.returncode)
        self.assertIn(b"accepts only", result.stderr)

    def test_normal_compile_still_requires_paths(self):
        result = subprocess.run([sys.executable, str(SCRIPT)],
                                capture_output=True, timeout=10)
        self.assertEqual(2, result.returncode)
        self.assertIn(b"--source-root and --output are required", result.stderr)

    def test_check_log_records_tool_resolution_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "diagnostic.log"
            result = subprocess.run([sys.executable, str(SCRIPT), "--check-tools",
                                     "--build-log", str(log), "--dxc", str(Path(directory) / "missing.exe")],
                                    capture_output=True, timeout=10)
            self.assertEqual(1, result.returncode)
            self.assertEqual(b"", result.stdout)
            self.assertIn("Required tool not found", log.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
