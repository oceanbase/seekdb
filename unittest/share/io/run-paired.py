from pathlib import Path
import argparse,datetime,hashlib,json,os,shutil,signal,statistics,subprocess,time

unit=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description='Prepare or run alternating local I/O comparisons. Profile and pilot runs never enter measurement summaries.')
parser.add_argument('--before',type=Path,required=True)
parser.add_argument('--after',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--mode',choices=['plan','pilot','profile','measure'],default='plan')
parser.add_argument('--read-operations',type=int,default=1048576)
parser.add_argument('--write-operations',type=int,default=131072)
parser.add_argument('--case',choices=['native-read','native-write','worker-read','worker-write'],action='append')
parser.add_argument('--timeout-seconds',type=int,default=900)
parser.add_argument('--perf',action='store_true',help='Also attach perf record in profile mode; never used in measured runs.')
args=parser.parse_args()
assert args.read_operations>=32 and args.write_operations>=4096
assert not args.perf or args.mode=='profile'
cases=args.case or ['native-read','worker-read','native-write','worker-write']
assert len(cases)==len(set(cases))
out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
shutil.copyfile(__file__,out/'runner.py')
def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
def command_snapshot(command):
 result=subprocess.run(command,text=True,capture_output=True)
 return {'command':command,'exit':result.returncode,'stdout':result.stdout,'stderr':result.stderr}
def ticks():
 result={}
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   line=(p/'stat').read_text();head,tail=line.rsplit(')',1);fields=tail.split()
   result[int(p.name)]={'name':head.split('(',1)[1],'ticks':int(fields[11])+int(fields[12]),'parent':int(fields[1])}
  except (OSError,ValueError,IndexError):continue
 return result
def host_snapshot():
 before=ticks();start=time.monotonic();time.sleep(.25);after=ticks();elapsed=time.monotonic()-start
 processes=[];frequency=os.sysconf('SC_CLK_TCK')
 for pid,value in after.items():
  previous=before.get(pid)
  if previous and value['name']==previous['name']:
   cpu=max(0,value['ticks']-previous['ticks'])/frequency/elapsed*100
   if cpu>0:processes.append({'pid':pid,'parent':value['parent'],'name':value['name'],'cpu_percent':cpu})
 return {'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':time.monotonic_ns(),'logical_cpus':os.cpu_count(),'sample_seconds':elapsed,'active_processes':sorted(processes,key=lambda x:x['cpu_percent'],reverse=True),'loadavg':Path('/proc/loadavg').read_text(),'meminfo':Path('/proc/meminfo').read_text(),'diskstats':Path('/proc/diskstats').read_text(),'process_inventory':command_snapshot(['ps','-eo','pid,ppid,comm,pcpu,pmem,etime'])}
artifacts={}
for side,path in [('before',args.before),('after',args.after)]:
 path=path.resolve(strict=True);receipt=json.loads((path/'verification.json').read_text());assert receipt['exit']==0 and receipt['side']==side
 exe=path/'io_path';source=path/'io_path.cpp';lib=path/'liboceanbase.so'
 assert sha(exe)==receipt['executable_sha256']
 assert sha(source)==receipt['source_sha256']
 assert sha(lib)==receipt['library_sha256']
 dynamic=command_snapshot(['readelf','-d',str(exe)])
 assert dynamic['exit']==0 and '['+str(lib)+']' in dynamic['stdout']
 artifacts[side]={'directory':str(path),'executable':str(exe),'library':str(lib),'source':str(source),'executable_sha256':receipt['executable_sha256'],'library_sha256':receipt['library_sha256'],'source_sha256':receipt['source_sha256'],'compile_command':json.loads((path/'command.json').read_text()),'dynamic':dynamic}
assert artifacts['before']['source_sha256']==artifacts['after']['source_sha256']
repetitions=5 if args.mode in ('plan','measure') else 1
schedule=[]
for case in cases:
 backend,operation=case.split('-');operations=args.read_operations if operation=='read' else args.write_operations
 for repetition in range(repetitions):
  for side in (['before','after'] if repetition%2==0 else ['after','before']):
   directory=out/case/(str(repetition+1)+'-'+side)
   schedule.append({'case':case,'side':side,'repetition':repetition+1,'directory':str(directory),'command':[artifacts[side]['executable'],str(directory/'data.bin'),backend,operation,'1024',str(operations),'measure'],'operations':operations,'request_bytes':16384 if operation=='read' else 262144})
provenance={'mode':args.mode,'reportable_timing':args.mode=='measure','artifacts':artifacts,'file_bytes':1<<30,'fixed_image_bytes':1<<30,'read_operations':args.read_operations,'write_operations':args.write_operations,'repetitions_per_side_per_case':repetitions,'configuration_note':'No fixture warmup. Pilot must establish a sufficient interval. Completion timing includes payload comparison and final drain, excludes preparation and final full-file verification. Final fdatasync is separate. Observed latency is application slot-wait observation. Peak RSS includes the 1 GiB image.','schedule':schedule}
save(out/'provenance.json',provenance)
if args.mode=='plan':
 print('Plan saved without running any workload:',out/'provenance.json')
 raise SystemExit(0)
results=[]
for planned in schedule:
 directory=Path(planned['directory']);directory.mkdir(parents=True,exist_ok=False)
 save(directory/'host-before.json',host_snapshot());save(directory/'command.json',planned['command'])
 monitors=[];monitor_files=[];monitor_details=[]
 with (directory/'stdout.log').open('w') as stdout,(directory/'stderr.log').open('w') as stderr:
  process=subprocess.Popen(planned['command'],cwd=directory,stdout=stdout,stderr=stderr)
  save(directory/'process.json',{'pid':process.pid,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':time.monotonic_ns()})
  try:
   if args.mode=='profile':
    commands=[('pidstat',['pidstat','-h','-H','-u','-r','-d','-p',str(process.pid),'1']),('iostat',['iostat','-t','-y','-x','-d','1'])]
    if args.perf:commands.append(('perf',['perf','record','-F','99','--call-graph','dwarf,8192','-p',str(process.pid),'-o',str(directory/'perf.data')]))
    for name,command in commands:
     detail={'name':name,'command':command};monitor_details.append(detail)
     if not shutil.which(command[0]):detail['unavailable']=True;continue
     output=(directory/(name+'.log')).open('w');monitor_files.append(output)
     monitor=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT,cwd=directory)
     monitors.append((monitor,detail))
   try:code=process.wait(timeout=args.timeout_seconds)
   except subprocess.TimeoutExpired:
    process.terminate()
    try:code=process.wait(timeout=15)
    except subprocess.TimeoutExpired:process.kill();code=process.wait()
    save(directory/'timeout.json',{'timeout_seconds':args.timeout_seconds,'exit':code})
  finally:
   if process.poll() is None:
    process.terminate()
    try:process.wait(timeout=15)
    except subprocess.TimeoutExpired:process.kill();process.wait()
   for monitor,detail in monitors:
    if monitor.poll() is None:monitor.send_signal(signal.SIGINT)
    try:detail['exit']=monitor.wait(timeout=10)
    except subprocess.TimeoutExpired:monitor.kill();detail['exit']=monitor.wait()
   for f in monitor_files:f.close()
 if monitor_details:save(directory/'profile-commands.json',monitor_details)
 save(directory/'host-after.json',host_snapshot())
 parsed=[json.loads(line) for line in (directory/'stdout.log').read_text().splitlines() if line.startswith('{')]
 payloads=[x for x in parsed if 'success' in x];phases=[x for x in parsed if 'phase' in x]
 result={'case':planned['case'],'side':planned['side'],'repetition':planned['repetition'],'exit':code,'directory':str(directory),'phases':phases,'reportable_timing':args.mode=='measure'}
 if len(payloads)==1:result['result']=payloads[0]
 data=result.get('result',{})
 valid=(code==0 and data.get('success') is True and data.get('mode')=='measure'
        and data.get('operations')==planned['operations']
        and data.get('accepted')==data.get('completed')==planned['operations']
        and data.get('logical_completed_bytes')==planned['operations']*planned['request_bytes']
        and data.get('file_bytes')==(1<<30) and data.get('request_bytes')==planned['request_bytes']
        and data.get('full_file_verified_after') is True
        and all(data.get(x)==0 for x in ['submission_errors','completion_errors','short_io','data_errors'])
        and data.get('completion_seconds_including_drain',0)>0 and len(phases)==2
        and [x['phase'] for x in phases]==['io_begin','io_end'])
 result['valid']=valid
 if valid:
  duration=data['completion_seconds_including_drain'];gib=data['logical_completed_bytes']/(1<<30)
  result['metrics']={'throughput_mib_s':data['logical_completed_bytes']/(1<<20)/duration,'operations_per_second':data['completed']/duration,'observed_p99_ms':data['observed_latency_p99_ns']/1e6,'cpu_seconds_per_gib':data['cpu_seconds_including_drain']/gib,'max_rss_kib_including_fixed_image':data['max_rss_kib'],'completion_seconds':duration,'fdatasync_seconds':data['final_fdatasync_seconds']}
  result['interval_at_least_10_seconds']=duration>=10
 if valid:Path(planned['command'][1]).unlink()
 results.append(result);save(out/'runs.json',results)
 print(json.dumps({k:v for k,v in result.items() if k not in ['result','phases']}),flush=True)
 if not valid:raise SystemExit('Invalid workload result; no successful performance verdict: '+str(directory))
summary={'mode':args.mode,'reportable_timing':args.mode=='measure','performance_verdict':'not interpreted; review limiter, variability and end-to-end workload','all_runs_valid':True,'all_intervals_at_least_10_seconds':all(r['interval_at_least_10_seconds'] for r in results),'fixed_image_bytes':1<<30,'groups':{}}
for case in cases:
 for side in ['before','after']:
  selected=[r['metrics'] for r in results if r['case']==case and r['side']==side]
  summary['groups'][case+'/'+side]={'runs':len(selected),'metrics':{name:{'median':statistics.median(r[name] for r in selected),'min':min(r[name] for r in selected),'max':max(r[name] for r in selected)} for name in selected[0]}}
for artifact in artifacts.values():
 for field in ['executable','library','source']:assert sha(Path(artifact[field]))==artifact[field+'_sha256']
save(out/'summary.json',summary)
print('Collected evidence:',out/'summary.json')
