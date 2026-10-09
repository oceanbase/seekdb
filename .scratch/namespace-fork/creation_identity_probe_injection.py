#!/usr/bin/env python3
"""Local incarnation checks, never included in production commits."""
import argparse
from pathlib import Path
import re
parser=argparse.ArgumentParser()
parser.add_argument('action', choices=('enable','disable'))
a=parser.parse_args()
root=Path(__file__).resolve().parents[2]
p=root/'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
s=re.sub(r'^[ \t]*// LOCAL_CREATION_IDENTITY_BEGIN\n.*?^[ \t]*// LOCAL_CREATION_IDENTITY_END\n','',p.read_text(),flags=re.M|re.S)
if a.action=='enable':
    def hook(anchor,code,before=False):
        global s
        assert s.count(anchor)==1,anchor
        block='// LOCAL_CREATION_IDENTITY_BEGIN\n'+code+'\n// LOCAL_CREATION_IDENTITY_END\n'
        s=s.replace(anchor,block+anchor if before else anchor+block,1)
    hook('int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n',
         '#include "'+str(Path(__file__).with_name('creation_identity_native_probe.ipp').resolve())+'"',True)
    hook('int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n',
         '  const int identity_ret = verify_creation_identities();\n  if (identity_ret != OB_SUCCESS) { return identity_ret; }')
    hook('  if (OB_SUCC(ret) && !already) {\n    failure_stage = "sources";\n',
         '''  if (ret == OB_SUCCESS && !already) {
    ret = query::ObInnerSQLConnectionAccess::with_native_transaction(
        trans.get_connection(), [&](transaction::ObTxDesc &native) -> int {
      for (const auto &item : items) {
        const int rc = record_creation_identity(directory_tx,
            ObTabletID(encoded(db, item.local_tablet)), native.get_tx_id().get_id());
        if (rc != OB_SUCCESS) { return rc; }
      }
      return OB_SUCCESS;
    });
  }''',True)
p.write_text(s)
print('Creation identity probes',a.action)
