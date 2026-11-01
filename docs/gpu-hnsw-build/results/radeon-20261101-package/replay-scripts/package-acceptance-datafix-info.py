"""Wait for this candidate build, create real TGZs and test the extracted server."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import time
import traceback

ROOT = Path('/workspace/amd-acceptance-20261101-6dc14ba3')
SRC = ROOT / 'src'
EVIDENCE = ROOT / 'evidence'
SHA = 'cfae4ba92b14d3010f7c7aa6a87d064dfd7c0075'
PACKAGE_SHA = 'f18737e699ad0b29e38759fa821fec79e3940545'
DB = Path('/var/tmp/amd-package-20261101-cfae4ba9-info')
PYTHON = '/opt/venv/bin/python'
PORT = '2897'
stage = 'waiting-for-build'
env = os.environ.copy()
env.pop('LD_LIBRARY_PATH', None)
env.pop('LD_PRELOAD', None)
env.pop('HIP_VISIBLE_DEVICES', None)
prefix = ROOT / 'tooling/rust-1.98.1'
env.update(PATH=str(prefix / 'bin') + ':/opt/venv/bin:/opt/rocm/bin:' + env.get('PATH', ''),
           CARGO_HOME=str(ROOT / 'tooling/cargo'), CARGO_NET_OFFLINE='true',
           CARGO_BUILD_JOBS='1', MAKEFLAGS='-j2', OMP_NUM_THREADS='8', OPENBLAS_NUM_THREADS='8')
report = dict(source_commit=PACKAGE_SHA, binary_source_commit=SHA,
              build_reused_for_data_only_change=True, completed=False, no_system_install=True,
              package_install_hooks_executed=False, syslog_level='INFO',
              previous_attempt_issue='TGZ defaults to DBA_WARN; INFO GPU evidence was suppressed', steps=[])
server_started = False


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save():
    report.update(stage=stage, updated_utc=datetime.now(timezone.utc).isoformat())
    (EVIDENCE / 'package-status.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(stage=stage, completed=report['completed'],
                          updated_utc=report['updated_utc'])), flush=True)


def run(label, args, selected_env=None, timeout=900):
    with (EVIDENCE / (label + '.log')).open('a') as log:
        log.write(json.dumps({'command': [str(x) for x in args]}) + '\n')
        log.flush()
        result = subprocess.run([str(x) for x in args], cwd=SRC, env=selected_env or env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    report['steps'].append(dict(label=label, returncode=result.returncode))
    save()
    result.check_returncode()


sdb = [PYTHON, SRC / '.github/script/seekdb/sdb.py']


def start(label, binary, selected_env=None):
    global server_started
    run(label, sdb + ['start', '--binary', binary, '--base-dir', DB, '--port', PORT,
                     '--parameter', 'cpu_count=16', '--parameter', 'syslog_level=INFO',
                     '--parameter', 'datafile_size=2G',
                     '--parameter', 'datafile_maxsize=8G', '--parameter', 'log_disk_size=2G'],
        selected_env, timeout=300)
    server_started = True
    run(label + '-ready', sdb + ['wait-ready', '--client', 'mysql', '--base-dir', DB,
                               '--port', PORT, '--timeout', '240'], selected_env, timeout=270)


def stop(label):
    global server_started
    run(label, sdb + ['stop', '--base-dir', DB])
    server_started = False


try:
    save()
    deadline = time.monotonic() + 10800
    while True:
        status_file = EVIDENCE / 'current-status.json'
        if status_file.exists():
            state = json.loads(status_file.read_text())
            assert state['source_commit'] == SHA
            if state['state'] == 'failed':
                raise RuntimeError('Build failed: ' + state.get('error', ''))
            if state['state'] == 'passed' and state['stage'] == 'build-passed':
                break
        if time.monotonic() > deadline:
            raise TimeoutError('Candidate build did not finish')
        time.sleep(20)
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SRC, text=True).strip() == SHA
    stage = 'package-prerequisites'
    save()
    # Make the image cache reusable without placing a packaging-only obshell in /opt.
    deps = SRC / 'deps/3rd'
    if deps.is_symlink():
        image_cache = deps.resolve()
        deps.unlink()
        deps.mkdir()
        for item in image_cache.iterdir():
            if item.name != 'home':
                (deps / item.name).symlink_to(item.resolve(), target_is_directory=item.is_dir())
    obshell = ROOT / 'tooling/obshell'
    receipt = json.loads((ROOT / 'tooling/obshell-receipt.json').read_text())
    assert digest(obshell) == receipt['extracted_binary_sha256']
    assert receipt['file'] in (SRC / 'deps/init/oceanbase.el9.x86_64.deps').read_text()
    target = deps / 'home/admin/oceanbase/bin/obshell'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(obshell, target)
    report['obshell'] = receipt
    previous = json.loads((EVIDENCE / 'package-status-before-timezone-fix.json').read_text())
    assert all(any(x['label'] == label and x['returncode'] == 0 for x in previous['steps'])
               for label in ['package-generate-admin', 'package-build-all'])
    data_receipt = json.loads((ROOT / 'restored-package-data.json').read_text())
    for item in data_receipt['restored_files'] + data_receipt['existing_data_files']:
        assert digest(SRC / item['path']) == item['sha256'], item['path']
    report['package_data'] = data_receipt
    main = SRC / 'build_tgz/src/observer/seekdb'
    version = subprocess.check_output([main, '--version'], env=env, text=True, stderr=subprocess.STDOUT)
    (EVIDENCE / 'package-binary-version.txt').write_text(version)
    assert SHA in version, version
    report['unpackaged_binary_sha256'] = digest(main)
    assert report['unpackaged_binary_sha256'] == previous['unpackaged_binary_sha256']
    stage = 'cpack-tgz'
    save()
    packages = ROOT / 'packages'
    run('cpack-tgz', ['/opt/venv/bin/cpack', '--config', SRC / 'build_tgz/CPackConfig.cmake',
                      '-G', 'TGZ', '-B', packages], timeout=1800)
    archives = sorted(packages.glob('*.tar.gz'))
    assert len(archives) == 2, [p.name for p in archives]
    report['packages'] = [dict(name=p.name, bytes=p.stat().st_size, sha256=digest(p)) for p in archives]
    unpacked = ROOT / 'unpacked'
    names = []
    for archive_path in archives:
        with tarfile.open(archive_path) as archive:
            names.extend(archive.getnames())
            archive.extractall(unpacked, filter='data')
    (EVIDENCE / 'package-file-list.json').write_text(json.dumps(names, indent=2) + '\n')
    binaries = list(unpacked.rglob('usr/bin/seekdb'))
    assert len(binaries) == 1, binaries
    binary = binaries[0]
    package_root = binary.parents[2]
    required = ['usr/bin/seekdb', 'usr/bin/obshell', 'usr/bin/seekdb_hipvs_hnsw_worker',
                'usr/libexec/seekdb/lib/libseekdb_hipvs_hnsw.so',
                'usr/libexec/seekdb/lib/libaio.so.1', 'etc/seekdb/seekdb.cnf',
                'usr/lib/systemd/system/seekdb.service']
    for name in required:
        assert (package_root / name).is_file(), name
    report['required_package_files'] = {name: digest(package_root / name) for name in required}
    for name in ['usr/bin/seekdb', 'usr/bin/seekdb_hipvs_hnsw_worker']:
        label = 'package-ldd-' + Path(name).name
        run(label, ['ldd', package_root / name])
        assert 'not found' not in (EVIDENCE / (label + '.log')).read_text()
    stage = 'extracted-package-sql'
    save()
    assert not DB.exists(), DB
    start('package-start', binary)
    run('package-contract', [PYTHON, SRC / 'unittest/sql/vector_index/check_hipvs_sql.py',
                             '--port', PORT, '--database', 'hipvs_package_contract_20261101',
                             '--output', EVIDENCE / 'package-contract.json'])
    benchmark = [PYTHON, SRC / 'docs/gpu-hnsw-build/benchmark.py', '--port', PORT,
                 '--gpu-lib', 'cuvs', '--rows', '50000', '--dim', '128', '--m', '32',
                 '--queries', '100', '--rounds', '3', '--min-recall', '0.99',
                 '--database', 'hipvs_package_benchmark_20261101',
                 '--server-log', DB / 'log/seekdb.log', '--output', EVIDENCE / 'package-sql.json']
    run('package-benchmark', benchmark, timeout=1800)
    stop('package-stop-before-restart')
    stage = 'hidden-gpu-package-restart'
    save()
    hidden = dict(env, HIP_VISIBLE_DEVICES='-1')
    start('package-hidden-gpu-restart', binary, hidden)
    run('package-restart-query', benchmark + ['--verify-existing'], hidden, timeout=900)
    stop('package-stop-final')
    result = json.loads((EVIDENCE / 'package-sql.json').read_text())
    contract = json.loads((EVIDENCE / 'package-contract.json').read_text())
    assert result['completed'] and result['restart_verification']['status'] == 'passed'
    assert contract['completed'] and len(contract['checks']) == 27
    run('package-final-source-check', ['git', 'diff', '--exit-code'])
    report.update(completed=True, contract_checks=len(contract['checks']),
                  fresh_build_runs=len(result['runs']), restart=result['restart_verification'],
                  ld_library_path_unset=True)
    stage = 'passed'
    save()
except Exception as error:
    report['error'] = repr(error)
    stage = 'failed'
    save()
    traceback.print_exc()
    raise
finally:
    if server_started:
        try:
            stop('package-stop-after-error')
        except Exception:
            traceback.print_exc()
