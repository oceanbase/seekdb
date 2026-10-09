#!/usr/bin/env python3
"""Local counters for lazy preparation and caller-owned LOB output; never commit."""
import argparse
from pathlib import Path
import re
parser = argparse.ArgumentParser()
parser.add_argument('action', choices=('enable','disable'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
hooks = {
 'src/rootserver/fork_table/instance_namespace_metadata.cpp': (
  'int InstanceNamespaceMetadata::read_table_definition(ns::CatalogPageRef root,\n    uint64_t table_id, std::string &definition)\n{\n',
  '  if (getenv("SEEKDB_PREPARATION_PROBE")) { fprintf(stderr, "PREPARATION_CATALOG table=%lu\\n", table_id); }\n'),
 'src/observer/namespace_worker_gateway_prototype.ipp': (
  '    THIS_WORKER.set_session(old_session);\n    THIS_WORKER.set_timeout_ts(old_timeout);\n  }\n  return ret;\n}\nint compare_in_process_lobs',
  '')
}
for file, (anchor, code) in hooks.items():
 p = root / file
 text = re.sub(r'^[ \t]*// LOCAL_PREPARATION_BEGIN\n.*?^[ \t]*// LOCAL_PREPARATION_END\n','',p.read_text(), flags=re.M|re.S)
 if args.action == 'enable':
  if file.endswith('gateway_prototype.ipp'):
   anchor = '    ret = data_plane::read_lob_to_buffer(allocator, locator,\n        std::min(timeout, old_timeout), storage->writes->tx, output);\n'
   code = ('    if (getenv("SEEKDB_PREPARATION_PROBE") && destination != nullptr && ret == OB_SUCCESS) {\n'
           '      if (length > 0 && output.ptr() != destination->ptr()) { ret = OB_ERR_UNEXPECTED; }\n'
           '      fprintf(stderr, "PREPARATION_LOB_DIRECT bytes=%ld ret=%d\\n", length, ret);\n    }\n')
  assert text.count(anchor) == 1, file
  text = text.replace(anchor,anchor+'// LOCAL_PREPARATION_BEGIN\n'+code+'// LOCAL_PREPARATION_END\n',1)
 p.write_text(text)
print('Preparation probes', args.action)
