#!/usr/bin/env python3
"""Build portable release archives. Uses Python's standard library and CMake."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TARGETS = (("linux", "x86_64"), ("linux", "arm64"), ("macos", "x86_64"),
           ("macos", "arm64"), ("windows", "x86_64"))


def project_version():
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(BlackSquelch VERSION (\d+\.\d+\.\d+)\b", source)
    if not match:
        raise ValueError("CMake project version is missing")
    return match[1]


def validate_tag(tag):
    if not re.fullmatch(r"v\d+\.\d+\.\d+", tag) or tag != "v" + project_version():
        raise ValueError("Use vMAJOR.MINOR.PATCH matching CMakeLists.txt")
    return tag[1:]


def asset_name(platform, arch):
    if (platform, arch) not in TARGETS:
        raise ValueError("Unsupported release target")
    extension = "zip" if platform == "windows" else "tar.gz"
    return f"blacksquelch-{platform}-{arch}.{extension}"


def binary_target(path):
    data = path.read_bytes()
    if data[:4] == b"\x7fELF":
        machine = struct.unpack_from("<H" if data[5] == 1 else ">H", data, 18)[0]
        return "linux", {62: "x86_64", 183: "arm64"}.get(machine)
    if data[:4] == b"\xcf\xfa\xed\xfe":
        machine = struct.unpack_from("<I", data, 4)[0]
        return "macos", {0x01000007: "x86_64", 0x0100000C: "arm64"}.get(machine)
    if data[:2] == b"MZ":
        offset = struct.unpack_from("<I", data, 60)[0]
        if data[offset:offset + 4] == b"PE\x00\x00":
            machine = struct.unpack_from("<H", data, offset + 4)[0]
            return "windows", {0x8664: "x86_64", 0xAA64: "arm64"}.get(machine)
    raise ValueError(f"Unsupported executable format: {path}")


def windows_dependencies(executable, runtime_dir):
    """Copy MinGW DLLs recursively; system DLLs stay supplied by Windows."""
    runtime_dir = Path(runtime_dir)
    objdump = runtime_dir / "objdump.exe"
    if not objdump.is_file():
        raise ValueError("--runtime-dir must point to the MinGW bin directory")
    available = {p.name.lower(): p for p in runtime_dir.glob("*.dll")}
    system = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
    pending, seen, copied = [executable], set(), []
    while pending:
        binary = pending.pop()
        output = subprocess.check_output([str(objdump), "-p", str(binary)], text=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", output):
            key = name.lower()
            if key in seen:
                continue
            seen.add(key)
            if key in available:
                if not re.fullmatch(r"[A-Za-z0-9_.+-]+\.dll", name, re.IGNORECASE):
                    raise ValueError("Unsafe DLL name")
                destination = executable.parent / available[key].name
                shutil.copy2(available[key], destination)
                pending.append(destination)
                copied.append(destination)
            elif key.startswith(("api-ms-", "ext-ms-")) or (system / name).is_file():
                continue
            else:
                raise ValueError(f"Missing Windows dependency: {name}")
    return copied


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def package(build_dir, output_dir, platform, arch, runtime_dir=None):
    name = asset_name(platform, arch)
    version = project_version()
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="blacksquelch-package-") as temporary:
        stage = Path(temporary) / "blacksquelch"
        subprocess.run(["cmake", "--install", str(build_dir), "--prefix", str(stage),
                        "--component", "Runtime"], check=True)
        executable = stage / "bin" / ("blacksquelch.exe" if platform == "windows" else "blacksquelch")
        if binary_target(executable) != (platform, arch):
            raise ValueError("Executable architecture does not match the package target")
        if platform == "windows":
            if not runtime_dir:
                raise ValueError("Windows packaging requires --runtime-dir")
            windows_dependencies(executable, runtime_dir)
        elif platform == "macos":
            subprocess.run(["codesign", "--force", "--sign", "-", str(executable)], check=True)
            subprocess.run(["codesign", "--verify", "--strict", str(executable)], check=True)
        actual = subprocess.check_output([str(executable), "--version"], text=True).strip()
        if actual != "BlackSquelch " + version:
            raise ValueError(f"Stale build: expected {version}, got {actual}")
        subprocess.run([str(executable), "--help"], check=True, stdout=subprocess.DEVNULL)
        shutil.copy2(ROOT / "README.md", stage / "README.md")
        shutil.copytree(ROOT / "docs", stage / "docs")
        shutil.copy2(ROOT / "third_party/miniaudio/LICENSE", stage / "LICENSE-miniaudio.txt")
        suffix = "ps1" if platform == "windows" else "sh"
        for script in (f"install.{suffix}", f"uninstall.{suffix}"):
            shutil.copy2(ROOT / "scripts" / script, stage / script)
        (stage / "BUILD_INFO").write_text(
            f"version={version}\nplatform={platform}\narch={arch}\n", encoding="ascii")
        archive = output_dir / name
        if platform == "windows":
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
                for path in sorted(stage.rglob("*")):
                    if path.is_file():
                        output.write(path, path.relative_to(stage.parent).as_posix())
        else:
            for path in stage.rglob("*"):
                path.chmod(0o755 if path.is_dir() or path.suffix == ".sh" or path == executable else 0o644)
            with tarfile.open(archive, "w:gz") as output:
                output.add(stage, arcname="blacksquelch")
        (output_dir / (name + ".sha256")).write_text(f"{sha256(archive)}  {name}\n", encoding="ascii")
        print(archive)
        return archive


def prepare_assets(directory):
    names = [asset_name(*target) for target in TARGETS]
    for name in names:
        archive = directory / name
        checksum = directory / (name + ".sha256")
        if not archive.is_file() or not checksum.is_file():
            raise ValueError(f"Missing release target: {name}")
        if checksum.read_text(encoding="ascii").strip() != f"{sha256(archive)}  {name}":
            raise ValueError(f"Checksum mismatch: {name}")
    for name in ("install-release.sh", "install-release.ps1"):
        shutil.copy2(ROOT / "scripts" / name, directory / name)
        names.append(name)
    (directory / "checksums.sha256").write_text(
        "".join(f"{sha256(directory / name)}  {name}\n" for name in sorted(names)), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-tag")
    parser.add_argument("--prepare-assets", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/dist")
    parser.add_argument("--platform", choices=("linux", "macos", "windows"))
    parser.add_argument("--arch", choices=("x86_64", "arm64"))
    parser.add_argument("--runtime-dir", type=Path)
    args = parser.parse_args()
    try:
        if args.check_tag:
            print(validate_tag(args.check_tag))
        elif args.prepare_assets:
            prepare_assets(args.prepare_assets)
        else:
            if not args.build_dir or not args.platform or not args.arch:
                parser.error("packaging needs --build-dir, --platform and --arch")
            package(args.build_dir.resolve(), args.output_dir.resolve(), args.platform, args.arch,
                    args.runtime_dir)
    except (ValueError, OSError, subprocess.CalledProcessError, struct.error) as error:
        parser.exit(1, f"Release failed: {error}\n")


if __name__ == "__main__":
    main()
