#!/usr/bin/env python3
"""Local commit-boundary faults for atomic SQL/catalog publication."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
local = Path(__file__).resolve().parent
target = local.parents[1] / 'src/rootserver/ob_ddl_service.cpp'
source = re.sub(r'^[ \t]*// LOCAL_DDL_CATALOG_BEGIN\n.*?^[ \t]*// LOCAL_DDL_CATALOG_END\n',
                '', target.read_text(), flags=re.M | re.S)
if args.action == 'enable':
    probe = (local / 'shared_transaction_fault_probe.ipp').read_text().replace(
        'shared_transaction_fault', 'ddl_catalog_fault').replace(
        'SEEKDB_SHARED_TX_CONTROL', 'SEEKDB_DDL_CATALOG_CONTROL').replace(
        'SHARED_TX_FAULT', 'DDL_CATALOG_FAULT')
    (local / 'ddl_catalog_fault_probe.ipp').write_text(probe)
    def before(anchor, code):
        global source
        assert source.count(anchor) == 1, anchor
        source = source.replace(anchor, '// LOCAL_DDL_CATALOG_BEGIN\n' + code +
            '\n// LOCAL_DDL_CATALOG_END\n' + anchor, 1)
    before('int ObDDLSQLTransaction::end(const bool commit)\n',
           '#include "' + str(local / 'ddl_catalog_fault_probe.ipp') + '"')
    before('  if (OB_SUCCESS != (tmp_ret = common::ObMySQLTransaction::end(commit && OB_SUCC(ret)))) {\n',
           '  if (ret == OB_SUCCESS && commit && publication != nullptr) {\n'
           '    ret = ddl_catalog_fault("before_commit", namespace_id_);\n  }')
    before('  // Clear runtime_ for success or failure\n',
           '  if (commit && OB_SUCC(ret) && publication != nullptr) {\n'
           '    ret = ddl_catalog_fault("after_commit", namespace_id_);\n  }')
target.write_text(source)
print('DDL catalog fault hooks', args.action)
