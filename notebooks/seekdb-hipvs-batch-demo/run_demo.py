"""Execute the seven-section notebook on the seekdb-hipvs course image."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def main():
    import nbclient
    import nbformat
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('results/executed.ipynb'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    output = args.output if args.output.is_absolute() else root/args.output
    output.parent.mkdir(parents=True,exist_ok=True)
    source = root/'seekdb_hipvs_batch_demo.ipynb'
    notebook = nbformat.read(source,as_version=4)
    for cell in notebook.cells:
        if cell.cell_type == 'code':
            cell.outputs=[]
            cell.execution_count=None
    report = {'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
              'completed':False,'python':platform.python_version(),
              'notebook_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
              'helper_sha256':hashlib.sha256((root/'lib/seekdb_demo.py').read_bytes()).hexdigest(),
              'scope':'course-image batch-query demonstration, not HNSW construction'}
    def progress(cell,cell_index,**kwargs):
        print(json.dumps({'cell':cell_index,'type':cell.cell_type}),flush=True)
    try:
        nbclient.NotebookClient(notebook,timeout=660,kernel_name='python3',
            resources={'metadata':{'path':str(root)}},on_cell_start=progress).execute()
        home = Path(os.environ.get('SEEKDB_HOME','/seekdb_workspace'))
        report['binary_version']=subprocess.check_output([str(home/'bin/seekdb'),'--version'],text=True)
        report['measured_files']={}
        for path in [home/'bin/seekdb',home/'bridge/libseekdb_cuvs_bridge.so',
                     home/'datasets/base.f32',home/'datasets/query.f32',home/'datasets/gt_100x10.i32']:
            digest=hashlib.sha256()
            with path.open('rb') as stream:
                for chunk in iter(lambda:stream.read(8*1024*1024),b''):
                    digest.update(chunk)
            report['measured_files'][str(path)]=digest.hexdigest()
        report['completed']=True
    except Exception as error:
        report['error']=str(error)
        raise
    finally:
        nbformat.write(notebook,output)
        report['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
        (output.parent/'execution.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps({'completed':report['completed'],'notebook':str(output)}),flush=True)


if __name__ == '__main__':
    main()
