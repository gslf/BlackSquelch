"""Optional POSIX smoke test using only Python's standard library and a PTY."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time

master, slave = pty.openpty()
original = termios.tcgetattr(slave)
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
process = None
try:
    with tempfile.TemporaryDirectory(prefix="blacksquelch-config-") as config:
        environment = os.environ.copy()
        environment["XDG_CONFIG_HOME"] = config
        environment["NO_COLOR"] = "1"
        # Linux-specific config isolation; macOS uses Application Support instead.
        if os.uname().sysname != "Linux":
            raise SystemExit("This first-run smoke test is Linux-specific.")
        process = subprocess.Popen(
            [sys.argv[1] if len(sys.argv) > 1 else "./build/blacksquelch"],
            stdin=slave, stdout=slave, stderr=slave,
            env=environment,
        )
        output = bytearray()

        def read_for(seconds):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                if select.select([master], [], [], 0.02)[0]:
                    output.extend(os.read(master, 65536))

        for _ in range(40):
            read_for(0.1)
            if b"First run" in output:
                break
        assert b"First run" in output
        assert b"\\--- BlackSquelch" in output and b":#/ GSLF" in output
        assert output.count(b"C-x") == 1
        assert b"Manual PTT" not in output and b"\x1b[2K" not in output
        os.write(master, b"iu2abc")
        read_for(0.2)
        assert b"IU2ABC" in output
        assert b"Exactly 6 ASCII" in output
        assert b"RX AUTO 8/16" in output
        os.write(master, b"\t\t\t\t\x1b[A")
        read_for(0.2)
        assert b"TX tones      16 tones" in output
        os.write(master, b"\x1b[A")
        read_for(0.2)
        assert b"TX tones      8 tones" in output
        os.write(master, b"\x1b[B")
        read_for(0.2)
        assert b"TX tones      16 tones" in output
        output.clear()
        os.write(master, b"\x18")
        read_for(0.15)
        assert b" MENU " in output
        assert b"Refresh audio devices" in output and b"Quit" in output
        output.clear()
        os.write(master, b"\x07")
        read_for(0.15)
        assert b" MENU " not in output
        output.clear()
        os.write(master, b"\x1b")
        read_for(0.2)
        assert b"Listening for incoming messages." in output
        output.clear()
        read_for(0.2)
        assert not output, "An unchanged screen must not be erased or redrawn"
        os.write(master, b"hello")
        read_for(0.15)
        assert b"HELLO_" in output
        assert b"Listening for incoming messages." not in output
        assert b"C-x" not in output and b"\x1b[2K" not in output
        # Assign the current composition, save it, then invoke the actual prefix shortcut.
        output.clear()
        os.write(master, b"\x18w")
        read_for(0.15)
        assert b"EDIT MESSAGE" in output and b"MACROS" in output
        os.write(master, b"\x13")
        read_for(0.15)
        assert b"Macros saved" in output
        macro_file = os.path.join(config, "blacksquelch", "macros.txt")
        with open(macro_file, encoding="ascii") as saved:
            assert "macro1=HELLO" in saved.read()
        os.write(master, b"\x18u")
        read_for(0.15)
        output.clear()
        os.write(master, b"\x181")
        read_for(0.15)
        assert b"HELLO_" in output and b"Macro 1 inserted" in output
        output.clear()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 8, 40, 0, 0))
        read_for(0.15)
        assert b"BlackSquelch" in output and output.count(b"C-x") == 1
        os.write(master, b"\x18q")
        read_for(0.15)
        assert process.wait(timeout=2) == 0
        assert termios.tcgetattr(slave) == original
        assert b"\x1b[?1049l" in output
        assert b"ERROR: AddressSanitizer" not in output
        # A fresh process must load the saved shortcuts even before audio setup.
        output.clear()
        process = subprocess.Popen(
            [sys.argv[1] if len(sys.argv) > 1 else "./build/blacksquelch"],
            stdin=slave, stdout=slave, stderr=slave, env=environment,
        )
        for _ in range(40):
            read_for(0.1)
            if b"First run" in output:
                break
        assert b"First run" in output
        os.write(master, b"\x1b")
        read_for(0.15)
        output.clear()
        os.write(master, b"\x181")
        read_for(0.15)
        assert b"HELLO_" in output and b"Macro 1 inserted" in output
        os.write(master, b"\x18q")
        read_for(0.15)
        assert process.wait(timeout=2) == 0 and termios.tcgetattr(slave) == original
        print("PASS terminal: settings, menus, incremental redraw, macro editing/shortcut/persistence, resize and restoration")
finally:
    if process and process.poll() is None:
        process.kill()
        process.wait()
    os.close(master)
    os.close(slave)
