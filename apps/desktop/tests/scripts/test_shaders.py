"""The checked-in CRT shader (resources/shaders/crt.spv) is built from the current crt.frag: the hash
recorded by scripts/build_shaders.sh must match the source, and the binary must be SPIR-V."""

import hashlib
import pathlib
import struct
import unittest

SHADERS = pathlib.Path(__file__).resolve().parents[2] / "resources" / "shaders"


class ShaderTest(unittest.TestCase):
    def test_spv_built_from_current_source(self):
        recorded = (SHADERS / "crt.spv.sha256").read_text().split()[0]
        actual = hashlib.sha256((SHADERS / "crt.frag").read_bytes()).hexdigest()
        self.assertEqual(recorded, actual, "crt.frag changed: run scripts/build_shaders.sh <raven dir>")

    def test_spv_is_spirv(self):
        data = (SHADERS / "crt.spv").read_bytes()
        self.assertGreater(len(data), 20)
        self.assertEqual(len(data) % 4, 0)
        (magic,) = struct.unpack("<I", data[:4])
        self.assertEqual(magic, 0x07230203)


if __name__ == "__main__":
    unittest.main()
