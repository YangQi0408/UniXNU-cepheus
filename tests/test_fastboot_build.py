import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_check", ROOT / "scripts/verify-fastboot-build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildChecks(unittest.TestCase):
    def setUp(self):
        self.source = Path("/build/xnu/bsd/dev/cepheus_fastboot.c")
        self.command = ("/xcode/clang -c -DKERNEL -DBSD_KERNEL_PRIVATE -fno-builtin -O2 "
                        "-I. -I/build/EXPORT_HDRS -MD -MF cepheus_fastboot.d -MP "
                        f"{self.source}")

    def test_uses_real_flags_without_writing_public_dependencies(self):
        args = build.compile_command(self.command, self.source)
        result = build.fixture_command(args, self.source, Path("/private/test.c"), Path("/private/test.o"))
        self.assertIn("-DKERNEL", result)
        self.assertIn("-I/build/EXPORT_HDRS", result)
        self.assertIn("/private/test.c", result)
        self.assertNotIn(str(self.source), result)
        self.assertNotIn("cepheus_fastboot.d", result)
        self.assertNotIn("-MD", result)
        self.assertNotIn("-MP", result)
        self.assertEqual(result[-2:], ["-o", "/private/test.o"])

    def test_absent_duplicate_or_non_kernel_compile_is_rejected(self):
        for log in ("", self.command + "\n" + self.command,
                    self.command.replace("-DKERNEL", ""),
                    self.command.replace("-fno-builtin", "")):
            with self.subTest(log=log), self.assertRaises(ValueError):
                build.compile_command(log, self.source)

    def test_all_output_overrides_are_removed(self):
        args = build.compile_command(self.command, self.source)
        args += ["-o", "public.o", "-MT", "public", "-MFpublic.d", "-MJ", "public.json"]
        result = build.fixture_command(args, self.source, Path("/p/test.c"), Path("/p/test.o"))
        self.assertFalse(any(x.startswith("public") or x == "-MFpublic.d" for x in result))
        self.assertEqual(result.count("-o"), 1)

    def test_opaque_flags_and_incomplete_output_are_rejected(self):
        args = build.compile_command(self.command, self.source)
        for suffix in (["-MF"], ["@response"], ["-Wp,-MD,public.d"]):
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                build.fixture_command(args + suffix, self.source, Path("/p/c"), Path("/p/o"))

    def test_symbol_parsers_accept_darwin_and_llvm(self):
        self.assertEqual(build.defined_symbols("fffffe0007000 S _foo\nfffffe0007010 s _local\n U _external\n"),
                         {"_foo", "_local"})
        self.assertEqual(build.undefined_symbols("_foo\n_bar\n"), {"_foo", "_bar"})
        self.assertEqual(build.undefined_symbols(" U _foo\n U _bar\n"), {"_foo", "_bar"})
        with self.assertRaises(ValueError):
            build.undefined_symbols("something unexpected here")

    def test_missing_runtime_and_unresolved_imports_fail(self):
        imports = build.REQUIRED_IMPORTS.copy()
        build.check_imports(imports, imports | {"_extra"})
        with self.assertRaises(ValueError):
            build.check_imports(imports - {"_sock_receive"}, imports)
        with self.assertRaises(ValueError):
            build.check_imports(imports | {"_unknown"}, imports)


if __name__ == "__main__":
    unittest.main()
