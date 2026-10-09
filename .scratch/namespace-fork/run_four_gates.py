#!/usr/bin/env python3
"""Local four-gate entry; native KV regressions are part of the bootstrap gate."""
import argparse
from datetime import datetime
from pathlib import Path
import subprocess
import sys

local = Path(__file__).resolve().parent
tools = local.parent / 'excluded-from-branch/tools/obtest'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', required=True, help='production binary without local test hooks')
parser.add_argument('--native-probe-binary', required=True, help='same source revision, built with gate_probe_injection.py enable (native, shared transaction, preparation, identity, DDL commit and cold materialization hooks)')
parser.add_argument('--log-dir', type=Path, default=local / 'gate-results' / datetime.now().strftime('%Y%m%d-%H%M%S'))
parser.add_argument('--gate', action='append', choices=('bootstrap-native-kv', 'bootstrap', 'sql', 'direct', 'tls'), help='rerun selected failed/pending steps; default runs all')
parser.add_argument('--start-at-script', help='resume one gate at the first command with this script filename; keep prior logs as evidence')
args = parser.parse_args()
if args.start_at_script and (not args.gate or len(args.gate) != 1):
    parser.error('--start-at-script requires exactly one --gate')
binary = str(Path(args.binary).resolve())
probe = str(Path(args.native_probe_binary).resolve())
args.log_dir.mkdir(parents=True, exist_ok=True)
gates = [
    ('bootstrap-native-kv', [
        [local / 'run_storage_schema_history_probe.py', '--binary', probe],
        [local / 'run_layout_retention_probe.py', '--binary', probe],
        [local / 'run_table_storage_layout_probe.py', '--binary', probe],
        [local / 'ddl_catalog_atomic_probe.py', '--binary', probe, '--owner', 'initial',
         '--fault', 'abort_crash', '--layout-history'],
        [local / 'ddl_catalog_atomic_probe.py', '--binary', probe, '--owner', 'child',
         '--fault', 'commit_crash', '--layout-history'],
        [local / 'catalog_batch_probe.py'],
        [local / 'ddl_publication_cost_probe.py', '--binary', probe],
        [local / 'standby_baseline_progress_probe.py', '--binary', probe],
        [local / 'standby_background_copy_probe.py', '--binary', probe,
         '--drop-during-copy', '--compaction-interval', '3s'],
        [local / 'standby_background_copy_probe.py', '--binary', probe, '--cancel-copy'],
        [local / 'standby_background_copy_probe.py', '--binary', probe, '--shutdown-primary'],
        [local / 'run_weak_source_gc_probe.py', '--binary', probe],
        [local / 'empty_shell_horizon_probe.py', '--binary', probe],
        [local / 'physical_retention_cut_probe.py', '--binary', probe],
        [local / 'physical_gc_plan_probe.py', '--binary', probe],
        [local / 'run_instance_meta_native_probe.py', '--binary', probe],
        [local / 'cold_materialization_probe.py', '--binary', probe],
        [local / 'run_standby_suite.py', '--binary', probe, '--case', 'catalog_read_view',
         '--log-dir', args.log_dir / 'catalog-read-view-standby'],
        [local / 'tablet_preparation_probe.py', '--binary', probe, '--instrumented'],
        [local / 'shared_transaction_probe.py', '--binary', probe, '--faults'],
        [local / 'shared_transaction_probe.py', '--binary', probe, '--faults',
         '--creation-identities', '--case', 'rollback_create', '--case', 'crash_committed'],
        [local / 'shared_transaction_probe.py', '--binary', probe, '--faults',
         '--case', 'rollback_create', '--case', 'rollback_owned',
         '--case', 'crash_create', '--case', 'crash_owned', '--flush-redo', '--creation-identities'],
        *[[local / 'ddl_catalog_atomic_probe.py', '--binary', probe, '--owner', owner,
           '--fault', fault] for owner in ('initial', 'child')
          for fault in ('rollback', 'abort_crash', 'commit_crash')],
    ]),
    ('bootstrap', [
        [local / 'quick_startup_probe.py', '--binary', binary, '--fork', '--write'],
        [tools / 'namespace_worker_bootstrap_prototype.py', '--binary', binary],
    ]),
    ('sql', [[tools / 'namespace_sql_worker_prototype.py', '--binary', binary, '--case', 'full']]),
    ('direct', [
        [local / 'native_scan_probe.py', '--binary', binary],
        [local / 'large_partition_fork_probe.py', '--binary', binary],
        [local / 'fork_service_latency_probe.py', '--binary', binary,
         '--result', args.log_dir / 'fork-service-latency-results.json'],
        [local / 'fullscan_diagnosis_probe.py', '--binary', binary, '--scaling'],
        [local / 'automatic_catalog_gc_probe.py', '--binary', binary],
        [local / 'sql_read_view_probe.py', '--binary', binary],
        [local / 'physical_retention_mvcc_probe.py', '--binary', binary],
        [local / 'source_roots_only_probe.py', '--binary', binary],
        [local / 'template_baseline_probe.py', '--binary', binary],
        [local / 'template_initial_failure_probe.py', '--binary', probe],
        [local / 'background_materialization_probe.py', '--binary', binary],
        [local / 'background_materialization_probe.py', '--binary', binary, '--index-families'],
        [local / 'standby_background_copy_probe.py', '--binary', binary],
        [local / 'standby_background_copy_probe.py', '--binary', binary, '--drop-parent'],
        [local / 'standby_background_copy_probe.py', '--binary', binary, '--compaction-interval', '3s'],
        [local / 'sql_read_view_routine_probe.py', '--binary', binary],
        [local / 'ddl_catalog_schema_probe.py', '--binary', binary],
        [local / 'primary_major_namespace_probe.py', '--binary', binary],
        [local / 'run_standby_suite.py', '--binary', binary],
        [local / 'tablet_preparation_probe.py', '--binary', binary],
        [local / 'tablet_access_isolation_probe.py', '--binary', binary],
        [local / 'tablet_access_concurrency_probe.py', '--binary', binary],
        [local / 'tablet_access_reads_probe.py', '--binary', binary],
        [local / 'tablet_access_index_probe.py', '--binary', binary],
        [local / 'ivf_inherited_probe.py', binary],
        [local / 'ivf_inherited_probe.py', binary, 'pq'],
        [local / 'truncate_global_inherited_probe.py', binary],
        [local / 'fts_heap_redefinition_probe.py', binary],
        [local / 'fts_column_redefinition_probe.py', '--binary', binary],
        [tools / 'namespace_fork_table_prototype.py', '--binary', binary],
        [local / 'shared_transaction_probe.py', '--binary', binary],
        [local / 'quick_startup_probe.py', '--binary', binary,
         '--fork', '--write', '--drop', '--gc'],
        [local / 'exchange_mapping_probe.py', '--binary', binary],
        [local / 'ddl_physical_drop_probe.py', '--binary', binary],
        [local / 'fork_parent_truncate_probe.py', '--binary', binary],
        [local / 'fork_parent_truncate_probe.py', '--binary', binary,
         '--prime-parent-schema', '--materialize-child'],
        [local / 'fork_parent_truncate_probe.py', '--binary', binary,
         '--prime-parent-schema'],
        [local / 'fork_parent_truncate_probe.py', '--binary', binary,
         '--prime-parent-schema', '--materialize-parent'],
        [local / 'fork_parent_truncate_probe.py', '--binary', binary,
         '--materialize-parent', '--write-child'],
        [local / 'fork_parent_truncate_probe.py', '--binary', binary,
         '--materialize-parent-after-fork', '--write-child'],
        [local / 'fork_history_gc_probe.py', '--binary', binary],
        [local / 'fork_history_gc_probe.py', '--binary', binary, '--complete-baseline'],
        [local / 'fork_detached_restart_probe.py', '--binary', binary],
        [local / 'fork_detached_restart_probe.py', '--binary', binary, '--mixed-source'],
        [local / 'retired_baseline_restart_probe.py', '--binary', binary],
        [local / 'fork_history_concurrency_probe.py', '--binary', binary],
        [tools / 'namespace_worker_direct_prototype.py', '--binary', binary, '--case', 'full'],
    ]),
    ('tls', [[tools / 'namespace_worker_direct_prototype.py', '--binary', binary, '--case', 'tls']]),
]
for name, commands in gates:
    if args.gate and name not in args.gate:
        continue
    if args.start_at_script:
        matches = [i for i, command in enumerate(commands) if command[0].name == args.start_at_script]
        if not matches:
            parser.error('script not found in selected gate: ' + args.start_at_script)
        commands = commands[matches[0]:]
        print('RESUME ' + name + ' at ' + args.start_at_script, flush=True)
    log = args.log_dir / (name + '.log')
    print('RUN ' + name + ' ' + str(log), flush=True)
    with log.open('w') as output:
        for command in commands:
            result = subprocess.run([sys.executable] + [str(arg) for arg in command],
                                    stdout=output, stderr=subprocess.STDOUT)
            if result.returncode:
                raise SystemExit('FAIL ' + name + ': ' + str(log))
    print('PASS ' + name, flush=True)
print('PASS selected gates' if args.gate else 'PASS four gates including native KV bootstrap regressions', flush=True)
