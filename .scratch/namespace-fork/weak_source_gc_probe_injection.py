#!/usr/bin/env python3
"""Deterministic local readable horizon, never committed as production code."""
from pathlib import Path
import argparse
import re

p = argparse.ArgumentParser()
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
local = Path(__file__).resolve().parent
kernel = root/'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
store = root/'src/storage/instance_meta/instance_meta_store.cpp'
def strip(text):
    return re.sub(r'^[ \t]*// LOCAL_WEAK_SOURCE_GC_BEGIN\n.*?^[ \t]*// LOCAL_WEAK_SOURCE_GC_END\n', '', text, flags=re.M|re.S)
def block(text):
    return '// LOCAL_WEAK_SOURCE_GC_BEGIN\n'+text+'// LOCAL_WEAK_SOURCE_GC_END\n'
text = strip(kernel.read_text())
st = strip(store.read_text())
if a.action == 'enable':
    anchor = 'int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n'
    assert text.count(anchor) == 1
    text = text.replace(anchor, block('#include "'+str(local/'weak_source_gc_probe.ipp')+'"\n')+anchor+block(
        '  if (getenv("SEEKDB_WEAK_SOURCE_GC_PROBE")) {\n'
        '    const int rc = run_weak_source_gc_probe();\n'
        '    if (rc != OB_SUCCESS) { return rc; }\n'
        '  }\n'))
    anchor = 'int InstanceMetaStore::begin(Transaction &tx, const int64_t deadline, const bool read_only)\n'
    assert st.count(anchor) == 1
    st = st.replace(anchor, block('thread_local int64_t weak_source_gc_probe_snapshot = 0;\n')+anchor)
    anchor = '    return transactions_.get_weak_read_snapshot_version(-1, snapshot);\n'
    if anchor in st:
        assert st.count(anchor) == 1
        st = st.replace(anchor, block('    if (weak_source_gc_probe_snapshot > 0) { return snapshot.convert_for_tx(weak_source_gc_probe_snapshot); }\n')+anchor)
for path, data in ((kernel, text), (store, st)):
    if path.read_text() != data: path.write_text(data)
print('weak source GC hook', a.action)
