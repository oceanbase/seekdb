from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, os, shutil, subprocess, time, traceback
ROOT=Path('/workspace/amd-acceptance-20261101-6dc14ba3/entry-environment-audit')
E=ROOT/'evidence';SRC=ROOT/'source'
report=dict(stage='waiting-for-native-bootstrap',completed=False,
            source_base='bb488f7cfcdf102d8d11c5f44ac7f2fd02d86cec',
            full_cpp_build_repeated=False,make_guard_expected_exit=42)
def save():
 report['updated_utc']=datetime.now(timezone.utc).isoformat()
 (E/'native-entry-status.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report),flush=True)
try:
 save();deadline=time.monotonic()+1800
 while '[setup-rust] Selected Rust/Cargo 1.98.1' not in (E/'actual-rust-bootstrap.log').read_text():
  if time.monotonic()>deadline:raise TimeoutError('native Rust bootstrap did not finish')
  time.sleep(10)
 assert hashlib.sha256((ROOT/'rust/rustup-init').read_bytes()).hexdigest()=='5be191e8c70a5007074254cb74d78dbfdd25cd5a61c27de7b8e5410122ec24f4'
 state=json.loads((E/'exact-dependency-initialization.json').read_text())
 assert state['returncode']==0
 report['stage']='real-compile-entry-through-configure';save()
 for n in ['.github/script/seekdb/compile.sh','deps/init/setup_rust.sh']:
  shutil.copy2(ROOT/'regressions'/n,SRC/n)
 guard=ROOT/'stop-before-cpp-build.sh'
 guard.write_text('#!/bin/sh\n'
                  'case "$PWD:$*" in\n'
                  f'  "{SRC}/build_release:"*seekdb*)\n'
                  '    echo "[native-entry-probe] reached make in $PWD with args: $*"; exit 42 ;;\n'
                  '  *) exec /usr/bin/make "$@" ;;\n'
                  'esac\n')
 guard.chmod(0o755)
 env=os.environ.copy()
 for n in ['CARGO_HOME','RUSTUP_HOME','RUSTUP_TOOLCHAIN','RUSTC','CARGO','LD_LIBRARY_PATH','LD_PRELOAD']:env.pop(n,None)
 task=E/'native-compile';task.mkdir(exist_ok=True)
 env.update(GITHUB_WORKSPACE=str(SRC),SEEKDB_TASK_DIR=str(task),
            SEEKDB_RUST_INSTALL_ROOT=str(ROOT/'rust'),DEP_CACHE_DIR=str(ROOT/'deps-cache'),
            MAKE=str(guard),MAKE_ARGS='-j2')
 report['starting_path']=env['PATH']
 start=time.monotonic()
 with (E/'native-compile-entry.log').open('w') as log:
  p=subprocess.run(['bash',str(SRC/'.github/script/seekdb/compile.sh')],cwd=SRC,env=env,
                   stdout=log,stderr=subprocess.STDOUT,timeout=900)
 report.update(entry_exit=p.returncode,entry_seconds=time.monotonic()-start)
 text=(E/'native-compile-entry.log').read_text()
 assert p.returncode==42,text[-4000:]
 assert '[native-entry-probe] reached make' in text
 assert '[compile.sh] final phase=build exit=42' in text
 cache=(SRC/'build_release/CMakeCache.txt').read_text()
 for flag in ['OB_BUILD_CUVS','OB_BUILD_HIPVS']:
  assert any(x.startswith(flag+':') and x.endswith('=ON') for x in cache.splitlines()),flag
 report.update(completed=True,stage='passed',actual_dependency_init_passed=True,
               actual_gpu_configure_passed=True,toolchain_version='1.98.1',
               rustup_installer_sha256=hashlib.sha256((ROOT/'rust/rustup-init').read_bytes()).hexdigest(),
               note='Real init and GPU CMake configuration; deliberate make guard stops before C++ compilation. Earlier full GPU/TGZ/SQL acceptance is separately recorded.')
 save()
except Exception as error:
 report.update(stage='failed',error=repr(error));save();traceback.print_exc();raise
