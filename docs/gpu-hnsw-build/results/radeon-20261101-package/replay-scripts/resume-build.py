"""Continue the same build with four C++ jobs and one job per Cargo invocation."""
from datetime import datetime, timezone
from pathlib import Path
import json
import os
import subprocess
import traceback

ROOT = Path('/workspace/amd-acceptance-20261101-6dc14ba3')
SRC = ROOT / 'src'
EVIDENCE = ROOT / 'evidence'
SHA = 'cfae4ba92b14d3010f7c7aa6a87d064dfd7c0075'
env = os.environ.copy()
prefix = ROOT / 'tooling/rust-1.98.1'
env.update(PATH=str(prefix / 'bin') + ':/opt/venv/bin:/opt/rocm/bin:' + env.get('PATH', ''),
           RUSTC=str(prefix / 'bin/rustc'), CARGO_HOME=str(ROOT / 'tooling/cargo'),
           CARGO_NET_OFFLINE='true', CARGO_BUILD_JOBS='1', MAKEFLAGS='-j4',
           OMP_NUM_THREADS='8', OPENBLAS_NUM_THREADS='8')


def status(stage, state, **extra):
    report = dict(stage=stage, state=state, source_commit=SHA, cpp_jobs=4,
                  external_cargo_jobs=1, updated_utc=datetime.now(timezone.utc).isoformat(), **extra)
    (EVIDENCE / 'current-status.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report), flush=True)


try:
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SRC, text=True).strip() == SHA
    status('compile-gpu', 'running')
    with (EVIDENCE / 'compile-gpu.log').open('a') as log:
        log.write('\nContinuing existing objects with make -j4; source unchanged.\n')
        log.flush()
        subprocess.run(['make', '-C', SRC / 'build_tgz', '-j4', 'seekdb'], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['git', 'diff', '--exit-code'], cwd=SRC, check=True)
    status('build-passed', 'passed')
except Exception as error:
    status('compile-gpu', 'failed', error=repr(error))
    traceback.print_exc()
    raise
