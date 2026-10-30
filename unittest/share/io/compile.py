from pathlib import Path
import hashlib,json,subprocess,sys
import argparse
parser=argparse.ArgumentParser(description='Compile the same public I/O benchmark against a selected built seekdb tree.')
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--side',choices=['before','after'],required=True)
parser.add_argument('--library-sha256',required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
unit=Path(__file__).resolve().parent
root=args.root.resolve(strict=True)
side=args.side
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
lib=root/'build/bin/src/observer/liboceanbase.so'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as source:
  for chunk in iter(lambda:source.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
expected_hash=args.library_sha256
assert sha(lib)==expected_hash, 'selected library hash differs'
(out/'tree.json').write_text(json.dumps({'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','HEAD'],cwd=root)).hexdigest()},indent=2)+'\n')
frozen_lib=out/'liboceanbase.so';subprocess.run(['cp','--reflink=auto',str(lib),str(frozen_lib)],check=True);assert sha(frozen_lib)==expected_hash
source=unit/'io_path.cpp';(out/source.name).write_bytes(source.read_bytes())
third=root/'deps/3rd/usr/local/oceanbase'
command=[str(third/'devtools/bin/clang++'),'-std=gnu++20','-O2','-g','-fPIC','-ffunction-sections','-fdata-sections','-fmax-type-align=8','-fno-strict-aliasing','-Wno-everything','-D_GLIBCXX_USE_CXX11_ABI=1','-D_NO_EXCEPTION','-DOB_BUILD_LITE','-DOB_BUILD_OBSERVER_LITE','-DENABLE_INITIAL_EXEC_TLS_MODEL']
for p in ['.','src','src/oblib','src/oblib/common','src/observer','src/logservice/palf','src/oblib/easy','src/oblib/easy/include','src/data_plane/api','src/query/api',str(third/'deps/devel/include/libxml2'),'build/bin','build/bin/src','build/bin/src/share/inner_table/bazel_generated',str(third/'deps/devel/include')]:command+=['-I'+p]
for p in (root/'build/bin/src').rglob('_virtual_includes'):
 command+=['-I'+str(x) for x in p.iterdir() if x.is_dir()]
command += [str(out/source.name),str(frozen_lib),'-fuse-ld=lld','-Wl,--gc-sections','-Wl,--export-dynamic','-Wl,-rpath,'+str(out),'-pthread','-ldl','-lm','-o',str(out/'io_path')]
(out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
with (out/'compile.log').open('w') as log:r=subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=300)
report={'exit':r.returncode,'source_sha256':sha(source),'library_sha256':sha(lib),'root':str(root),'side':side}
if r.returncode==0:report['executable_sha256']=sha(out/'io_path')
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
raise SystemExit(r.returncode)
