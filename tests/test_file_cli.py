"""File formats and failure behavior through the installed command interface."""
import base64
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

executable = str(Path(sys.argv[1]).resolve())


def run(*arguments, code=0):
    result = subprocess.run(
        [executable, *map(str, arguments)], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=60,
    )
    assert result.returncode == code, (arguments, result.returncode, result.stderr, result.stdout[-2000:])
    return result


with tempfile.TemporaryDirectory(prefix="blacksquelch-files-") as temporary:
    root = Path(temporary)
    fixtures = {
        "Mixed Case.txt": "caffè, minuscole e MAIUSCOLE\r\n日本語\n".encode("utf-8"),
        # Complete 1x1 PNG, not a text representation of an image.
        "Small Image.png": base64.b64decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jF9sAAAAASUVORK5CYII="
        ),
        "All Bytes.bin": bytes(range(256)) + b"\x00",
    }
    for tones in (8, 16):
        for name, content in fixtures.items():
            source = root / name
            source.write_bytes(content)
            audio = root / "transfer.wav"
            output = root / (source.suffix[1:] + f"-{tones}-received")
            run("--tones", str(tones), "--encode-file", "IU2ABC", source, audio)
            run("--decode", audio, "--output-dir", output)
            received = list(output.iterdir())
            assert len(received) == 1 and received[0].read_bytes() == content
            assert received[0].name.endswith(name.replace(" ", "_"))
            # Running again must create a distinct file and preserve the first one.
            run("--decode", audio, "--output-dir", output)
            assert len(list(output.iterdir())) == 2
            assert all(path.read_bytes() == content for path in output.iterdir())

    # The final fixture spans multiple 64-byte blocks. Cut off part of its last frame while
    # keeping a valid WAV container: verified early blocks must not become a file.
    with wave.open(str(audio), "rb") as original:
        parameters = original.getparams()
        samples = original.readframes(original.getnframes() - 48000)
    truncated = root / "incomplete.wav"
    with wave.open(str(truncated), "wb") as partial:
        partial.setparams(parameters)
        partial.writeframes(samples)
    output = root / "incomplete-received"
    result = run("--decode", truncated, "--output-dir", output, code=3)
    assert b"INCOMPLETE FILE" in result.stdout
    assert not output.exists()

    blocked = root / "not-a-directory"
    blocked.write_bytes(b"preserve this")
    run("--decode", audio, "--output-dir", blocked, code=1)
    assert blocked.read_bytes() == b"preserve this"
    oversized = root / "oversized.bin"
    oversized.write_bytes(bytes(1048577))
    run("--encode-file", "IU2ABC", oversized, root / "too-big.wav", code=1)
    assert not (root / "too-big.wav").exists()
    run("--tones", "2", "--encode-file", "IU2ABC", source, root / "removed.wav", code=2)
    assert not (root / "removed.wav").exists()
    run("--rate", "50", "--frame", "IU2ABC", "REMOVED", code=2)
    run("--encode-file", "BAD", source, root / "invalid.wav", code=2)
    assert not (root / "invalid.wav").exists()

print("PASS file CLI: real UTF-8 text/PNG/binary roundtrips, no overwrite, incomplete/error exit codes")
