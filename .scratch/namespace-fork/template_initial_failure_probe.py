#!/usr/bin/env python3
"""An incomplete first installation is neither admitted nor repaired on restart."""
import argparse,os,resource,subprocess,time
from pathlib import Path
import pymysql
from fork_parent_truncate_probe import BootstrapExperiment

def run(binary):
    e=BootstrapExperiment(binary,'template_initial_failure',prototype=6)
    env=os.environ.copy();env['SEEKDB_TEMPLATE_FAIL_INITIAL_BASELINE']='1'
    command=[e.binary,'--nodaemon','--base-dir='+str(e.base),'-P'+str(e.port),'--log-level=INFO',
        '--parameter','memory_budget=2G','--parameter','cpu_count=4',
        '--parameter','datafile_size=256M','--parameter','datafile_maxsize=512M',
        '--parameter','log_disk_size=2G','--parameter','max_syslog_file_count=16']
    def trace():
        return '\n'.join(p.read_text(errors='replace') for p in list((e.base/'log').glob('seekdb.log*'))+[e.base/'process.out'] if p.is_file())
    try:
        for attempt in range(2):
            previous_failures=trace().count('PROTOTYPE_NAMESPACE_CONTROL_SCHEMA(ret=-4104)')
            e.proc=subprocess.Popen(command,env=env,stdout=e.output,stderr=subprocess.STDOUT)
            e.record('setup',base=str(e.base),pid=e.proc.pid,attempt=attempt)
            deadline=time.monotonic()+45
            seen_failure=False
            while time.monotonic()<deadline:
                text=trace()
                if text.count('PROTOTYPE_NAMESPACE_CONTROL_SCHEMA(ret=-4104)')>previous_failures:
                    seen_failure=True;break
                if e.proc.poll() is not None:break
                time.sleep(.25)
            assert seen_failure,'incomplete template was not rejected'
            time.sleep(4)
            try:
                c=pymysql.connect(host='127.0.0.1',port=e.port,user='root',autocommit=True,connect_timeout=2,read_timeout=2)
                with c.cursor() as q:q.execute('SELECT 1');q.fetchall()
                c.close()
            except pymysql.MySQLError:pass
            else:raise AssertionError('incomplete installation admitted SQL')
            text=trace()
            assert text.count('TEMPLATE_INITIAL_FAILURE_INJECTED')==1,'initial template construction was retried'
            assert '__template_build__' not in text,'obsolete template repair was entered'
            e.record('failure_rejected',attempt=attempt,no_repair=True,no_admission=True)
            e.proc.kill();e.proc.wait(timeout=15)
        e.record('PASS',case='template_initial_failure_no_repair',restart=True)
    finally:e.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',required=True);a=p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0));run(a.binary)
