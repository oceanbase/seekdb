#!/usr/bin/env python3
"""Extract the actual candidate traversal; only test adapters and assertions are handwritten."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import time
import tempfile

base = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=pathlib.Path, default=base.parents[2] / 'src/storage/scheduler/ob_dag_scheduler.cpp')
parser.add_argument('--compiler', default='clang++')
parser.add_argument('--output', type=pathlib.Path, help='Keep generated sources, binaries and results here')
args = parser.parse_args()
temporary = tempfile.TemporaryDirectory(prefix='mini-scheduler-') if args.output is None else None
output = pathlib.Path(temporary.name) if temporary is not None else args.output
output.mkdir(parents=True, exist_ok=True)
raw = args.source.read_bytes()
text = raw.decode()
signature = 'int ObDagPrioScheduler::pop_task_from_ready_list_(ObITask *&task)'
start = text.index(signature)
end = text.index('\n}\n', start) + len('\n}\n')
function = text[start:end]
helper_start = text.index('void ObDagPrioScheduler::record_ready_task_dispatch_(const ObDagType::ObDagTypeEnum dag_type)')
helper_end = text.index('\n}\n', helper_start) + len('\n}\n')
helper = text[helper_start:helper_end]
out = output / 'test-actual-traversal.cpp'
out.write_text((base/'mocks.cpp').read_text()+'\n'+helper+'\n'+function+'\n'+(base/'cases.cpp').read_text()+'\n'+(base/'main.cpp').read_text())
metadata = {'source': str(args.source.resolve()), 'source_sha256': hashlib.sha256(raw).hexdigest(),
            'function_sha256': hashlib.sha256(function.encode()).hexdigest(),
            'dispatch_helper_sha256': hashlib.sha256(helper.encode()).hexdigest(),
            'source_start_line': text[:start].count('\n')+1,
            'extraction': 'verbatim function; mocked DAG/list/dispatch environment; no reimplemented selection logic',
            'compiler': args.compiler, 'runs': []}
for name, flags in [('optimized', ['-O2']), ('sanitized', ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
    binary = output / ('test-'+name)
    build = [args.compiler,'-std=c++17','-Wall','-Wextra','-Werror',*flags,str(out),'-o',str(binary)]
    built = subprocess.run(build, text=True, capture_output=True)
    record = {'name':name,'command':build,'build_exit':built.returncode,'build_output':built.stdout+built.stderr}
    if built.returncode == 0:
        run = subprocess.run([str(binary)], text=True, capture_output=True)
        record.update(exit_code=run.returncode,output=run.stdout+run.stderr)
    metadata['runs'].append(record)
    (output/('test-'+name+'.log')).write_text(record['build_output']+record.get('output',''))
    print(name, record.get('exit_code', record['build_exit']))
    print(record['build_output']+record.get('output',''))
metadata['completed_unix'] = time.time()
(output/'test-results.json').write_text(json.dumps(metadata,indent=2)+'\n')
raise SystemExit(0 if all(r.get('exit_code')==0 for r in metadata['runs']) else 1)
