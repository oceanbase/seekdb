#!/usr/bin/env python3
"""Local-only initial template failure. Never included in a production build."""
import argparse,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=('enable','disable'));a=p.parse_args()
target=Path(__file__).resolve().parents[2]/'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
s=target.read_text();s=re.sub(r'// LOCAL_TEMPLATE_BASELINE_BEGIN\n.*?// LOCAL_TEMPLATE_BASELINE_END\n','',s,flags=re.S)
if a.action=='enable':
    anchor='int NamespaceForkKernelPrototype::complete_initial_baseline(uint64_t id, int64_t deadline)\n{\n'
    assert s.count(anchor)==1
    hook='''// LOCAL_TEMPLATE_BASELINE_BEGIN
  if (getenv("SEEKDB_TEMPLATE_FAIL_INITIAL_BASELINE")) {
    fprintf(stderr, "TEMPLATE_INITIAL_FAILURE_INJECTED\\n");
    return OB_ERR_UNEXPECTED;
  }
// LOCAL_TEMPLATE_BASELINE_END
'''
    s=s.replace(anchor,anchor+hook)
if s!=target.read_text():target.write_text(s)
