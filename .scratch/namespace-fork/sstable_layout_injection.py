#!/usr/bin/env python3
"""Audit persisted layout identities and trace real fork input selection."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
local = Path(__file__).resolve().parent
root = local.parents[1]


def block(text):
    return '// LOCAL_SSTABLE_LAYOUT_BEGIN\n' + text + '// LOCAL_SSTABLE_LAYOUT_END\n'


for relative in ('src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp',
                 'src/storage/ddl/ob_tablet_fork_task.cpp'):
    path = root / relative
    text = re.sub(r'^// LOCAL_SSTABLE_LAYOUT_BEGIN\n.*?^// LOCAL_SSTABLE_LAYOUT_END\n', '',
                  path.read_text(), flags=re.M | re.S)
    if a.action == 'enable':
        if 'kernel_prototype' in relative:
            anchor = '#include "namespace/catalog.h"\n'
            entry = 'int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n'
            assert text.count(anchor) == text.count(entry) == 1
            text = text.replace(anchor, anchor + block(
                '#include "storage/instance_meta/storage_schema_history.h"\n'
                '#include "storage/tablet/ob_tablet_iterator.h"\n'
                '#include "storage/tablet/ob_tablet_create_sstable_param.h"\n'))
            text = text.replace(entry, block('#include "' + str(local / 'sstable_layout_native_probe.ipp') + '"\n')
                                + entry + block('  if (getenv("SEEKDB_SSTABLE_LAYOUT_PROBE")) {\n'
                                    '    const int rc = audit_sstable_layouts();\n'
                                    '    if (rc != OB_SUCCESS) { return rc; }\n  }\n'))
        else:
            anchor = '  if (OB_FAIL(ret) && schema != nullptr) {\n'
            assert text.count(anchor) == 1
            text = text.replace(anchor, block('''  if (getenv("SEEKDB_SSTABLE_LAYOUT_PROBE") && ret == OB_SUCCESS) {
    const auto &basic = meta.get_sstable_meta().get_basic_meta();
    fprintf(stderr, "FORK_INPUT_LAYOUT tablet=%lu G=%lu V=%ld selected=%ld full=%d columns=%ld body_columns=%ld ret=%d\\n",
        sstable.get_key().tablet_id_.id(), basic.storage_layout_id_, basic.schema_version_,
        schema->get_schema_version(), !schema->is_column_info_simplified(), basic.column_cnt_, columns, ret);
    fflush(stderr);
  }
''') + anchor)
    path.write_text(text)
print('SSTable layout hooks', a.action)
