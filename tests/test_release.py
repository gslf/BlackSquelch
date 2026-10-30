#!/usr/bin/env python3
"""Release validation, architecture checks and dependency closure (no network)."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release", ROOT / "scripts/package-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def test_offline_cache_is_stable_and_tracks_worker_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            worker = root / 'sw.js'
            worker.write_text("const VERSION = '__BUILD__';\n")
            (root / 'app.js').write_text('application v1')
            def finalize():
                subprocess.run([sys.executable, str(ROOT / 'scripts/finalize-web.py'), str(root)],
                               check=True, stdout=subprocess.DEVNULL)
                return worker.read_text()
            first = finalize()
            self.assertNotIn('__BUILD__', first)
            self.assertEqual(finalize(), first)
            worker.write_text(first + '// New caching behavior\n')
            self.assertNotEqual(finalize().splitlines()[0], first.splitlines()[0])

    def test_tags(self):
        version = release.project_version()
        self.assertEqual(release.validate_tag("v" + version), version)
        for tag in (version, "v999.0.0", "v1.0.0-rc1", "../main", "v1.0.0\n"):
            with self.assertRaises(ValueError):
                release.validate_tag(tag)

    def test_executable_architecture(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "binary"
            for machine, arch in ((62, "x86_64"), (183, "arm64")):
                data = bytearray(64)
                data[:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<H", data, 18, machine)
                binary.write_bytes(data)
                self.assertEqual(release.binary_target(binary), ("linux", arch))
            for machine, arch in ((0x01000007, "x86_64"), (0x0100000C, "arm64")):
                binary.write_bytes(b"\xcf\xfa\xed\xfe" + struct.pack("<I", machine))
                self.assertEqual(release.binary_target(binary), ("macos", arch))
            data = bytearray(128)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 60, 80)
            data[80:84] = b"PE\x00\x00"
            struct.pack_into("<H", data, 84, 0x8664)
            binary.write_bytes(data)
            self.assertEqual(release.binary_target(binary), ("windows", "x86_64"))
            binary.write_bytes(b"not an executable")
            with self.assertRaises(ValueError):
                release.binary_target(binary)

    def test_complete_release_required(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with self.assertRaisesRegex(ValueError, "Missing release target"):
                release.prepare_assets(output)
            for name in [release.asset_name(*target) for target in release.TARGETS] + ['blacksquelch-web.zip']:
                (output / name).write_bytes(b"test archive")
                (output / (name + ".sha256")).write_text(f"{release.sha256(output / name)}  {name}\n")
            release.prepare_assets(output)
            lines = (output / "checksums.sha256").read_text().splitlines()
            self.assertEqual(len(lines), 8)
            for line in lines:
                digest, name = line.split("  ")
                self.assertEqual(digest, release.sha256(output / name))
            (output / release.asset_name(*release.TARGETS[-1])).write_bytes(b"corrupted")
            with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
                release.prepare_assets(output)

    def test_recursive_windows_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime, output = root / "runtime", root / "bin"
            runtime.mkdir()
            output.mkdir()
            (runtime / "objdump.exe").touch()
            for name in ("libgcc.dll", "libwinpthread.dll"):
                (runtime / name).write_bytes(b"dll")
            executable = output / "blacksquelch.exe"
            executable.touch()
            dependencies = {
                "blacksquelch.exe": "DLL Name: libgcc.dll\nDLL Name: api-ms-win-core-test.dll",
                "libgcc.dll": "DLL Name: libwinpthread.dll",
                "libwinpthread.dll": "DLL Name: libgcc.dll",
            }
            with patch.object(release.subprocess, "check_output", side_effect=lambda cmd, **kw: dependencies[Path(cmd[-1]).name]):
                copied = release.windows_dependencies(executable, runtime)
                self.assertEqual({p.name for p in copied}, {"libgcc.dll", "libwinpthread.dll"})
            with patch.object(release.subprocess, "check_output", return_value="DLL Name: missing.dll"), patch.dict(os.environ, {"SystemRoot": str(root)}):
                with self.assertRaisesRegex(ValueError, "Missing Windows dependency"):
                    release.windows_dependencies(executable, runtime)


if __name__ == "__main__":
    unittest.main()
