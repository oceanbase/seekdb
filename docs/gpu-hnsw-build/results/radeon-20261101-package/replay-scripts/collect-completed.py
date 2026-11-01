from pathlib import Path
from datetime import datetime, timezone
import gzip, hashlib, json, subprocess, tarfile
r=Path('/workspace/amd-acceptance-20261101-6dc14ba3');e=r/'evidence';src=r/'src'
s=json.loads((e/'package-status.json').read_text());assert s['completed'] and s['stage']=='passed'
original=json.loads((e/'source-sha256.json').read_text())
for name,expected in original.items():
 assert hashlib.sha256((src/name).read_bytes()).hexdigest()==expected,name
receipt=dict(created_utc=datetime.now(timezone.utc).isoformat(),compiled_source_commit=s['binary_source_commit'],package_source_commit=s['source_commit'],tracked_files_unchanged_through_acceptance=len(original),binary_sha256=s['unpackaged_binary_sha256'],initial_source_manifest='source-sha256.json',git_status=subprocess.check_output(['git','status','--porcelain'],cwd=src,text=True))
assert not receipt['git_status']
all_events=[]
for p in (Path('/var/tmp/amd-package-20261101-cfae4ba9-info')/'log').glob('seekdb.log*'):
 if p.is_file() and not p.name.endswith('.gz'):
  with p.open('rb') as source,gzip.open(e/(p.name+'.gz'),'wb') as target:
   while chunk:=source.read(1024*1024):target.write(chunk)
  events=[x for x in p.read_text(errors='replace').splitlines() if '[HIPVS_BUILD]' in x]
  all_events.extend(events)
(e/'package-gpu-events.log').write_text('\n'.join(all_events)+'\n')
for network_file in ['/proc/net/tcp','/proc/net/tcp6']:
 for line in Path(network_file).read_text().splitlines()[1:]:
  fields=line.split()
  assert not (fields[3]=='0A' and int(fields[1].split(':')[1],16)==2897),line
receipt['dedicated_port_closed_after_acceptance']=True
(e/'final-provenance.json').write_text(json.dumps(receipt,indent=2)+'\n')
with tarfile.open(r/'completed-evidence.tar.gz','w:gz') as archive:archive.add(e,arcname='evidence')
print(json.dumps(receipt,indent=2))
print('evidence_archive_bytes',(r/'completed-evidence.tar.gz').stat().st_size)
