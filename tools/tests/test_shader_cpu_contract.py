import copy
import unittest

from test_shader_buffer_layout import backend, reflection


def matrix():
    return {"name": "ViewProjection", "type": "mat4", "offset": 0,
            "matrix_stride": 16, "row_major": True}


class CpuContracts(unittest.TestCase):
    def test_scene_scalar_padding_for_gl(self):
        data = reflection([
            {"name": "Lights", "type": "_light", "offset": 0, "array": [4],
             "array_size_is_literal": [True], "array_stride": 32},
            {"name": "Count", "type": "int", "offset": 128},
            *[{"name": f"Pad{i}", "type": "float", "offset": 132 + 4*i} for i in range(3)]
        ], 144, "SceneBuffer", 3)
        data["types"]["_light"] = {"members": [
            {"name": "Direction", "type": "vec3", "offset": 0},
            {"name": "Intensity", "type": "float", "offset": 12},
            {"name": "Color", "type": "vec3", "offset": 16},
            {"name": "Padding", "type": "float", "offset": 28}]}
        backend.validate_buffer_layouts(data)
        data["types"]["_1"]["members"][3]["type"] = "int"
        with self.assertRaisesRegex(ValueError, "CPU upload contract"):
            backend.validate_buffer_layouts(data)

    def test_camera_prefix(self):
        backend.validate_buffer_layouts(reflection([matrix()], 64, "type.CameraBuffer"))

    def test_camera_full(self):
        backend.validate_buffer_layouts(reflection([matrix(),
            {"name": "Position", "type": "vec3", "offset": 64},
            {"name": "Padding", "type": "float", "offset": 76}], 80, "CameraBuffer"))

    def test_wrong_slot(self):
        with self.assertRaisesRegex(ValueError, "CPU upload contract"):
            backend.validate_buffer_layouts(reflection([matrix()], 64, "CameraBuffer", 1))

    def test_wrong_matrix_major(self):
        field = matrix()
        field["row_major"] = False
        with self.assertRaisesRegex(ValueError, "CPU upload contract"):
            backend.validate_buffer_layouts(reflection([field], 64, "CameraBuffer"))

    def test_bone_capacity(self):
        for count in (100, 512, 513):
            field = matrix()
            field.update(array=[count], array_size_is_literal=[True], array_stride=64)
            data = reflection([field], count * 64, "BoneBuffer", 2)
            if count == 512:
                backend.validate_buffer_layouts(data)
            else:
                with self.assertRaisesRegex(ValueError, "CPU upload contract"):
                    backend.validate_buffer_layouts(data)

    def test_transform_integer_flag(self):
        data = reflection([matrix(),
            {"name": "Color", "type": "vec4", "offset": 64},
            {"name": "Entity", "type": "int", "offset": 80},
            {"name": "Animated", "type": "int", "offset": 84},
            {"name": "Padding", "type": "vec2", "offset": 88}], 96, "TransformBuffer", 1)
        backend.validate_buffer_layouts(data)
        data["types"]["_1"]["members"][3]["type"] = "float"
        with self.assertRaisesRegex(ValueError, "CPU upload contract"):
            backend.validate_buffer_layouts(data)

    def test_scene_nested_layout(self):
        data = reflection([
            {"name": "Lights", "type": "_light", "offset": 0, "array": [4],
             "array_size_is_literal": [True], "array_stride": 32},
            {"name": "Count", "type": "int", "offset": 128},
            {"name": "Padding", "type": "vec3", "offset": 132}], 144, "SceneBuffer", 3)
        data["types"]["_light"] = {"members": [
            {"name": "Direction", "type": "vec3", "offset": 0},
            {"name": "Intensity", "type": "float", "offset": 12},
            {"name": "Color", "type": "vec3", "offset": 16},
            {"name": "Padding", "type": "float", "offset": 28}]}
        backend.validate_buffer_layouts(data)
        invalid = copy.deepcopy(data)
        invalid["types"]["_light"]["members"][1]["type"] = "int"
        with self.assertRaisesRegex(ValueError, "CPU upload contract"):
            backend.validate_buffer_layouts(invalid)


if __name__ == "__main__":
    unittest.main()
