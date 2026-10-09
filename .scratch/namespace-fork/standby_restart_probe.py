import argparse, os, pathlib, signal, subprocess, time, sys
import pymysql
parser=argparse.ArgumentParser()
parser.add_argument('label')
parser.add_argument('--binary',default='build_release/src/observer/seekdb')
opts=parser.parse_args()
root=pathlib.Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work/db_s.z1.obs0')
for entry in pathlib.Path('/proc').iterdir():
    if not entry.name.isdigit(): continue
    try: exe=(entry/'exe').resolve()
    except OSError: continue
    if str(exe).startswith(str(root)+'/bin/'):
        os.kill(int(entry.name),signal.SIGTERM)
        for _ in range(100):
            if not (entry/'exe').exists(): break
            time.sleep(.1)
        else: os.kill(int(entry.name),signal.SIGKILL)
replacement=root/'bin/observer.restart-new'
subprocess.run(['cp','--reflink=auto',str(pathlib.Path(opts.binary).resolve()),str(replacement)],check=True)
os.replace(replacement,root/'bin/observer')
label=opts.label
log=pathlib.Path('.scratch/namespace-fork/standby-results')/(label+'.log')
env=os.environ.copy(); env['LD_LIBRARY_PATH']=str(root/'lib')+':'+env.get('LD_LIBRARY_PATH','')
args=[str(root/'bin/observer'),'--nodaemon','--port','42036','--data-dir',str(root/'store'),'--redo-dir',str(root/'store/clog'),'--role=STANDBY']
for opt in ['log_restore_source=6.12.232.130:42000','enable_rpc_service=true','rpc_port=42001','datafile_size=2G','log_disk_size=2G','cpu_count=4','memory_limit=8G']:
    args+=['--parameter',opt]
with log.open('w') as out:
    proc=subprocess.Popen(args,cwd=root,env=env,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
    for _ in range(200):
        if proc.poll() is not None:
            print('RED startup exited',proc.returncode); sys.exit(1)
        try:
            c=pymysql.connect(host='127.0.0.1',port=42036,user='root@sys',database='oceanbase',connect_timeout=1,read_timeout=2)
            with c.cursor() as cur:
                cur.execute('select role from __all_virtual_server_stat'); rows=cur.fetchall()
            c.close()
            if rows==(('STANDBY',),):
                print('GREEN standby SQL listener ready',proc.pid); sys.exit(0)
        except Exception: pass
        time.sleep(.2)
    print('RED standby SQL readiness timeout',proc.pid); sys.exit(1)
