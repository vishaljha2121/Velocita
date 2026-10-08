"""Build settings that safety depends on."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RaftLibraryKeepsItsAssertions(unittest.TestCase):
    def test_release_builds_do_not_define_ndebug_for_the_raft_library(self):
        text = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("target_compile_options(willemt_raft PRIVATE -UNDEBUG)", text)


if __name__ == "__main__":
    unittest.main()
