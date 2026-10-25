import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from concurrent.futures import ThreadPoolExecutor
from threading import Barrier

spec = importlib.util.spec_from_file_location("shader_backends", Path(__file__).parents[1] / "CompileShaderBackends.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)

TEST_ROOT = Path(__file__).parents[2] / "local" / "qa" / "shader-input-tests"
TEST_ROOT.mkdir(parents=True, exist_ok=True)

def temporary_directory():
    return tempfile.TemporaryDirectory(dir=TEST_ROOT)


class ShaderInputs(unittest.TestCase):
    def exercise_cache(self, failure):
        with temporary_directory() as directory:
            root = Path(directory)
            source = root / "main.hlsl"
            source.write_text("float value;", encoding="utf-8")
            args = SimpleNamespace(include=[], define=[], target=["dx12"],
                                   vertex_entry="VS", pixel_entry="PS", cache=root / "cache")
            def compile_stub(source, root, folder, *unused):
                folder.mkdir(parents=True)
                (folder / "vertex.dxil").write_bytes(b"compiled")
                return {"source": "main.hlsl"}
            with patch.object(backend, "compile_source", side_effect=compile_stub) as compiler:
                backend.compile_cached_source(source, root, root / "first", args, source, source)
                entry = next(args.cache.iterdir())
                if failure == "corrupt":
                    (entry / "files" / "vertex.dxil").write_bytes(b"corrupt")
                real_copy = backend.shutil.copyfile
                failed = False
                def copy_once(src, dst, *positional, **keywords):
                    nonlocal failed
                    if failure == "copy" and not failed and ".cache-tmp" in str(dst):
                        failed = True
                        Path(dst).write_bytes(b"partial")
                        raise OSError("simulated interrupted copy")
                    return real_copy(src, dst, *positional, **keywords)
                with patch.object(backend.shutil, "copyfile", side_effect=copy_once):
                    backend.compile_cached_source(source, root, root / "second", args, source, source)
                self.assertEqual(b"compiled", (root / "second" / "vertex.dxil").read_bytes())
                self.assertEqual(2, compiler.call_count)
                backend.compile_cached_source(source, root, root / "third", args, source, source)
                self.assertEqual(2, compiler.call_count, "repaired cache must be reused")
                self.assertFalse(list(root.glob("*.cache-tmp")))
                self.assertFalse(list(args.cache.glob("*.invalid")))

    def test_corrupt_cache_rebuilt_and_reused(self):
        self.exercise_cache("corrupt")

    def test_partial_cache_copy_falls_back_cleanly(self):
        self.exercise_cache("copy")

    def test_concurrent_cache_publication(self):
        with temporary_directory() as directory:
            root = Path(directory)
            source = root / "main.hlsl"
            source.write_text("float value;", encoding="utf-8")
            args = SimpleNamespace(include=[], define=[], target=["dx12"],
                                   vertex_entry="VS", pixel_entry="PS", cache=root / "cache")
            barrier = Barrier(2)
            def compile_stub(source, root, folder, *unused):
                folder.mkdir(parents=True)
                (folder / "vertex.dxil").write_bytes(b"compiled")
                barrier.wait(timeout=10)
                return {"source": "main.hlsl"}
            with patch.object(backend, "compile_source", side_effect=compile_stub) as compiler:
                with ThreadPoolExecutor(max_workers=2) as pool:
                    futures = [pool.submit(backend.compile_cached_source, source, root,
                                           root / name, args, source, source) for name in ("a", "b")]
                    for future in futures:
                        future.result(timeout=15)
                backend.compile_cached_source(source, root, root / "c", args, source, source)
                self.assertEqual(2, compiler.call_count)
                self.assertEqual(1, len(list(args.cache.iterdir())))

    def test_nested_include_invalidation(self):
        with temporary_directory() as directory:
            root = Path(directory)
            source = root / "main.hlsl"
            source.write_text('#include "shared.hlsli"\n', encoding="utf-8")
            include = root / "shared.hlsli"
            include.write_text("float value = 1;", encoding="utf-8")
            before = backend.shader_input_hash(source, root, [])
            include.write_text("float value = 2;", encoding="utf-8")
            self.assertNotEqual(before, backend.shader_input_hash(source, root, []))

    def test_include_cycle_terminates(self):
        with temporary_directory() as directory:
            root = Path(directory)
            (root / "a.hlsl").write_text('#include "b.hlsli"', encoding="utf-8")
            (root / "b.hlsli").write_text('#include "a.hlsl"', encoding="utf-8")
            self.assertEqual(2, len(backend.shader_input_hash(root / "a.hlsl", root, [])))

    def test_missing_include_rejected(self):
        with temporary_directory() as directory:
            root = Path(directory)
            source = root / "main.hlsl"
            source.write_text('#include "missing.hlsli"', encoding="utf-8")
            with self.assertRaises(ValueError):
                backend.shader_input_hash(source, root, [])

    def test_macro_include_rejected(self):
        with temporary_directory() as directory:
            root = Path(directory)
            source = root / "main.hlsl"
            source.write_text('#include HEADER_NAME', encoding="utf-8")
            with self.assertRaises(ValueError):
                backend.shader_input_hash(source, root, [])

    def test_invalid_reflection_slot_rejected(self):
        with self.assertRaises(ValueError):
            backend.validate_reflection({"ubos": [{"name": "Bad", "binding": 8, "set": 0}]}, "vertex")

    def test_vertex_texture_rejected(self):
        with self.assertRaises(ValueError):
            backend.validate_reflection({"separate_images": [{"name": "Bad", "binding": 8, "set": 0}]}, "vertex")


if __name__ == "__main__":
    unittest.main()
