#!/usr/bin/env python3
"""Temporary table-layout binding tests and recovery audit, no production API."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
local = Path(__file__).resolve().parent
source = local.parents[1] / 'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
text = re.sub(r'^[ \t]*// LOCAL_TABLE_LAYOUT_BEGIN\n.*?^[ \t]*// LOCAL_TABLE_LAYOUT_END\n',
              '', source.read_text(), flags=re.M | re.S)
if args.action == 'enable':
    anchor = '#include "namespace/catalog.h"\n'
    entry = 'int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n'
    assert text.count(anchor) == text.count(entry) == 1
    text = text.replace(anchor, anchor + '// LOCAL_TABLE_LAYOUT_BEGIN\n'
                        '#include "rootserver/fork_table/table_storage_layouts.h"\n'
                        '#include "storage/instance_meta/storage_schema_history.h"\n'
                        '#include "storage/tablet/ob_tablet_iterator.h"\n'
                        '#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"\n'
                        '#include "storage/ls/ob_ls_tablet_service.h"\n'
                        '#include <atomic>\n#include <thread>\n'
                        '// LOCAL_TABLE_LAYOUT_END\n')
    text = text.replace(entry, '// LOCAL_TABLE_LAYOUT_BEGIN\n#include "'
                        + str(local / 'external_layout_roots_native_probe.ipp') + '"\n#include "'
                        + str(local / 'table_storage_layout_native_probe.ipp') + '"\n'
                        '// LOCAL_TABLE_LAYOUT_END\n' + entry
                        + '  // LOCAL_TABLE_LAYOUT_BEGIN\n'
                          '  if (getenv("SEEKDB_TABLE_LAYOUT_PROBE") != nullptr) {\n'
                          '    int rc = initial_install ? run_table_storage_layout_native_probe() : OB_SUCCESS;\n'
                          '    if (rc == OB_SUCCESS) { rc = run_external_layout_roots_native_probe(); }\n'
                          '    if (rc == OB_SUCCESS) { rc = audit_table_storage_layouts(); }\n'
                          '    if (rc != OB_SUCCESS) { return rc; }\n'
                          '  }\n'
                          '  // LOCAL_TABLE_LAYOUT_END\n')
source.write_text(text)
print('Table layout probe ' + args.action + 'd')
