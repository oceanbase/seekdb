"""Test native toolchain selection and bootstrap failure propagation without network."""
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class RustSetupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="seekdb-rust-setup-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source with spaces"
        (self.source / "rust").mkdir(parents=True)
        (self.source / "rust/rust-toolchain.toml").write_text('[toolchain]\nchannel = "1.98.1"\n')
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.install = self.root / "private rust"
        self.record = self.root / "record"
        self.isolate_path = False

    def tool(self, name, body):
        p = self.bin / name
        p.write_text("#!/bin/sh\n" + body)
        p.chmod(0o755)

    def versions(self, cargo="1.98.1", rustc="1.98.1"):
        self.tool("cargo", f'echo "cargo {cargo} (fixture)"\n')
        self.tool("rustc", f'echo "rustc {rustc} (fixture)"\n')

    def run_setup(self):
        path = str(self.bin) if self.isolate_path else str(self.bin) + os.pathsep + os.environ["PATH"]
        env = dict(os.environ, PATH=path,
                   CARGO_HOME=str(self.install / "cargo"),
                   RUSTUP_HOME=str(self.install / "rustup"), RECORD=str(self.record))
        return subprocess.run(["bash", "-c",
                               'source "$1"; seekdb_prepare_rust "$2" "$3"; '
                               'result=$?; echo "override=$RUSTUP_TOOLCHAIN"; exit "$result"',
                               "setup-test", str(ROOT / "deps/init/setup_rust.sh"),
                               str(self.source), str(self.install)],
                              env=env, text=True, capture_output=True, timeout=15)

    def test_exact_standalone_pair_needs_no_rustup_or_download(self):
        self.versions()
        self.tool("curl", 'echo unexpected-download >> "$RECORD"; exit 99\n')
        result = self.run_setup()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Reusing Rust/Cargo 1.98.1", result.stdout)
        self.assertIn("override=1.98.1", result.stdout)
        self.assertFalse(self.record.exists())

    def bootstrap_tools(self):
        self.isolate_path = True
        for name in ("bash", "sed", "dirname", "mkdir", "cat", "chmod", "sh", "uname"):
            (self.bin / name).symlink_to(shutil.which(name))
        self.versions(cargo="1.97.1", rustc="1.97.1")

    def test_download_failure_is_preserved_without_using_old_compiler(self):
        self.bootstrap_tools()
        self.tool("curl", "exit 19\n")
        result = self.run_setup()
        self.assertEqual(result.returncode, 19, result.stderr)

    def test_bootstrap_exports_private_toolchain_and_keeps_system_pair(self):
        self.bootstrap_tools()
        self.tool("curl", '''for argument; do output=$argument; done
cat > "$output" <<'INSTALLER'
echo "installer:$*" > "$RECORD"
mkdir -p "$CARGO_HOME/bin"
for name in cargo rustc; do
  printf '#!/bin/sh\necho "%s 1.98.1 (private fixture)"\n' "$name" > "$CARGO_HOME/bin/$name"
  chmod +x "$CARGO_HOME/bin/$name"
done
cat > "$CARGO_HOME/bin/rustup" <<'RUSTUP'
#!/bin/sh
echo "rustup:$*" >> "$RECORD"
RUSTUP
chmod +x "$CARGO_HOME/bin/rustup"
INSTALLER
''')
        result = self.run_setup()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Selected Rust/Cargo 1.98.1", result.stdout)
        self.assertIn("--default-toolchain none --no-modify-path", self.record.read_text())
        self.assertIn("toolchain install 1.98.1 --profile minimal --no-self-update", self.record.read_text())
        self.assertIn("1.97.1", (self.bin / "rustc").read_text())

    def test_existing_rustup_install_failure_is_preserved(self):
        self.versions(cargo="1.97.1", rustc="1.97.1")
        self.tool("rustup", 'echo "$*" > "$RECORD"; exit 23\n')
        result = self.run_setup()
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(self.record.read_text().strip(),
                         "toolchain install 1.98.1 --profile minimal --no-self-update")

    def test_mismatched_compiler_cannot_pass_after_install(self):
        self.versions(rustc="1.97.1")
        self.tool("rustup", "exit 0\n")
        result = self.run_setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must both match pinned version 1.98.1", result.stderr)

    def test_malformed_pin_stops_before_installer(self):
        (self.source / "rust/rust-toolchain.toml").write_text('[toolchain]\nchannel = "stable"\n')
        self.tool("curl", 'echo unexpected-download >> "$RECORD"; exit 99\n')
        result = self.run_setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot read exact Rust version", result.stderr)
        self.assertFalse(self.record.exists())


if __name__ == "__main__":
    unittest.main()
