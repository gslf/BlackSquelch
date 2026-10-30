# Releases

The [Release workflow](../.github/workflows/release.yml) publishes packages when a
version tag is pushed. All packages must build, pass tests and install successfully.

1. Set the version in `CMakeLists.txt` and update the documentation.
2. Commit and push the changes to `main`.
3. Push a matching tag:

```sh
git tag -a v1.0.0 -m "BlackSquelch 1.0.0"
git push origin v1.0.0
```

Replace `1.0.0` with the release version. Tags must be `vMAJOR.MINOR.PATCH` and
match CMake exactly. The executable reports that version with `--version`.

The workflow builds these archives:

| System | Processor | Archive |
| --- | --- | --- |
| Linux | x86_64 / ARM64 | `blacksquelch-linux-<arch>.tar.gz` |
| macOS | x86_64 / ARM64 | `blacksquelch-macos-<arch>.tar.gz` |
| Windows | x86_64 | `blacksquelch-windows-x86_64.zip` |

Linux builds on Ubuntu 22.04. macOS targets 13+ and uses an ad-hoc signature;
Developer ID signing and notarization are not configured. Windows bundles its MinGW
runtime DLLs. Every archive includes offline install and uninstall scripts.

The release also includes download installers, `checksums.sha256` and generated
release notes. Publication uses GitHub's built-in token; no extra secret is needed.
Only the publish job can write releases. No release is published if any target fails.

To retry, rerun the failed workflow, or open Actions → Release → Run workflow and
enter the existing tag. This builds the tagged commit. Existing releases are never
overwritten: use a new version for changes after publication.

For a local package, build first, then run:

```sh
python3 scripts/package-release.py --build-dir build --platform linux --arch x86_64
```

Choose `linux`, `macos` or `windows`, and `x86_64` or `arm64` as appropriate.
Windows requires `--runtime-dir C:/msys64/ucrt64/bin`. Archives are written to `build/dist/`.
