#!/usr/bin/env python3
"""Local hook: run a concurrent writer while the GC scan snapshot is still held."""
import argparse,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=('enable','disable'));a=p.parse_args()
local=Path(__file__).resolve().parent
source=local.parents[1]/'src/rootserver/fork_table/instance_namespace_metadata.cpp'
s=source.read_text()
s=re.sub(r'// LOCAL_GC_BOUNDARY_BEGIN\n.*?// LOCAL_GC_BOUNDARY_END\n','',s,flags=re.S)
if a.action=='enable':
    anchor='#include "rootserver/fork_table/instance_namespace_metadata.h"\n'
    assert s.count(anchor)==1
    s=s.replace(anchor,anchor+'// LOCAL_GC_BOUNDARY_BEGIN\n#include "'+str(local/'catalog_gc_test_hook.h')+'"\n// LOCAL_GC_BOUNDARY_END\n')
    anchor='    ret = metadata.find_unreachable_pages(256, roots, garbage);\n'
    assert s.count(anchor)==1
    s=s.replace(anchor,anchor+'// LOCAL_GC_BOUNDARY_BEGIN\n    if (ret == OB_SUCCESS && !garbage.empty() && catalog_gc_test::after_scan) { ret = catalog_gc_test::after_scan(); }\n// LOCAL_GC_BOUNDARY_END\n')
if s!=source.read_text():source.write_text(s)
