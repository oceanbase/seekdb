#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Run with python [-X utf8=0] -m unittest discover -s unittest/package."""

import gzip
import hashlib
from pathlib import Path
import runpy
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RELEASE = runpy.run_path(str(ROOT / "package/wasm/release-shell.py"))


class WasmReleaseTest(unittest.TestCase):
    def test_utf8_entrypoints_and_compressed_representations(self):
        # Packaging only needs files; no engine execution is needed for this test.
        html = (ROOT / "src/wasm/shell.html").read_text(encoding="utf-8")
        self.assertIn("\u2026", html)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            for name in RELEASE["ASSETS"]:
                (build / name).write_bytes(b"test asset\n")
            (build / "shell.html").write_bytes(html.encode("utf-8"))
            output = root / "release"
            manifest = RELEASE["package"](build, output)
            expected = html.replace(
                "<head>", '<head>\n  <base href="./releases/%s/">' % manifest["build_id"], 1
            )
            for name in ("index.html", "shell.html"):
                with self.subTest(entrypoint=name):
                    path = output / name
                    self.assertEqual(path.read_text(encoding="utf-8"), expected)
                    content = path.read_bytes()
                    self.assertEqual(gzip.decompress(path.with_name(name + ".gz").read_bytes()), content)
                    record = manifest["assets"]["/" + name]["representations"]["identity"]
                    self.assertEqual(record["bytes"], len(content))
                    self.assertEqual(record["sha256"], hashlib.sha256(content).hexdigest())
            self.assertEqual(RELEASE["load_assets"](output), manifest["assets"])


if __name__ == "__main__":
    unittest.main()
