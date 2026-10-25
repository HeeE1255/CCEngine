import struct
import unittest
from test_shader_buffer_layout import backend


def module(storage, flags=()):
    words = [0x07230203, 0x10000, 0, 10, 0,
             (4 << 16) | 71, 2, 30, 0,
             (4 << 16) | 59, 1, 2, storage]
    for flag in flags:
        words += [(3 << 16) | 71, 2, flag]
    return struct.pack("<" + "I" * len(words), *words)


class Interpolation(unittest.TestCase):
    def check(self, vertex=(), pixel=(), kind="vec2"):
        backend.validate_interpolation(module(3, vertex), module(1, pixel),
                                       {"inputs": [{"location": 0, "type": kind}]})

    def test_smooth(self):
        self.check()

    def test_flat_integer(self):
        self.check((14,), (14,), "int")

    def test_integer_needs_flat(self):
        with self.assertRaisesRegex(ValueError, "requires flat"):
            self.check(kind="int")

    def test_mode_mismatch(self):
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.check((), (13,))

    def test_noperspective(self):
        self.check((13,), (13,))

    def test_centroid_pixel_only(self):
        self.check((), (16,))

    def test_sample_needs_pipeline(self):
        with self.assertRaisesRegex(ValueError, "MSAA"):
            self.check((), (17,))

    def test_conflicting_flags(self):
        with self.assertRaisesRegex(ValueError, "conflicting"):
            self.check((14, 13), (14,))

    def test_malformed_instruction(self):
        with self.assertRaisesRegex(ValueError, "Truncated"):
            backend.spirv_interpolation(module(3) + struct.pack("<I", 0), 3)

    def test_missing_location(self):
        with self.assertRaisesRegex(ValueError, "Missing"):
            backend.validate_interpolation(module(1), module(1), {"inputs": [
                {"location": 0, "type": "vec2"}]})


if __name__ == "__main__":
    unittest.main()
