"""Build a standalone lifetime test with an existing CMake release build.

Usage: python3 tools/tx_ctx_cache_experiment/run_cache_test.py build_release
This checkout has no CMake storage_tests target; reuse the production flags and
link inputs so the test exercises the real ObTxCtx and TxCtxCache implementations.
"""
import pathlib
import shlex
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parents[2]
build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build_release').resolve()
source = pathlib.Path(__file__).with_name('test_cache.cpp')
test_dir = build / 'tx_ctx_cache_experiment'
test_dir.mkdir(exist_ok=True)
obj = test_dir / 'test_cache.o'
binary = test_dir / 'test_cache'
flags = {}
for line in (build / 'src/storage/CMakeFiles/ob_storage.dir/flags.make').read_text().splitlines():
    if line.startswith(('CXX_DEFINES = ', 'CXX_INCLUDES = ', 'CXX_FLAGS = ')):
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
link = shlex.split((build / 'src/observer/CMakeFiles/seekdb.dir/link.txt').read_text())
compiler_index = next(i for i, arg in enumerate(link) if pathlib.Path(arg).name.startswith('clang++'))
compiler = link[compiler_index]
subprocess.run([compiler, *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'],
                *flags['CXX_FLAGS'], '-c', str(source), '-o', str(obj)], check=True, cwd=repo)
link = link[compiler_index:]
link[link.index('CMakeFiles/ob_main.dir/main.cpp.o')] = str(obj)
link[link.index('-o') + 1] = str(binary)
subprocess.run(link, check=True, cwd=build / 'src/observer')
subprocess.run([str(binary)], check=True, cwd=test_dir)
