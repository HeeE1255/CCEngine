import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("shader_backends", Path(__file__).parents[1] / "CompileShaderBackends.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


def reflection(members, size=32, name="TestBuffer", binding=0):
    return {"types": {"_1": {"members": members}}, "ubos": [
        {"name": name, "type": "_1", "binding": binding, "set": 0, "block_size": size}]}


class BufferLayouts(unittest.TestCase):
    def test_repack_rounds_gl_block_tail_to_std140_alignment(self):
        data = reflection([{"name": "value", "type": "float", "offset": 0}], 4)
        mapping = backend.build_uniform_repack(data, data)[0]
        self.assertEqual(4, mapping["sourceSize"])
        self.assertEqual(16, mapping["destinationSize"])
        self.assertEqual([{"source": 0, "destination": 0, "size": 4}], mapping["copies"])

    def test_repack_merges_stage_specific_and_shared_bindings(self):
        shared = reflection([{"name": "value", "type": "float", "offset": 0}], 16)
        pixel = reflection([{"name": "tint", "type": "vec4", "offset": 0}], 16, binding=2)
        result = backend.merge_uniform_repack([(shared, shared), (pixel, pixel), (shared, shared)])
        self.assertEqual("cc-uniform-repack-v1", result["contract"])
        self.assertEqual([0, 2], [block["binding"] for block in result["buffers"]])

    def test_repack_rejects_cross_stage_semantics_with_identical_copy_ranges(self):
        vertex = reflection([{"name": "value", "type": "float", "offset": 0}], 16)
        pixel = reflection([{"name": "value", "type": "int", "offset": 0}], 16)
        with self.assertRaisesRegex(ValueError, "vertex/pixel uniform layout mismatch"):
            backend.merge_uniform_repack([(vertex, vertex), (pixel, pixel)])

    def test_repack_rejects_cross_stage_destination_layout_change(self):
        source = reflection([{"name": "value", "type": "float", "offset": 0}], 16)
        destination = reflection([{"name": "value", "type": "float", "offset": 4}], 16)
        with self.assertRaisesRegex(ValueError, "vertex/pixel uniform layout mismatch"):
            backend.merge_uniform_repack([(source, source), (source, destination)])

    def test_repack_nested_array_preserves_values_not_padding(self):
        src = reflection([{"name": "items", "type": "_2", "offset": 0,
                           "array": [2], "array_size_is_literal": [True], "array_stride": 12}], 24)
        dst = reflection([{"name": "items", "type": "_3", "offset": 0,
                           "array": [2], "array_size_is_literal": [True], "array_stride": 16}], 32)
        src["types"]["_2"] = {"members": [{"name": "v", "type": "vec3", "offset": 0}]}
        dst["types"]["_3"] = {"members": [{"name": "v", "type": "vec3", "offset": 0}]}
        mapping = backend.build_uniform_repack(src, dst)[0]
        self.assertEqual(24, mapping["sourceSize"])
        self.assertEqual(32, mapping["destinationSize"])
        self.assertEqual([{"source": 0, "destination": 0, "size": 12},
                          {"source": 12, "destination": 16, "size": 12}], mapping["copies"])

    def test_repack_rectangular_matrix_changes_storage_order(self):
        src = reflection([{"name": "m", "type": "mat2x3", "offset": 0,
                           "matrix_stride": 16}], 32)
        dst = reflection([{"name": "m", "type": "mat2x3", "offset": 0,
                           "matrix_stride": 16, "row_major": True}], 48)
        mapping = backend.build_uniform_repack(src, dst)[0]
        pairs = {(c["source"], c["destination"]) for c in mapping["copies"]}
        self.assertEqual({(0, 0), (16, 4), (4, 16), (20, 20), (8, 32), (24, 36)}, pairs)

    def test_repack_rejects_semantic_change(self):
        src = reflection([{"name": "value", "type": "float", "offset": 0}], 16)
        dst = reflection([{"name": "value", "type": "int", "offset": 0}], 16)
        with self.assertRaisesRegex(ValueError, "type differs"):
            backend.build_uniform_repack(src, dst)

    def test_gl_reports_packed_vector(self):
        data = reflection([{"name": "padding", "type": "vec3", "offset": 132}], 144)
        issues = backend.describe_gl_layout_conflicts(data)
        self.assertEqual(["TestBuffer (b0).padding: offset=132, std140 alignment=16"], issues)

    def test_gl_nested_array_and_matrix_diagnostics(self):
        data = reflection([{"name": "items", "type": "_2", "offset": 0,
                            "array": [2], "array_stride": 28}])
        data["types"]["_2"] = {"members": [
            {"name": "basis", "type": "mat2", "offset": 0, "matrix_stride": 8}]}
        issues = backend.describe_gl_layout_conflicts(data)
        self.assertTrue(any("array_stride=28" in issue for issue in issues))
        self.assertTrue(any("items[].basis" in issue and "matrix_stride=8" in issue for issue in issues))

    def test_gl_does_not_flag_vec3_scalar_packing(self):
        data = reflection([{"name": "direction", "type": "vec3", "offset": 0},
                           {"name": "power", "type": "float", "offset": 12}], 16)
        self.assertEqual([], backend.describe_gl_layout_conflicts(data))

    def test_packed_float3_and_scalar(self):
        backend.validate_reflection(reflection([
            {"name": "direction", "type": "vec3", "offset": 0},
            {"name": "power", "type": "float", "offset": 12}], 16), "pixel")

    def test_overlap_rejected(self):
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            backend.validate_buffer_layouts(reflection([
                {"name": "a", "type": "vec4", "offset": 0},
                {"name": "b", "type": "float", "offset": 12}]))

    def test_out_of_bounds_rejected(self):
        with self.assertRaisesRegex(ValueError, "exceed"):
            backend.validate_buffer_layouts(reflection([{"name": "a", "type": "vec4", "offset": 20}]))

    def test_short_array_stride_rejected(self):
        with self.assertRaisesRegex(ValueError, "array stride"):
            backend.validate_buffer_layouts(reflection([{"name": "a", "type": "vec4", "offset": 0,
                "array": [2], "array_size_is_literal": [True], "array_stride": 4}]))

    def test_nonliteral_array_rejected(self):
        with self.assertRaisesRegex(ValueError, "fixed one-dimensional"):
            backend.validate_buffer_layouts(reflection([{"name": "a", "type": "vec4", "offset": 0,
                "array": [2], "array_size_is_literal": [False], "array_stride": 16}]))

    def test_recursive_type_rejected(self):
        with self.assertRaisesRegex(ValueError, "recursive"):
            backend.validate_buffer_layouts(reflection([{"name": "a", "type": "_1", "offset": 0}]))

    def test_rectangular_matrix_layout(self):
        backend.validate_buffer_layouts(reflection([
            {"name": "m", "type": "mat2x3", "offset": 0, "matrix_stride": 16}], 28))
        backend.validate_buffer_layouts(reflection([
            {"name": "m", "type": "mat2x3", "offset": 0, "matrix_stride": 16, "row_major": True}], 40))

    def test_material_contract(self):
        members = []
        for name, offset, count in [("AlbedoColor", 0, 0), ("PropertyColors", 16, 8),
                                    ("PropertyScalars", 144, 4), ("PropertyToggles", 208, 4),
                                    ("SurfaceValues", 272, 0)]:
            member = {"name": name, "type": "vec4", "offset": offset}
            if count:
                member.update(array=[count], array_size_is_literal=[True], array_stride=16)
            members.append(member)
        data = reflection(members, 288, "MaterialPropertyBuffer", 4)
        backend.validate_reflection(data, "pixel")
        data["ubos"][0]["name"] = "type.MaterialPropertyBuffer"
        backend.validate_reflection(data, "pixel")
        members[0]["name"] = "WrongField"
        with self.assertRaisesRegex(ValueError, "CPU layout mismatch"):
            backend.validate_reflection(data, "pixel")


if __name__ == "__main__":
    unittest.main()
