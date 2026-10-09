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
    hook('#include "namespace/catalog.h"\n',
         '#include "storage/compaction/physical_merge_candidate.h"')
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
commit_source = root/'src/storage/tablet/ob_tablet_create_delete_mds_user_data.cpp'
commit_text = re.sub(r'^[ \t]*// LOCAL_CREATION_IDENTITY_BEGIN\n.*?^[ \t]*// LOCAL_CREATION_IDENTITY_END\n', '', commit_source.read_text(), flags=re.M|re.S)
if a.action == 'enable':
    anchor = '    LOG_INFO("prototype tablet materialization commit", KPC(this), K(commit_version));\n'
    assert commit_text.count(anchor) == 1
    commit_text = commit_text.replace(anchor, '''// LOCAL_CREATION_IDENTITY_BEGIN
    if (getenv("SEEKDB_CREATION_IDENTITY_PROBE") != nullptr) {
      fprintf(stderr, "CREATION_PHYSICAL_COMMIT transaction=%ld physical=%ld native=%ld\\n",
          create_transaction_id_, physical_create_version_, commit_version.get_val_for_tx());
      fflush(stderr);
    }
// LOCAL_CREATION_IDENTITY_END
''' + anchor)
commit_source.write_text(commit_text)
print('Creation identity probes',a.action)
