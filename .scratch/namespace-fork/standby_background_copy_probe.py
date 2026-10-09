#!/usr/bin/env python3
"""Fresh standby full copy while background-created tablets have flushed MDS."""
import argparse,json,os,re,resource,signal,socket,subprocess,time
from pathlib import Path
import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, connect, physical_id, physical_state


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); return s.getsockname()[1]


def run(binary,drop_parent=False,compaction_interval='5m',drop_during_copy=False,cancel_copy=False,shutdown_primary=False):
    primary=BootstrapExperiment(binary,'background_copy_primary',prototype=6)
    standby=BootstrapExperiment(binary,'background_copy_standby',prototype=6)
    rpc=free_port()
    primary.extra_parameters=[('enable_rpc_service','true'),('rpc_port',str(rpc)),('ob_compaction_schedule_interval',compaction_interval)]
    try:
        previous_stop = os.environ.get('SEEKDB_TEST_GRPC_STOP')
        if shutdown_primary: os.environ['SEEKDB_TEST_GRPC_STOP'] = '1'
        try:
            primary.start()
        finally:
            if previous_stop is None: os.environ.pop('SEEKDB_TEST_GRPC_STOP', None)
            else: os.environ['SEEKDB_TEST_GRPC_STOP'] = previous_stop
        primary.sql('CREATE DATABASE copy_probe')
        primary.sql('CREATE TABLE copy_probe.t(id INT PRIMARY KEY,v INT)')
        primary.sql('INSERT INTO copy_probe.t VALUES(1,7)')
        primary.sql('FORK NAMESPACE copy_child FROM ns1')
        target = primary.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE "
            "database_id=(SELECT database_id FROM oceanbase.__all_database WHERE database_name='copy_probe') "
            "AND table_name='t'", log=False)[0][0]
        physical = physical_id(1, int(target))
        control = standby.base/'copy-pause'
        environment = os.environ.copy()
        if drop_during_copy or cancel_copy or shutdown_primary:
            control.write_text(str(physical))
            environment['SEEKDB_STANDBY_COPY_PAUSE'] = str(control)
            primary.sql('ALTER SYSTEM MINOR FREEZE')
        if drop_parent:
            primary.sql('DROP TABLE copy_probe.t')
            primary.sql('ALTER SYSTEM MINOR FREEZE')
        # Background materialization + native checkpoint flush creates nonempty
        # MDS SSTables even while these data tablets have no user SSTables.
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            log=(primary.base/'log/seekdb.log').read_text(errors='replace')
            mds = re.findall(r'sstable merge finish\(ret=0,.*?tablet_id:\{id:(\d+)\}, merge_type:"MDS_MINI_MERGE"', log)
            if any(((int(tablet) >> 37) & ((1 << 25)-1)) == 2 for tablet in mds):
                primary.record('template_mds_flushed', tablets=mds[-5:])
                break
            time.sleep(.5)
        else: raise AssertionError('source did not exercise background materialization + MDS flush')
        command=[str(Path(binary).resolve()),'--nodaemon','--base-dir='+str(standby.base),'-P'+str(standby.port),
                 '--role=STANDBY','--parameter',f'log_restore_source=127.0.0.1:{rpc}',
                 '--parameter','enable_rpc_service=true','--parameter',f'rpc_port={free_port()}',
                 '--parameter','memory_budget=2G','--parameter','cpu_count=4',
                 '--parameter','datafile_size=2G','--parameter','datafile_maxsize=4G',
                 '--parameter','log_disk_size=2G','--parameter','max_syslog_file_count=16']
        standby.proc=subprocess.Popen(command,env=environment,stdout=standby.output,stderr=subprocess.STDOUT)
        standby.record('setup',base=standby.base,pid=standby.proc.pid,port=standby.port,primary_base=primary.base)
        if drop_during_copy or cancel_copy or shutdown_primary:
            deadline=time.monotonic()+60
            while time.monotonic()<deadline and not Path(str(control)+'.ready').exists():
                if standby.proc.poll() is not None: raise AssertionError('copy exited before controlled pause')
                time.sleep(.2)
            assert Path(str(control)+'.ready').exists(), 'copy pause was not reached (requires local test binary)'
            standby.record('paused_after_sstable_metadata', physical=physical)
            if shutdown_primary:
                started = time.monotonic()
                primary.proc.send_signal(signal.SIGUSR1)
                try:
                    primary.proc.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    raise AssertionError('gRPC service stop hung while peer retained the copy view')
                assert primary.proc.returncode == 0, ('unclean shutdown', primary.proc.returncode)
                log = '\n'.join(path.read_text(errors='replace') for path in
                                (primary.base/'log').glob('seekdb.log*') if path.is_file())
                assert 'physical copy view released' in log, 'service stop did not release the view'
                assert 'gRPC server stopped' in log, 'service stop was not exercised (requires test hook)'
                assert standby.proc.poll() is None, 'peer exited before primary shutdown completed'
                primary.record('PASS', case='copy_view_primary_shutdown', seconds=time.monotonic()-started,
                               source_view_released=True, peer_alive=True)
                return
            if cancel_copy:
                standby.proc.kill(); standby.proc.wait(timeout=15)
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    log='\n'.join(path.read_text(errors='replace') for path in
                                  (primary.base/'log').glob('seekdb.log*') if path.is_file())
                    if 'physical copy view released' in log: break
                    time.sleep(.2)
                else: raise AssertionError('source copy view was not released after peer SIGKILL')
                assert primary.sql('SELECT * FROM copy_probe.t') == ((1,7),)
                primary.record('PASS',case='copy_view_peer_loss',source_view_released=True)
                return
            with connect(primary, 'root@copy_child') as child:
                primary.sql('UPDATE copy_probe.t SET v=7',child)
            primary.sql('DROP TABLE copy_probe.t')
            primary.sql('ALTER SYSTEM MINOR FREEZE')
            deadline=time.monotonic()+180
            last_report=0
            while time.monotonic()<deadline:
                state=physical_state(primary,[physical])
                if not state or all(row[3] for row in state): break
                if time.monotonic()-last_report>15:
                    primary.record('waiting_source_reclaimed', physical=state)
                    last_report=time.monotonic()
                time.sleep(.5)
            else: raise AssertionError(('source was not reclaimed while copy paused',state))
            primary.record('source_reclaimed_while_copy_paused',physical=physical,state=state)
            control.unlink()
        deadline=time.monotonic()+90
        last_report=0; last_status=None
        while time.monotonic()<deadline:
            if standby.proc.poll() is not None:
                raise AssertionError(('standby full copy failed',standby.proc.returncode,str(standby.base)))
            try:
                standby.connection=pymysql.connect(host='127.0.0.1',port=standby.port,user='root@copy_child' if drop_parent or drop_during_copy else 'root',autocommit=True,connect_timeout=2,read_timeout=5)
                rows=standby.sql('SELECT * FROM copy_probe.t',log=False)
                last_status=rows
                if rows == ((1,7),): break
            except pymysql.MySQLError as error:
                last_status=error.args
                if standby.connection is not None: standby.connection.close(); standby.connection=None
            if time.monotonic()-last_report>10:
                standby.record('waiting_readable', status=last_status);last_report=time.monotonic()
            time.sleep(.5)
        else: raise AssertionError(('standby full copy never became readable',last_status))
        with pymysql.connect(host='127.0.0.1',port=standby.port,user='root@copy_child',autocommit=True,connect_timeout=2,read_timeout=15) as child:
            with child.cursor() as cursor:
                cursor.execute('SELECT * FROM copy_probe.t')
                assert cursor.fetchall() == ((1,7),)
        standby.record('PASS',case='background_mds_fullcopy',source_mds_flushed=True,rows=rows,child_read=True,parent_dropped_before_copy=drop_parent,parent_dropped_during_copy=drop_during_copy,compaction_interval=compaction_interval)
    finally:
        standby.close();primary.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',required=True)
    parser.add_argument('--drop-parent',action='store_true')
    parser.add_argument('--drop-during-copy',action='store_true')
    parser.add_argument('--cancel-copy',action='store_true')
    parser.add_argument('--shutdown-primary',action='store_true')
    parser.add_argument('--compaction-interval',default='5m')
    args=parser.parse_args()
    if sum((args.drop_parent,args.drop_during_copy,args.cancel_copy,args.shutdown_primary))>1: parser.error('select one controlled boundary')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0));run(args.binary,args.drop_parent,args.compaction_interval,args.drop_during_copy,args.cancel_copy,args.shutdown_primary)
