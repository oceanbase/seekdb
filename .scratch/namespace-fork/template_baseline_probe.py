#!/usr/bin/env python3
"""Template is nonlogin, owns all sources before admission, and survives restart."""
import argparse, resource, time
from pathlib import Path
import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata, physical_id
from ddl_catalog_atomic_probe import graph

def run(binary):
    e=BootstrapExperiment(binary,'template_baseline',prototype=6)
    try:
        start=time.monotonic();e.start()
        e.record('startup',seconds=round(time.monotonic()-start,2))
        def verify():
            records=metadata(e,1)
            matches=[(int(k['namespace_id']),v) for k,v in records if v['name']=='__template__']
            assert len(matches)==1,matches
            template,state=matches[0]
            assert state['allow_login']==0,state
            sources=graph(e,state)
            assert sources
            assert all(source[0][1]==physical_id(template,logical) and source[1]==0
                       for logical,source in sources.items()),'template still inherits source bindings'
            rows=e.sql('SELECT tablet_id FROM oceanbase.__all_virtual_tablet_info',log=False)
            physical={int(r[0]) for r in rows}
            assert all(physical_id(template,logical) in physical for logical in sources)
            try:
                c=connect(e,'root@__template__');c.close()
            except pymysql.MySQLError as error:assert error.args[0]==4179,error.args
            else:raise AssertionError('template allowed login')
            trace='\n'.join(p.read_text(errors='replace') for p in list((e.base/'log').glob('seekdb.log*'))+[e.base/'process.out'] if p.is_file())
            assert f'PROTOTYPE_INPROCESS_NS_ACTIVATE ns={template} ' not in trace
            e.record('template_verified',namespace=template,tablets=len(sources),nonlogin=True,runtime_unloaded=True)
        verify()
        e.sql('CREATE DATABASE template_must_not_include')
        e.sql('CREATE TABLE template_must_not_include.t(id INT PRIMARY KEY)')
        e.sql('CREATE NAMESPACE from_template')
        with connect(e,'root@from_template') as c:
            assert not e.sql("SHOW DATABASES LIKE 'template_must_not_include'",c)
            e.sql('CREATE TABLE test.t(id INT PRIMARY KEY)',c)
            e.sql('INSERT INTO test.t VALUES(1)',c)
            assert e.sql('SELECT * FROM test.t',c)==((1,),)
        e.connection.close();e.connection=None;e.proc.kill();e.proc.wait(timeout=15)
        e.start();verify()
        e.sql('CREATE NAMESPACE after_restart')
        with connect(e,'root@after_restart') as c:
            assert not e.sql("SHOW DATABASES LIKE 'template_must_not_include'",c)
            assert e.sql('SELECT COUNT(*) FROM oceanbase.__all_table',c)[0][0]>0
        e.record('PASS',case='template_initial_baseline',all_bindings_owned=True,nonlogin=True,
                 sql_runtime_unloaded=True,empty_namespace=True,sigkill_restart=True)
    finally:e.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',required=True);a=p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0));run(a.binary)
