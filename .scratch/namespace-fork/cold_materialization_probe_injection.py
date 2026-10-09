#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
parser = argparse.ArgumentParser()
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
local = Path(__file__).resolve().parent
p = local.parents[1] / 'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
s = re.sub(r'^[ \t]*// LOCAL_COLD_MATERIALIZATION_BEGIN\n.*?^[ \t]*// LOCAL_COLD_MATERIALIZATION_END\n',
           '', p.read_text(), flags=re.M | re.S)
if args.action == 'enable':
    def before(anchor, code):
        global s
        assert s.count(anchor) == 1, anchor
        s = s.replace(anchor, '// LOCAL_COLD_MATERIALIZATION_BEGIN\n' + code +
                      '\n// LOCAL_COLD_MATERIALIZATION_END\n' + anchor, 1)
    before('#include "namespace/catalog.h"\n', '#include "rootserver/fork_table/namespace_tablet_access.h"')
    before('int NamespaceForkKernelPrototype::control_namespace(const ObString &source, const ObString &target,\n    uint64_t &id, bool allow_login) {\n',
           '#include "' + str(local / 'cold_materialization_probe.ipp') + '"')
    before('  LOG_INFO("PROTOTYPE_NAMESPACE_KV_REGISTER", K(ret), K(id),\n',
           '  if (ret == OB_SUCCESS) { ret = probe_cold_materialization(id, target_name); }')
p.write_text(s)
print('Cold materialization hooks', args.action)
