#!/usr/bin/env python3
"""Local trigger for the actual page collector from its existing timer thread."""
from pathlib import Path
import argparse,re
p=argparse.ArgumentParser();p.add_argument('action',choices=['enable','disable']);a=p.parse_args()
source=Path(__file__).resolve().parents[2]/'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
s=source.read_text();s=re.sub(r'^[ \t]*// CATALOG_GC_PROBE_BEGIN\n.*?^[ \t]*// CATALOG_GC_PROBE_END\n','',s,flags=re.M|re.S)
if a.action=='enable':
 anchor='  if (store == nullptr || !ATOMIC_LOAD(&GCTX.sys_package_ready_)) { return OB_SUCCESS; }\n'
 assert s.count(anchor)==1
 hook='''  // CATALOG_GC_PROBE_BEGIN
  if (const char *trigger = ::getenv("SEEKDB_CATALOG_GC_TRIGGER")) {
    if (share::server_is_write_enabled() && ::unlink(trigger) == 0) {
      const int result = collect_metadata();
      fprintf(stderr, "CATALOG_GC_PROBE ret=%d\\n", result);
      return result;
    }
  }
  // CATALOG_GC_PROBE_END
'''
 s=s.replace(anchor,anchor+hook)
source.write_text(s)
