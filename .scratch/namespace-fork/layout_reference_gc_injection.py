#!/usr/bin/env python3
"""Disposable controls for retaining real tablet handles across SQL DROP and GC."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
local = Path(__file__).resolve().parent
root = local.parents[1]
changes = {
    'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp': [
        ('#include "namespace/catalog.h"\n',
         '#include "storage/instance_meta/storage_schema_history.h"\n'
         '#include "storage/blocksstable/ob_sstable_meta.h"\n'
         '#include "storage/tablet/ob_tablet_table_store.h"\n'
         '#include "storage/tx/ob_trans_service.h"\n', False),
        ('int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n',
         '#include "' + str(local / 'layout_reference_gc_native_probe.ipp') + '"\n', True),
    ],
    'src/rootserver/fork_table/namespace_maintenance.cpp': [
        ('namespace oceanbase {\n',
         'namespace oceanbase { namespace storage { void run_layout_reference_gc_command(); } }\n', True),
        ('  if (!ATOMIC_LOAD(&GCTX.sys_package_ready_) || !share::server_is_write_enabled()) { return; }\n',
         '  if (ATOMIC_LOAD(&GCTX.sys_package_ready_)) { storage::run_layout_reference_gc_command(); }\n', True),
    ],
}
for name, hooks in changes.items():
    path = root / name
    text = re.sub(r'^// LOCAL_LAYOUT_REFERENCE_GC_BEGIN\n.*?^// LOCAL_LAYOUT_REFERENCE_GC_END\n',
                  '', path.read_text(), flags=re.M | re.S)
    if a.action == 'enable':
        for anchor, code, before in hooks:
            assert text.count(anchor) == 1, (name, anchor)
            block = '// LOCAL_LAYOUT_REFERENCE_GC_BEGIN\n' + code + '// LOCAL_LAYOUT_REFERENCE_GC_END\n'
            text = text.replace(anchor, block + anchor if before else anchor + block)
    path.write_text(text)
print('Layout reference GC hooks', a.action)
