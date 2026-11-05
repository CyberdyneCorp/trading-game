"""Tests for apps/desktop/scripts/check_raven_pin.py (spec "Pin drift")."""

import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "check_raven_pin.py"
spec = importlib.util.spec_from_file_location("check_raven_pin", SCRIPT)
pin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pin)

GOOD = "0fc738631c33844d6da732210267d21b5223f960"
OTHER = "1111111111111111111111111111111111111111"
VULKAN = "b379292b2ab6df5771ba9870d53cf8b2c9295daf"


def make_tree(root: Path, cmake_sha: str, readme_sha: str, design_sha: str) -> None:
    files = {
        "apps/desktop/cmake/Dependencies.cmake": f"set(TRADELAB_RAVEN_SHA {cmake_sha})\nset(TRADELAB_VULKAN_HEADERS_SHA {VULKAN})\n",
        "apps/desktop/README.md": f"Raven is pinned at `{readme_sha}`.\nVulkan-Headers `{VULKAN}`.\n",
        "openspec/changes/add-native-clients/design.md": f"- **Pin:** Raven commit `{design_sha}`.\n",
    }
    for rel, text in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


class CheckRavenPinTest(unittest.TestCase):
    def run_check(self, cmake_sha, readme_sha, design_sha):
        with tempfile.TemporaryDirectory() as tmp:
            make_tree(Path(tmp), cmake_sha, readme_sha, design_sha)
            return pin.check(Path(tmp))

    def test_agreeing_pins_pass(self):
        self.assertEqual(self.run_check(GOOD, GOOD, GOOD), [])

    def test_cmake_bumped_without_docs_fails(self):
        problems = self.run_check(OTHER, GOOD, GOOD)
        self.assertTrue(any("README.md" in p for p in problems), problems)
        self.assertTrue(any("design.md" in p for p in problems), problems)

    def test_mismatched_readme_fails(self):
        problems = self.run_check(GOOD, OTHER, GOOD)
        self.assertEqual(len(problems), 2, problems)  # names another commit and misses the pinned one
        self.assertTrue(all("README.md" in p for p in problems), problems)

    def test_other_pins_are_ignored(self):
        self.assertEqual(self.run_check(GOOD, GOOD, GOOD), [])  # the Vulkan-Headers sha is not Raven's

    def test_repository_is_consistent(self):
        self.assertEqual(pin.check(Path(__file__).resolve().parents[4]), [])


if __name__ == "__main__":
    unittest.main()
