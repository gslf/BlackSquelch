#!/usr/bin/env python3
"""Exercise a real Linux/macOS release archive without touching the user's files."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
archive = Path(sys.argv[1]).resolve()


def run(*args, success=True, env=None):
    result = subprocess.run(args, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (result.returncode == 0) != success:
        raise AssertionError(f"Unexpected exit {result.returncode}: {args}\n{result.stdout}")
    return result.stdout


with tempfile.TemporaryDirectory(prefix="blacksquelch-test-") as directory:
    root = Path(directory)
    with tarfile.open(archive) as package:
        package.extractall(root, filter="data")
    package = root / "blacksquelch"
    version = dict(line.split("=", 1) for line in (package / "BUILD_INFO").read_text().splitlines())["version"]
    expected = f"BlackSquelch {version}"
    test_home = root / "home"
    test_home.mkdir()
    env = dict(os.environ, HOME=str(test_home))
    config = test_home / ".config/blacksquelch/macros.txt"
    config.parent.mkdir(parents=True)
    config.write_text("keep my settings")
    prefix = root / "custom path with spaces"
    run("sh", str(package / "install.sh"), "--prefix", str(prefix), env=env)
    executable = prefix / "bin/blacksquelch"
    assert run(str(executable), "--version").strip() == expected
    run(str(executable), "--help")
    unrelated = prefix / "bin/another-program"
    unrelated.write_text("keep")
    run("sh", str(package / "install.sh"), "--prefix", str(prefix), env=env)
    assert unrelated.read_text() == "keep"
    assert run(str(executable), "--version").strip() == expected
    # Reject the wrong architecture before changing an existing installation.
    info = package / "BUILD_INFO"
    original = info.read_text()
    info.write_text(original.replace("arch=", "arch=wrong-"))
    run("sh", str(package / "install.sh"), "--prefix", str(prefix), success=False, env=env)
    assert run(str(executable), "--version").strip() == expected
    info.write_text(original)
    run("sh", str(prefix / "share/blacksquelch/uninstall.sh"), env=env)
    assert not executable.exists()
    assert unrelated.read_text() == "keep"
    assert config.read_text() == "keep my settings"
    # Default location is inside the isolated HOME.
    run("sh", str(package / "install.sh"), env=env)
    assert (test_home / ".local/bin/blacksquelch").exists()
    run("sh", str(test_home / ".local/share/blacksquelch/uninstall.sh"), env=env)
    run("sh", str(package / "install.sh"), "--prefix", "/", success=False, env=env)

    # Simulate GitHub downloads; exercise selection, checksums and rejection without a network.
    downloads, fake_bin = root / "downloads", root / "fake-bin"
    downloads.mkdir()
    fake_bin.mkdir()
    shutil.copy2(archive, downloads / archive.name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (downloads / "checksums.sha256").write_text(f"{digest}  {archive.name}\n")
    fake_curl = fake_bin / "curl"
    fake_curl.write_text('''#!/usr/bin/env python3
import os, pathlib, shutil, sys
args = sys.argv[1:]
if '--write-out' in args:
    print('https://github.com/gslf/BlackSquelch/releases/tag/v' + os.environ['TEST_VERSION'], end='')
else:
    url = next(arg for arg in args if arg.startswith('https://'))
    output = args[args.index('--output') + 1]
    assert '/releases/download/v' + os.environ['TEST_VERSION'] + '/' in url
    shutil.copyfile(pathlib.Path(os.environ['TEST_DOWNLOADS']) / url.rsplit('/', 1)[1], output)
''')
    fake_curl.chmod(0o755)
    env.update(PATH=str(fake_bin) + os.pathsep + os.environ["PATH"],
               TEST_VERSION=version, TEST_DOWNLOADS=str(downloads))
    bootstrap_prefix = root / "download install"
    bootstrap = ROOT / "scripts/install-release.sh"
    for options in ([], ["--version", "v" + version]):
        run("sh", str(bootstrap), *options, "--prefix", str(bootstrap_prefix), env=env)
        assert run(str(bootstrap_prefix / "bin/blacksquelch"), "--version").strip() == expected
        run("sh", str(bootstrap_prefix / "share/blacksquelch/uninstall.sh"), env=env)
    with (downloads / archive.name).open("ab") as file:
        file.write(b"tampered")
    result = run("sh", str(bootstrap), "--prefix", str(bootstrap_prefix), success=False, env=env)
    assert "Checksum mismatch" in result
    assert not (bootstrap_prefix / "bin/blacksquelch").exists()
    assert config.read_text() == "keep my settings"
print("Install, update, uninstall, platform selection and checksum rejection passed.")
