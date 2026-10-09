#!/usr/bin/env python3
from pathlib import Path
import argparse
import re
p=argparse.ArgumentParser()
p.add_argument('action',choices=('enable','disable'))
a=p.parse_args()
local=Path(__file__).resolve().parent
target=local.parents[1]/'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
text=re.sub(r'^[ \t]*// LOCAL_RETENTION_CUT_BEGIN\n.*?^[ \t]*// LOCAL_RETENTION_CUT_END\n','',target.read_text(),flags=re.M|re.S)
def block(code):return '// LOCAL_RETENTION_CUT_BEGIN\n'+code+'\n// LOCAL_RETENTION_CUT_END\n'
if a.action=='enable':
    helper=(local/'shared_transaction_fault_probe.ipp').read_text().replace('shared_transaction_fault','retention_cut_fault').replace('SEEKDB_SHARED_TX_CONTROL','SEEKDB_RETENTION_CUT_CONTROL')
    anchor='int NamespaceForkKernelPrototype::load_physical_retention(PhysicalSnapshotRetention &plan)\n'
    assert text.count(anchor)==1
    text=text.replace(anchor,block(helper)+anchor)
    anchor='  fence.release_publication();\n  rootserver::InstanceNamespaceMetadata metadata(*store, tx);\n'
    assert text.count(anchor)==1
    text=text.replace(anchor,'  fence.release_publication();\n'+block('  if (ret == OB_SUCCESS) { ret = retention_cut_fault("after_cut", 0); }')+'  rootserver::InstanceNamespaceMetadata metadata(*store, tx);\n')
    anchor='  if (source == "__gc__" && target == "__gc__") {\n'
    assert text.count(anchor)==1
    code='''  if (source == "ns1" && target == "retention_probe") {
    PhysicalSnapshotRetention plan;
    int rc = load_physical_retention(plan);
    const char *path = getenv("SEEKDB_RETENTION_PLAN_OUTPUT");
    if (rc == OB_SUCCESS && path != nullptr) {
      FILE *out = fopen(path, "w");
      if (!out) { return OB_IO_ERROR; }
      fprintf(out, "{\\"read_snapshot\\":%ld,\\"new_source_floor\\":%ld,\\"tablets\\":[", plan.read_snapshot, plan.new_source_floor);
      bool first = true;
      for (const auto &entry : plan.tablets) {
        fprintf(out, "%s[%lu,%ld,%ld]", first ? "" : ",", entry.first, entry.second.create_transaction_id, entry.second.snapshot);
        first = false;
      }
      fprintf(out, "]}\\n");
      fclose(out);
    }
    id = OB_INVALID_ID;
    return rc;
  }
'''
    text=text.replace(anchor,block(code)+anchor)
if text!=target.read_text():target.write_text(text)
print('physical retention cut hook',a.action)
