#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import resource
import sys
import threading
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment

p=argparse.ArgumentParser()
p.add_argument('--binary',required=True)
p.add_argument('--expect-failure',action='store_true')
a=p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
os.environ['SEEKDB_WEAK_SOURCE_GC_PROBE']='1'
exp=BootstrapExperiment(a.binary,'weak_source_gc',prototype=6)
done=threading.Event()
observed=[]
def monitor():
    until=time.monotonic()+90
    while not done.wait(.2) and time.monotonic()<until:
        paths=list((exp.base/'log').glob('seekdb.log*'))+list(exp.base.glob('process.out'))
        for path in paths:
            text=path.read_text(errors='replace')
            lines=[line for line in text.splitlines() if 'INSTANCE_WEAK_SOURCE_GC_' in line or 'WEAK_SOURCE_GC_GAP' in line]
            if lines:
                observed.extend(lines)
                if any('INSTANCE_WEAK_SOURCE_GC_FAIL' in line for line in lines):
                    if exp.proc and exp.proc.poll() is None: exp.proc.terminate()
                if any('INSTANCE_WEAK_SOURCE_GC_' in line for line in lines): return
thread=threading.Thread(target=monitor,daemon=True)
thread.start()
try:
    error=None
    try: exp.start()
    except Exception as exc: error=repr(exc)
    thread.join(timeout=60)
    print('\n'.join(observed),flush=True)
    failed=any('INSTANCE_WEAK_SOURCE_GC_FAIL' in line for line in observed)
    passed=any('INSTANCE_WEAK_SOURCE_GC_PASS' in line for line in observed)
    if a.expect_failure:
        assert failed and any('WEAK_SOURCE_GC_GAP' in line for line in observed), (error,observed)
        exp.record('EXPECTED_FAILURE',case='future_weak_source_gc',evidence=observed)
    else:
        assert passed and not failed and error is None,(error,observed)
        exp.record('PASS',case='future_weak_source_gc')
finally:
    done.set()
    thread.join(timeout=2)
    exp.close()
