#!/usr/bin/env python3
"""Enable/disable every local hook required by run_four_gates.py.

Enable, build and copy the native binary, then disable before the production
build. Hook code must be absent from production sources; probe files are
archived on this branch with the validation evidence.
"""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
local = Path(__file__).resolve().parent
scripts = ('native_probe_injection.py', 'shared_transaction_fault_injection.py',
           'preparation_probe_injection.py', 'creation_identity_probe_injection.py',
           'ddl_catalog_fault_injection.py', 'cold_materialization_probe_injection.py',
           'catalog_gc_probe_injection.py', 'weak_source_gc_probe_injection.py',
           'empty_shell_horizon_injection.py', 'physical_retention_cut_injection.py',
           'physical_gc_plan_injection.py', 'template_baseline_injection.py',
           'standby_copy_pause_injection.py', 'baseline_progress_injection.py',
           'catalog_gc_boundary_injection.py', 'grpc_stop_injection.py', 'ddl_publication_cost_injection.py')
try:
    for script in scripts:
        subprocess.run([sys.executable, str(local / script), args.action], check=True)
except subprocess.CalledProcessError:
    if args.action == 'enable':
        for script in scripts:
            subprocess.run([sys.executable, str(local / script), 'disable'], check=False)
    raise
