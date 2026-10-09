#!/usr/bin/env python3
"""Local test build hook. Disable before building/pushing production code."""
import argparse
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
source = root / 'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
freeze_header = root / 'src/storage/compaction/ob_freeze_info_mgr.h'
probe = Path(__file__).resolve().with_name('instance_meta_native_probe.ipp')
record_probe = Path(__file__).resolve().with_name('instance_namespace_metadata_probe.ipp')
durable_probe = Path(__file__).resolve().with_name('instance_namespace_durable_probe.ipp')
pin_probe = Path(__file__).resolve().with_name('physical_snapshot_retention_native_probe.ipp')
ddl_probe = Path(__file__).resolve().with_name('ddl_schema_fence_probe.ipp')
shared_probe = Path(__file__).resolve().with_name('shared_transaction_native_probe.ipp')
binding_probe = Path(__file__).resolve().with_name('tablet_binding_native_probe.ipp')
fixture_probe = Path(__file__).resolve().with_name('fixture_catalog_bootstrap.ipp')
view_probe = Path(__file__).resolve().with_name('catalog_read_view_probe.ipp')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
text = source.read_text()
text = re.sub(r'^[ \t]*// LOCAL_INSTANCE_META_PROBE_BEGIN\n.*?^[ \t]*// LOCAL_INSTANCE_META_PROBE_END\n', '', text, flags=re.M | re.S)
header_text = freeze_header.read_text()
header_text = re.sub(r'^[ \t]*// LOCAL_INSTANCE_META_PROBE_BEGIN\n.*?^[ \t]*// LOCAL_INSTANCE_META_PROBE_END\n', '', header_text, flags=re.M | re.S)
if args.action == 'enable':
    include_anchor = '#include "namespace/catalog.h"\n'
    entry_anchor = 'int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n'
    if text.count(include_anchor) != 1 or text.count(entry_anchor) != 1:
        raise SystemExit('Probe entry moved; update the local hook before building tests.')
    text = text.replace(include_anchor, include_anchor + '// LOCAL_INSTANCE_META_PROBE_BEGIN\n' + '#include <thread>\n'
        '#include "' + str(Path(__file__).resolve().with_name('catalog_gc_test_hook.h')) + '"\n'
        '#include "storage/instance_meta/instance_meta_store.h"\n'
        '#include "storage/tx/ob_trans_service.h"\n'
        '#include "rootserver/fork_table/namespace_tablet_access.h"\n'
        '#include "rootserver/fork_table/instance_namespace_metadata.h"\n'
        '#include "storage/compaction/ob_freeze_info_mgr.h"\n'
        '#include "storage/tx_storage/ob_access_service.h"\n'
        '#include "rootserver/ob_ddl_service.h"\n'
        '#include "share/tablet/ob_tablet_mapping_operator.h"\n'
        '#include "common/mysqlclient/ob_mysql_result.h"\n'
        '// LOCAL_INSTANCE_META_PROBE_END\n', 1)
    hook = ('// LOCAL_INSTANCE_META_PROBE_BEGIN\n#include "' + str(probe) + '"\n'
        '#include "' + str(record_probe) + '"\n'
        '#include "' + str(durable_probe) + '"\n'
        '#include "' + str(pin_probe) + '"\n'
        '#include "' + str(ddl_probe) + '"\n'
        '#include "' + str(shared_probe) + '"\n'
        '#include "' + str(binding_probe) + '"\n'
        '#include "' + str(fixture_probe) + '"\n'
        '#include "' + str(view_probe) + '"\n'
        '// LOCAL_INSTANCE_META_PROBE_END\n' + entry_anchor +
        '  // LOCAL_INSTANCE_META_PROBE_BEGIN\n'
        '  if (getenv("SEEKDB_INSTANCE_META_PROBE") != nullptr) {\n'
        '    const int binding_ret = run_tablet_binding_native_probe();\n'
        '    if (binding_ret != OB_SUCCESS) { return binding_ret; }\n'
        '    const int ddl_ret = run_ddl_schema_fence_probe();\n'
        '    if (ddl_ret != OB_SUCCESS) { return ddl_ret; }\n'
        '    const int record_ret = run_instance_namespace_metadata_probe();\n'
        '    if (record_ret != OB_SUCCESS) { return record_ret; }\n'
        '    const int fixture_ret = initialize_fixture_catalog();\n'
        '    if (fixture_ret != OB_SUCCESS) { return fixture_ret; }\n'
        '    const int view_ret = run_catalog_read_view_probe();\n'
        '    if (view_ret != OB_SUCCESS) { return view_ret; }\n'
        '    const int durable_ret = run_instance_namespace_durable_probe();\n'
        '    if (durable_ret != OB_SUCCESS) { return durable_ret; }\n'
        '    const int probe_ret = run_instance_meta_native_probe();\n'
        '    if (probe_ret != OB_SUCCESS) { return probe_ret; }\n'
        '    const int shared_ret = run_shared_transaction_native_probe();\n'
        '    if (shared_ret != OB_SUCCESS) { return shared_ret; }\n'
        '    const int pin_ret = run_physical_snapshot_retention_native_probe();\n'
        '    if (pin_ret != OB_SUCCESS) { return pin_ret; }\n'
        '    initial_install = false; // Synthetic native fixture already owns its directory.\n'
        '  }\n  // LOCAL_INSTANCE_META_PROBE_END\n')
    text = text.replace(entry_anchor, hook, 1)
if text != source.read_text():
    source.write_text(text)
if header_text != freeze_header.read_text():
    freeze_header.write_text(header_text)
print('Native probe hook ' + args.action + 'd: ' + str(source))
