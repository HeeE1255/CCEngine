import unittest
from test_shader_buffer_layout import backend


class RenderContract(unittest.TestCase):
    def test_vertex_input_metadata(self):
        self.assertEqual(backend.vertex_inputs({"inputs": [
            {"name": "in.var.TEXCOORD0", "type": "vec2", "location": 7}]}),
            [{"semantic": "TEXCOORD", "type": "vec2", "location": 7}])

    def test_vertex_input_rejections(self):
        for item in ({"name": "POSITION", "type": "vec3", "location": 0},
                     {"name": "in.var.POSITION", "type": "vec3", "location": 32},
                     {"name": "in.var.POSITION", "type": "mat4", "location": 0},
                     {"name": "in.var.POSITION", "type": "vec3", "location": True}):
            with self.assertRaises(ValueError):
                backend.vertex_inputs({"inputs": [item]})

    def test_vertex_semantic_alias_collision(self):
        with self.assertRaises(ValueError):
            backend.vertex_inputs({"inputs": [
                {"name":"in.var.TEXCOORD", "type":"vec2", "location":0},
                {"name":"in.var.TEXCOORD0", "type":"vec2", "location":1}]})

    def test_texture_mapping_uses_register_not_order(self):
        images = [{"name": "ZDetail", "binding": 11}, {"name": "AMask", "binding": 15}]
        expected = {"ZDetail": 3, "AMask": 7}
        self.assertEqual(backend.texture_bindings({"separate_images": images}), expected)
        self.assertEqual(backend.texture_bindings({"separate_images": images[::-1]}), expected)

    def test_texture_mapping_rejects_conflicts(self):
        for images in (
                [{"name": "A", "binding": 8}, {"name": "B", "binding": 8}],
                [{"name": "A", "binding": 8}, {"name": "A", "binding": 9}],
                [{"name": "A", "binding": 40}]):
            with self.assertRaisesRegex(ValueError, "Invalid texture"):
                backend.texture_bindings({"separate_images": images})

    def test_no_active_textures(self):
        self.assertEqual(backend.texture_bindings({}), {})

    def pixel(self, kind="int", location=1):
        return {"outputs": [{"location": 0, "type": "vec4"},
                            {"location": location, "type": kind}]}

    def test_color_and_signed_picking(self):
        backend.validate_render_contract(self.pixel(), "pixel")

    def test_wrong_picking_type(self):
        for kind in ("float", "uint", "vec4"):
            with self.assertRaisesRegex(ValueError, "render target"):
                backend.validate_render_contract(self.pixel(kind), "pixel")

    def test_extra_or_duplicate_target(self):
        for location in (0, 2):
            with self.assertRaisesRegex(ValueError, "render target"):
                backend.validate_render_contract(self.pixel(location=location), "pixel")

    def test_missing_color(self):
        with self.assertRaisesRegex(ValueError, "target 0"):
            backend.validate_render_contract({"outputs": []}, "pixel")

    def test_reserved_textures(self):
        for name, binding in (("AlbedoTexture", 8), ("g_NormalMap", 9)):
            data = self.pixel()
            data["separate_images"] = [{"name": name, "type": "texture2D", "binding": binding}]
            backend.validate_render_contract(data, "pixel")
            data["separate_images"][0]["binding"] = 10
            with self.assertRaisesRegex(ValueError, "texture slot"):
                backend.validate_render_contract(data, "pixel")

    def test_mesh_inputs(self):
        data = {"ubos": [{"name": "type.CameraBuffer"}], "inputs": [
            {"name": "in.var.POSITION", "type": "vec3"},
            {"name": "in.var.BONEIDS", "type": "ivec4"}]}
        backend.validate_render_contract(data, "vertex")
        data["inputs"][1]["type"] = "vec4"
        with self.assertRaisesRegex(ValueError, "vertex input"):
            backend.validate_render_contract(data, "vertex")

    def test_mixed_mesh_quad(self):
        data = {"ubos": [{"name": "Camera"}], "inputs": [
            {"name": "in.var.NORMAL", "type": "vec3"},
            {"name": "in.var.COLOR", "type": "vec4"}]}
        with self.assertRaisesRegex(ValueError, "vertex input"):
            backend.validate_render_contract(data, "vertex")

    def test_custom_pipeline_not_assumed_mesh(self):
        backend.validate_render_contract(
            {"inputs": [{"name": "in.var.POSITION", "type": "vec2"}]}, "vertex")


if __name__ == "__main__":
    unittest.main()
