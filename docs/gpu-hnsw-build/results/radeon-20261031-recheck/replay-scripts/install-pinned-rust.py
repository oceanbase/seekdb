import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

run=Path('/workspace/hipvs-recheck-20261031')
scratch=Path('/workspace/hipvs-recheck-20261031/build-area')
prefix=scratch/'tooling/rust-1.98.1'
manifest=json.loads((run/'incoming/rust-packages.json').read_text())
for pkg in manifest:
    archive=run/'incoming'/pkg['file']
    assert archive.stat().st_size==pkg['bytes']
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==pkg['sha256'],archive
    target=scratch/'tooling/rust-packages'/pkg['package']
    target.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive,'r:xz') as packed:
        packed.extractall(target,filter='data')
    installers=list(target.glob('*/install.sh'))
    assert len(installers)==1,installers
    subprocess.run(['bash',str(installers[0]),'--prefix='+str(prefix),'--disable-ldconfig'],check=True)
    print('Installed and verified:',pkg['package'],flush=True)
for tool in ('rustc','cargo'):
    subprocess.run([str(prefix/'bin'/tool),'--version'],check=True)
(run/'evidence/rust-packages.json').write_text(json.dumps(manifest,indent=2))
