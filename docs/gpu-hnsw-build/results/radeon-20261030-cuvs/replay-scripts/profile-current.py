import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import numpy as np

run=Path('/workspace/hipvs-competition-20261030')
evidence=run/'evidence'
worker=run/'artifacts/bin/seekdb_hipvs_hnsw_worker'
data=evidence/'profile-50000x128-input.f32'
reply=evidence/'profile-50000x128-reply.bin'
x=np.random.default_rng(20261030).standard_normal((50000,128),dtype=np.float32)
x.tofile(data)
output=evidence/'kernel-trace-50000x128'
cmd=['rocprofv3','--kernel-trace','--memory-copy-trace','--stats','-f','csv',
     '-d',str(output),'-o','hipvs-build','--','bash','-c',
     f'exec 3<{shlex.quote(str(data))}; exec 4>{shlex.quote(str(reply))}; exec {shlex.quote(str(worker))} 50000 128 32 200']
report={'command':cmd,'rows':50000,'dim':128,'m':32,'ef_construction':200,
        'input_sha256':hashlib.sha256(data.read_bytes()).hexdigest(),
        'worker_sha256':hashlib.sha256(worker.read_bytes()).hexdigest(),
        'outside_sql_timed_runs':True}
result=subprocess.run(cmd,capture_output=True,text=True,timeout=300)
(evidence/'kernel-profile.stdout.log').write_text(result.stdout)
(evidence/'kernel-profile.stderr.log').write_text(result.stderr)
report['returncode']=result.returncode
class Stats(ctypes.Structure):
    _fields_=[('cagra_ms',ctypes.c_double),('hierarchy_ms',ctypes.c_double),('serialize_ms',ctypes.c_double)]
class Reply(ctypes.Structure):
    _fields_=[('status',ctypes.c_int),('output_size',ctypes.c_size_t),('stats',Stats),('error',ctypes.c_char*2048)]
if reply.exists() and reply.stat().st_size>=ctypes.sizeof(Reply):
    header=Reply.from_buffer_copy(reply.read_bytes()[:ctypes.sizeof(Reply)])
    report['worker_reply']={'status':header.status,'output_size':header.output_size,
                           'error':header.error.decode(errors='replace'),
                           'actual_file_size':reply.stat().st_size,
                           'header_bytes':ctypes.sizeof(Reply)}
    assert header.status==0 and reply.stat().st_size==ctypes.sizeof(Reply)+header.output_size,report
traces=list(output.rglob('*kernel_trace.csv'))
assert len(traces)==1,traces
trace=traces[0]
if trace!=output/'hipvs-build_kernel_trace.csv':
    (output/'hipvs-build_kernel_trace.csv').write_bytes(trace.read_bytes())
with trace.open(newline='') as stream: rows=list(csv.DictReader(stream))
report['kernel_dispatches']=len(rows)
report['nn_descent_dispatches']=sum('nn_descent' in row['Kernel_Name'].lower() for row in rows)
(evidence/'kernel-trace-50000x128-command.json').write_text(json.dumps(report,indent=2))
assert result.returncode==0 and report['nn_descent_dispatches']>0,report
print(json.dumps(report),flush=True)
