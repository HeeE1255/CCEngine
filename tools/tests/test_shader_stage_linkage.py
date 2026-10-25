import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("shader_backends", Path(__file__).parents[1] / "CompileShaderBackends.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


def parameter(name, mask=15, active=15):
    return {"semantic": name, "index": 0, "mask": mask, "activeMask": active,
            "componentType": 3, "precision": 0, "stream": 0}


def pair():
    return {"outputs": [parameter("SV_POSITION"), parameter("TEXCOORD")]}, {"inputs": [parameter("TEXCOORD")]}


class StageLinkage(unittest.TestCase):
    def test_matching_signatures(self):
        backend.validate_dxil_stage_linkage(*pair())

    def test_missing_output(self):
        vertex, pixel = pair()
        pixel["inputs"][0]["index"] = 1
        with self.assertRaisesRegex(ValueError, "no vertex output"):
            backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_component_type_mismatch(self):
        vertex, pixel = pair()
        pixel["inputs"][0]["componentType"] = 2
        with self.assertRaisesRegex(ValueError, "type mismatch"):
            backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_unwritten_component(self):
        vertex, pixel = pair()
        vertex["outputs"][1]["activeMask"] = 3
        with self.assertRaisesRegex(ValueError, "unwritten"):
            backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_register_packing_independent(self):
        vertex, pixel = pair()
        vertex["outputs"][1].update(mask=12, activeMask=12)
        pixel["inputs"][0].update(mask=3, activeMask=3)
        backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_rasterizer_generated_input(self):
        vertex, pixel = pair()
        pixel["inputs"].append(parameter("SV_IsFrontFace", 1, 1))
        backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_unused_input(self):
        vertex, pixel = pair()
        pixel["inputs"].append(parameter("Unused", 15, 0))
        backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_missing_position(self):
        vertex, pixel = pair()
        vertex["outputs"].pop(0)
        with self.assertRaisesRegex(ValueError, "SV_POSITION"):
            backend.validate_dxil_stage_linkage(vertex, pixel)

    def test_spirv_matching_locations(self):
        backend.validate_spirv_stage_linkage({"outputs": [{"location": 0, "type": "vec2"}]},
                                            {"inputs": [{"location": 0, "type": "vec2"}]})

    def test_spirv_type_mismatch(self):
        with self.assertRaisesRegex(ValueError, "interface mismatch"):
            backend.validate_spirv_stage_linkage({"outputs": [{"location": 0, "type": "vec2"}]},
                                                {"inputs": [{"location": 0, "type": "ivec2"}]})

    def test_spirv_unsupported_array(self):
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            backend.validate_spirv_stage_linkage({"outputs": [{"location": 0, "type": "vec2", "array": [2]}]}, {})


if __name__ == "__main__":
    unittest.main()
