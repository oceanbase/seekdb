#!/usr/bin/env python3
"""Compile and run the local COW batch regression against production tree code."""
from pathlib import Path
import shlex
import subprocess
import tempfile

local = Path(__file__).resolve().parent
root = local.parent.parent
with tempfile.TemporaryDirectory(prefix='seekdb-catalog-batch-') as tmp:
    binary = Path(tmp) / 'catalog_batch_probe'
    command = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-I' + str(root / 'src'),
               str(local / 'catalog_batch_probe.cpp'), str(root / 'src/namespace/catalog.cpp'),
               '-o', str(binary), '-pthread']
    subprocess.run(['bash', '-c', 'source ~/.bashrc && ' + shlex.join(command)], check=True)
    subprocess.run([str(binary)], check=True)
