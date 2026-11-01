import hashlib
import json
from pathlib import Path
import subprocess
import time

run=Path('/workspace/hipvs-competition-20261030')
src=run/'src'
evidence=run/'evidence'
scratch=Path('/root/hipvs-competition-build-20261030')
def command(args):
    p=subprocess.run(args,capture_output=True,text=True,timeout=90)
    return (p.stdout+p.stderr).strip()
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(8*1024*1024),b''): h.update(chunk)
    return h.hexdigest()
files=[run/'artifacts/bin/seekdb',run/'artifacts/bin/seekdb_hipvs_hnsw_worker',
       run/'artifacts/lib/libseekdb_hipvs_hnsw.so',Path('/opt/hipvs/lib/libcuvs_c.so'),Path('/opt/hipvs/lib/libcuvs.so')]
env={'recorded_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
     'base_revision':'5bce9e06d1281347ad61344e4b01fab9158c58a2',
     'source_snapshot':json.loads((run/'incoming/source-snapshot.json').read_text()),
     'built_revision':command([str(files[0]),'--version']),
     'compiler':command([str(src/'deps/3rd/usr/local/oceanbase/devtools/bin/clang++-17'),'--version']),
     'rustc':command([str(scratch/'tooling/rust-1.98.1/bin/rustc'),'--version']),
     'cargo':command([str(scratch/'tooling/rust-1.98.1/bin/cargo'),'--version']),
     'python':command(['/opt/venv/bin/python','--version']),
     'numpy':command(['/opt/venv/bin/python','-c','import numpy; print(numpy.__version__)']),
     'cmake':command([str(src/'deps/3rd/usr/local/oceanbase/devtools/bin/cmake'),'--version']),
     'cpu_max':Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
     'memory_max':Path('/sys/fs/cgroup/memory.max').read_text().strip(),
     'rocm_smi':command(['/opt/rocm/bin/rocm-smi','--showproductname','--showmeminfo','vram']),
     'rocminfo':command(['/opt/rocm/bin/rocminfo']),
     'rocm_version':Path('/opt/rocm/.info/version').read_text().strip(),
     'ldd_seekdb':command(['ldd',str(files[0])]),
     'restart_gpu_visibility':'HIP_VISIBLE_DEVICES=-1',
     'database_base_dir':str(scratch/'db'),
     'database_storage_note':'Persistent /workspace is NFS and rejected fallocate; database runs on local overlay, artifacts and evidence are copied to persistent storage.',
     'cpu_off_full_build':False,'sql_profiled':False,
     'rust_lock_sha256':digest(src/'rust/Cargo.lock'),
     'sha256':{str(path):digest(path) for path in files}}
(evidence/'final-environment.json').write_text(json.dumps(env,indent=2))
original=json.loads((src/'docs/gpu-hnsw-build/results/source-sha256.json').read_text())
original['docs/gpu-hnsw-build/verify_evidence.py']=''
original['unittest/sql/vector_index/check_hipvs_sql.py']=''
manifest={path:hashlib.sha256((src/path).read_bytes().replace(b'\r\n',b'\n')).hexdigest() for path in original}
env['post_archive_updates']={path:value for path,value in manifest.items()
                             if path in env['source_snapshot']['dirty_overlay']
                             and value!=env['source_snapshot']['dirty_overlay'][path]}
env['post_archive_additions']=['unittest/sql/vector_index/check_hipvs_sql.py']
(evidence/'final-environment.json').write_text(json.dumps(env,indent=2))
(evidence/'source-sha256.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps({'source_files':len(manifest),'binaries':env['sha256']}),flush=True)
