import importlib.util
import json
import os
from pathlib import Path
import tempfile
import time
import unittest


spec = importlib.util.spec_from_file_location(
    "cache_maintenance_backend", Path(__file__).parents[1] / "CompileShaderBackends.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


class CacheMaintenance(unittest.TestCase):
    def make_entry(self, root, key, age):
        entry = root / key
        (entry / "files").mkdir(parents=True)
        (entry / "files" / "vertex.spv").write_bytes(b"shader")
        (entry / "cache.json").write_text(json.dumps({
            "key": key, "files": {"vertex.spv": "hash"}}), encoding="utf-8")
        timestamp = time.time() - age * 86400
        os.utime(entry, (timestamp, timestamp))
        return entry

    def test_age_cleanup_preserves_recent_and_unknown(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = self.make_entry(root, "a" * 64, 31)
            recent = self.make_entry(root, "b" * 64, 1)
            unknown = root / "in-progress.tmp"
            unknown.mkdir()
            backend.prune_shader_cache(root)
            self.assertFalse(old.exists())
            self.assertTrue(recent.exists())
            self.assertTrue(unknown.exists())

    def test_budget_removes_oldest_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = self.make_entry(root, "a" * 64, 2)
            recent = self.make_entry(root, "b" * 64, 1)
            size = sum(p.stat().st_size for p in recent.rglob("*") if p.is_file())
            backend.prune_shader_cache(root, max_bytes=size)
            self.assertFalse(old.exists())
            self.assertTrue(recent.exists())

    def test_extra_files_prevent_deletion(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = self.make_entry(root, "a" * 64, 40)
            (entry / "personal.txt").write_text("keep", encoding="utf-8")
            backend.prune_shader_cache(root, max_bytes=0)
            self.assertTrue((entry / "personal.txt").exists())

    def test_orphan_cleanup_requires_marker_age_and_known_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entries = []
            for i, kind in enumerate(("old", "recent", "unmarked", "unknown")):
                key = str(i) * 64
                entry = root / (key + "." + "a" * 32 + ".tmp")
                (entry / "files").mkdir(parents=True)
                (entry / "files" / "vertex.spv").write_bytes(b"partial")
                if kind != "unmarked":
                    (entry / "owner.json").write_text(json.dumps({
                        "schema": 1, "kind": "cc-shader-cache-staging", "key": key}), encoding="utf-8")
                if kind == "unknown":
                    (entry / "files" / "personal.txt").write_text("keep", encoding="utf-8")
                age = time.time() - (1 if kind == "recent" else 8) * 86400
                for path in [*entry.rglob("*"), entry]:
                    os.utime(path, (age, age))
                entries.append(entry)
            backend.prune_shader_cache(root)
            self.assertFalse(entries[0].exists())
            self.assertTrue(all(entry.exists() for entry in entries[1:]))


if __name__ == "__main__":
    unittest.main()
