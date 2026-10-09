#!/usr/bin/env python3
"""Temporary native layout-history integration hook; remove from production builds."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
local = Path(__file__).resolve().parent
source = local.parents[1] / 'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
text = source.read_text()
text = re.sub(r'^[ \t]*// LOCAL_LAYOUT_HISTORY_PROBE_BEGIN\n.*?^[ \t]*// LOCAL_LAYOUT_HISTORY_PROBE_END\n', '', text, flags=re.M | re.S)
if args.action == 'enable':
    anchor = '#include "namespace/catalog.h"\n'
    entry = 'int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n'
    if text.count(anchor) != 1 or text.count(entry) != 1:
        raise SystemExit('Layout probe hook moved; update injection anchors.')
    text = text.replace(anchor, anchor + '// LOCAL_LAYOUT_HISTORY_PROBE_BEGIN\n'
                        '#include "storage/instance_meta/storage_schema_history.h"\n'
                        '#include "storage/ob_storage_schema_util.h"\n'
                        '#include "storage/tx_storage/ob_access_service.h"\n'
                        '#include "share/tablet/ob_tablet_mapping_operator.h"\n'
                        '// LOCAL_LAYOUT_HISTORY_PROBE_END\n')
    text = text.replace(entry, '// LOCAL_LAYOUT_HISTORY_PROBE_BEGIN\n#include "'
                        + str(local / 'storage_schema_history_native_probe.ipp') + '"\n#include "'
                        + str(local / 'local_storage_schema_native_probe.ipp') + '"\n'
                        '// LOCAL_LAYOUT_HISTORY_PROBE_END\n' + entry
                        + '  // LOCAL_LAYOUT_HISTORY_PROBE_BEGIN\n'
                          '  if (getenv("SEEKDB_LOCAL_STORAGE_SCHEMA_PROBE") != nullptr) {\n'
                          '    const int rc = run_local_storage_schema_native_probe();\n'
                          '    if (rc != OB_SUCCESS) { return rc; }\n'
                          '  }\n'
                          '  if (getenv("SEEKDB_LAYOUT_HISTORY_PROBE") != nullptr) {\n'
                          '    const int rc = run_storage_schema_history_native_probe();\n'
                          '    if (rc != OB_SUCCESS) { return rc; }\n'
                          '  }\n'
                          '  // LOCAL_LAYOUT_HISTORY_PROBE_END\n')
source.write_text(text)
print('Layout history probe ' + args.action + 'd')
