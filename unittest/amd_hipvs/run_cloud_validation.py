"""Run the new binary in a dedicated, owned instance; retain raw test artifacts."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import signal
import socket
import subprocess
import sys
import time
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace', type=Path, default=Path('/workspace'))
parser.add_argument('--output', type=Path, required=True,
                    help='New report directory; existing results are never overwritten')
parser.add_argument('--port', type=int, default=2883)
args = parser.parse_args()
if not 1024 <= args.port <= 65535:
    parser.error('Use an unoccupied unprivileged port')
ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = args.workspace.resolve()
WORKSPACE.mkdir(parents=True, exist_ok=True)
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=False)
BUILD = OUT / 'bridge'
BUILD.mkdir()
LOG = (OUT / 'driver.log').open('a', buffering=1)
sys.stdout = LOG
sys.stderr = LOG
print('START', time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime()), flush=True)
RUNTIME = WORKSPACE / ('amd-runtime-' + uuid.uuid4().hex[:10])
PORT = args.port
process = None
console = None
connection = None
summary = {'status': 'RUNNING', 'runtime': str(RUNTIME), 'port': PORT}


def save():
    temporary = OUT / 'status.json.tmp'
    temporary.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    temporary.replace(OUT / 'status.json')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256_file(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(command, **kwargs):
    print('COMMAND', command, flush=True)
    return subprocess.run(command, check=True, stdout=LOG, stderr=LOG, **kwargs)


def stop():
    global process, console
    try:
        if process and process.poll() is None:
            print('Graceful shutdown of owned seekdb', process.pid, flush=True)
            process.terminate()
            try:
                process.wait(timeout=120)
            except subprocess.TimeoutExpired:
                # Only the Popen child owned by this controller is eligible.
                # A forced stop must never count as a successful graceful restart.
                process.kill()
                process.wait(timeout=30)
                raise RuntimeError('Owned seekdb required forced shutdown after 120 seconds')
    finally:
        if console:
            console.close()
            console = None


def interrupted(signum, frame):
    raise KeyboardInterrupt(f'Validation interrupted by signal {signum}')


signal.signal(signal.SIGTERM, interrupted)


try:
    save()
    binary = ROOT / 'build_release/src/observer/seekdb'
    if not binary.is_file():
        raise RuntimeError('Built seekdb binary missing')
    with socket.socket() as probe:
        if probe.connect_ex(('127.0.0.1', PORT)) == 0:
            raise RuntimeError('Dedicated test port is already occupied')
    import pymysql
    import numpy as np
    sys.path.insert(0, str(ROOT / 'unittest/amd_hipvs'))
    import benchmark
    summary['source_commit'] = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    summary['source_dirty'] = bool(subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=all'], cwd=ROOT).strip())
    summary['controller_sha256'] = sha256_file(__file__)
    summary['benchmark_sha256'] = sha256_file(ROOT / 'unittest/amd_hipvs/benchmark.py')
    summary['phase'] = 'BUILD_BRIDGE'
    save()
    run(['bash', str(ROOT / 'tools/hipvs_bridge_build/build.sh'), str(BUILD)])
    run([sys.executable, str(ROOT / 'unittest/amd_hipvs/probe_gpu.py'), '--output-dir', str(BUILD)])
    devel = ROOT / 'deps/3rd/usr/local/oceanbase/deps/devel'
    env = os.environ.copy()
    env['SEEKDB_HIPVS_BUILD_LIBRARY'] = str(BUILD / 'libseekdb_hipvs_build.so')
    env['SEEKDB_HIPVS_BUILD_THREADS'] = '8'
    env['LD_LIBRARY_PATH'] = ':'.join([str(devel / 'lib/vsag_lib'), str(devel / 'lib'),
        '/opt/hipvs/lib', '/opt/rocm/lib', env.get('LD_LIBRARY_PATH', '')])
    cpus = sorted(os.sched_getaffinity(0))[:8]
    if len(cpus) != 8:
        raise RuntimeError('This fixed reproduction configuration requires 8 available CPU IDs')
    command = [str(binary), '--nodaemon', '--base-dir', str(RUNTIME), '--port', str(PORT),
        '--parameter', 'memory_budget=8G', '--parameter', 'vector_memory_limit=2G',
        '--parameter', 'cpu_count=8', '--parameter', 'server_cpu_quota_min=8',
        '--parameter', 'server_cpu_quota_max=8', '--parameter', 'datafile_size=1G',
        '--parameter', 'datafile_maxsize=8G', '--parameter', 'log_disk_size=4G']

    def start():
        global process, console
        console = (OUT / 'server-console.log').open('a')
        process = subprocess.Popen(command, env=env, stdout=console, stderr=console,
                                   preexec_fn=lambda: os.sched_setaffinity(0, cpus))
        summary['pid'] = process.pid
        save()
        until = time.monotonic() + 240
        last = None
        while time.monotonic() < until:
            if process.poll() is not None:
                raise RuntimeError(f'Owned seekdb exited during startup: {process.returncode}')
            candidate = None
            try:
                candidate = pymysql.connect(host='127.0.0.1', port=PORT, user='root',
                    autocommit=True, connect_timeout=3, read_timeout=300, write_timeout=300)
                with candidate.cursor() as cur:
                    cur.execute('SELECT 1')
                    require(cur.fetchone()[0] == 1, 'Database readiness query returned an unexpected result')
                    cur.execute('SET SESSION ob_query_timeout=300000000')
                    cur.execute('SET SESSION ob_hnsw_ef_search=200')
                ready = candidate
                candidate = None
                return ready
            except pymysql.MySQLError as error:
                last = type(error).__name__, error.args[0]
                time.sleep(2)
            finally:
                if candidate is not None:
                    candidate.close()
        raise RuntimeError(f'Database readiness timed out: {last}')

    connection = start()
    stderr_path = Path(os.readlink(f'/proc/{process.pid}/fd/2'))
    summary['server_stderr'] = str(stderr_path)
    summary['binary_sha256'] = sha256_file(binary)
    summary['bridge_sha256'] = sha256_file(BUILD / 'libseekdb_hipvs_build.so')
    summary['binary_version'] = subprocess.check_output(
        [str(binary), '--version'], env=env, stderr=subprocess.STDOUT, text=True, timeout=30).strip()
    summary['phase'] = 'SQL_LIFECYCLE'
    save()
    rng = np.random.default_rng(20261031)
    vectors = rng.normal(size=(128, 8)).astype(np.float32)
    ids = [1000000000001 + i * 7 for i in range(128)]
    query = benchmark.query_sql('lifecycle', 'lifecycle_gpu', 10)
    q = benchmark.vector_json(vectors[3] * .9)
    with connection.cursor() as cur:
        cur.execute('CREATE DATABASE amd_hipvs_validation')
        cur.execute('USE amd_hipvs_validation')
        cur.execute('CREATE TABLE lifecycle(id BIGINT PRIMARY KEY, v VECTOR(8) NOT NULL)')
        cur.executemany('INSERT INTO lifecycle VALUES (%s,%s)',
                        [(doc_id, benchmark.vector_json(v)) for doc_id, v in zip(ids, vectors)])
        pos = benchmark.log_position(stderr_path)
        cur.execute('CREATE VECTOR INDEX lifecycle_gpu ON lifecycle(v) WITH '
                    '(distance=l2,type=hnsw,lib=hipvs,m=16,ef_construction=200,ef_search=200)')
        cur.execute(query, (q, q))
        before = cur.fetchall()
        require(len(before) == 10, 'Lifecycle query returned fewer than 10 neighbors')
        summary['lifecycle_gpu_evidence'] = benchmark.evidence_from_log(
            stderr_path, pos, 'lifecycle', 'lifecycle_gpu', 128, 8)
        cur.execute('EXPLAIN ' + query, (q, q))
        summary['lifecycle_plan'] = '\n'.join(str(row[0]) for row in cur.fetchall())
        require('VECTOR INDEX' in summary['lifecycle_plan'].upper(), 'Lifecycle query did not use a vector index')
    connection.close()
    connection = None
    stop()
    connection = start()
    with connection.cursor() as cur:
        cur.execute('USE amd_hipvs_validation')
        cur.execute(query, (q, q))
        after = cur.fetchall()
        require([r[0] for r in before] == [r[0] for r in after], 'Restart changed document IDs')
        require(all(abs(float(a[1]) - float(b[1])) < 1e-5 for a, b in zip(before, after)), 'Restart changed distances')
        cur.execute('INSERT INTO lifecycle VALUES (%s,%s)', (9000000000000001, benchmark.vector_json(np.zeros(8))))
        zero = benchmark.vector_json(np.zeros(8))
        cur.execute(benchmark.query_sql('lifecycle', 'lifecycle_gpu', 1), (zero, zero))
        inserted = cur.fetchone()
        require(inserted is not None and inserted[0] == 9000000000000001, 'Incremental insert unavailable after restart')
        cur.execute('DELETE FROM lifecycle WHERE id=9000000000000001')
        cur.execute(benchmark.query_sql('lifecycle', 'lifecycle_gpu', 1), (zero, zero))
        remaining = cur.fetchone()
        require(remaining is not None and remaining[0] != 9000000000000001, 'Deleted document still returned or query returned no neighbors')
    connection.close()
    connection = None
    summary['lifecycle'] = 'PASS: SQL GPU CREATE, restart IDs/distances, incremental insert/delete'
    summary['phase'] = 'BENCHMARK'
    save()
    cpu = next(line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
               if line.startswith('model name'))
    hardware = {'cpu_model': cpu, 'cpu_threads': len(cpus), 'cpu_affinity': cpus,
                'gpu_model': summary['lifecycle_gpu_evidence']['gpu_name'],
                'gpu_architecture': summary['lifecycle_gpu_evidence']['gpu_architecture'],
                'rocm_version': subprocess.check_output(['/opt/rocm/bin/hipcc', '--version'], text=True).strip(),
                'hipvs_version': 'preinstalled libcuvs_c.so sha256=' + sha256_file('/opt/hipvs/lib/libcuvs_c.so'),
                'seekdb_commit': summary['source_commit'],
                'cgroup_cpu_max': Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
                'cgroup_memory_max': Path('/sys/fs/cgroup/memory.max').read_text().strip()}
    (OUT / 'environment.json').write_text(json.dumps(hardware, indent=2))
    # benchmark.py is also usable against remote servers. This controller owns
    # a local instance, so inherited credentials/host settings must not redirect it.
    os.environ.update(AMD_BENCH_HOST='127.0.0.1', AMD_BENCH_PORT=str(PORT),
                      AMD_BENCH_USER='root', AMD_BENCH_PASSWORD='',
                      AMD_BENCH_DATABASE='amd_hipvs_validation')
    stderr_path = Path(os.readlink(f'/proc/{process.pid}/fd/2'))
    common = ['--gpu-with', 'distance=l2,type=hnsw,lib=hipvs,m=16,ef_construction=200,ef_search=200',
              '--repeats', '3', '--min-recall', '0.90', '--environment-json', str(OUT / 'environment.json'),
              '--server-stderr', str(stderr_path)]
    for label, shape in [('smoke', ['--rows', '200', '--dim', '16', '--queries', '20']),
                         ('build-10000x128', ['--rows', '10000', '--dim', '128', '--queries', '100'])]:
        result = benchmark.main(common + shape + ['--output', str(OUT / (label + '.json'))])
        summary[label] = json.loads((OUT / (label + '.json')).read_text())['summary']
        save()
        if result != 0:
            raise RuntimeError(f'{label} benchmark failed; inspect its JSON and server log')
    summary['status'] = 'PASS_LOCAL_VALIDATION_TARGET_UNSPECIFIED'
    summary['phase'] = 'DONE'
except (Exception, KeyboardInterrupt) as error:
    import traceback
    traceback.print_exc()
    summary['status'] = 'FAILED'
    summary['error'] = str(error)
finally:
    if connection is not None:
        try:
            connection.close()
        except Exception as error:
            summary['connection_cleanup_error'] = type(error).__name__
            summary['status'] = 'FAILED'
    try:
        stop()
    except Exception as error:
        summary['shutdown_error'] = str(error)
        summary['status'] = 'FAILED'
    save()
    print(json.dumps(summary, indent=2), flush=True)

raise SystemExit(0 if summary['status'] == 'PASS_LOCAL_VALIDATION_TARGET_UNSPECIFIED' else 2)
