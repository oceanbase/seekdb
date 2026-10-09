#!/usr/bin/env python3
from pathlib import Path
import argparse, re
p=argparse.ArgumentParser();p.add_argument('action',choices=['enable','disable']);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
def edit(name, anchor, code):
    path=root/name
    s=re.sub(r'^[ \t]*// LOCAL_PHYSICAL_GC_PLAN_BEGIN\n.*?^[ \t]*// LOCAL_PHYSICAL_GC_PLAN_END\n','',path.read_text(),flags=re.M|re.S)
    if a.action=='enable':
        assert s.count(anchor)==1,(name,anchor)
        s=s.replace(anchor,anchor+'// LOCAL_PHYSICAL_GC_PLAN_BEGIN\n'+code+'// LOCAL_PHYSICAL_GC_PLAN_END\n')
    path.write_text(s)
edit('src/storage/compaction/ob_freeze_info_mgr.cpp','int ObFreezeInfoMgr::try_update_info()\n{\n','''  if (const char *path = getenv("SEEKDB_FREEZE_PLAN_PAUSE")) {
    if (::access(path, F_OK) == 0) { return OB_EAGAIN; }
  }
''')
edit('src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp',
     'int NamespaceForkKernelPrototype::control_namespace(const ObString &source, const ObString &target,\n    uint64_t &id, bool allow_login) {\n',
     '''  if (target == "retention_publish") {
    auto *freeze = share::server_service<ObFreezeInfoMgr>();
    int rc = freeze->reload_for_test();
    std::shared_ptr<const PhysicalSnapshotRetention> plan;
    if (rc == OB_SUCCESS) { rc = freeze->get_physical_retention(plan); }
    if (rc == OB_SUCCESS) {
      const char *path = getenv("SEEKDB_RETENTION_PLAN_OUTPUT");
      FILE *out = path ? fopen(path, "w") : nullptr;
      if (!out) { return OB_IO_ERROR; }
      fprintf(out, "%ld\\n", plan->read_snapshot);
      fclose(out);
    }
    id = OB_INVALID_ID;
    return rc;
  }
''')
