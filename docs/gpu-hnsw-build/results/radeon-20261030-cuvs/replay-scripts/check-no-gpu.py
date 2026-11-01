import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess

run=Path('/workspace/hipvs-competition-20261030')
e=run/'evidence'
reply=e/'worker-no-gpu-reply.bin'
worker=run/'artifacts/bin/seekdb_hipvs_hnsw_worker'
command=['bash','-c',f'exec 3<{shlex.quote(str(e/"profile-50000x128-input.f32"))}; exec 4>{shlex.quote(str(reply))}; exec {shlex.quote(str(worker))} 50000 128 32 200']
env=dict(os.environ,HIP_VISIBLE_DEVICES='-1')
p=subprocess.run(command,env=env,capture_output=True,text=True,timeout=45)
class Stats(ctypes.Structure):
    _fields_=[('cagra_ms',ctypes.c_double),('hierarchy_ms',ctypes.c_double),('serialize_ms',ctypes.c_double)]
class Reply(ctypes.Structure):
    _fields_=[('status',ctypes.c_int),('output_size',ctypes.c_size_t),('stats',Stats),('error',ctypes.c_char*2048)]
header=Reply.from_buffer_copy(reply.read_bytes())
report={'command':command,'HIP_VISIBLE_DEVICES':'-1','returncode':p.returncode,
        'status':header.status,'output_size':header.output_size,
        'error':header.error.decode(errors='replace'),'stdout':p.stdout,'stderr':p.stderr}
report['passed']=p.returncode!=0 and header.status!=0 and header.output_size==0 and 'cagra_gpu_build' in report['error']
(e/'worker-no-gpu.json').write_text(json.dumps(report,indent=2))
assert report['passed'],report
print(json.dumps({'no_gpu_worker_failure_check':True}),flush=True)
