#!/usr/bin/env python3
"""Local route evidence only; remove all marked blocks before production build."""
from pathlib import Path
import argparse
import re
ROOT = Path(__file__).resolve().parents[2]
BLOCKS = [
('src/rootserver/fork_table/namespace_tablet_access.cpp',
 '  int ret = prepare_read(namespace_id, param.index_id_, param.tablet_id_, mode, view);',
 '''  if (::getenv("SEEKDB_SQL_VIEW_PROBE") != nullptr) {
    fprintf(stderr, "SQL_VIEW_LOOKUP ns=%lu table=%lu snapshot=%ld found=%d\\n",
        namespace_id, param.index_id_, param.snapshot_.core_.version_.get_val_for_tx(), bool(view));
  }
'''),
('src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp',
 '    if (view->entry().namespace_id != database_of(tablet_id.id())) { return OB_INVALID_ARGUMENT; }',
 '''    if (::getenv("SEEKDB_SQL_VIEW_PROBE") != nullptr) {
      fprintf(stderr, "SQL_VIEW_ROUTE ns=%lu logical=%lu snapshot=%ld\\n",
          view->entry().namespace_id, local_of(tablet_id.id()), view->entry().snapshot);
    }
''')]
def main():
    p = argparse.ArgumentParser(); p.add_argument('action', choices=['enable', 'disable']); args=p.parse_args()
    source = ROOT / 'src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp'
    content = source.read_text()
    content = re.sub(r'^[ \t]*// SQL_VIEW_NATIVE_PROBE_BEGIN\n.*?^[ \t]*// SQL_VIEW_NATIVE_PROBE_END\n', '', content, flags=re.M|re.S)
    if args.action == 'enable':
        entry = 'int NamespaceForkKernelPrototype::ensure_control_schema(bool initial_install) {\n'
        hook = '// SQL_VIEW_NATIVE_PROBE_BEGIN\n#include "' + str(Path(__file__).resolve().with_name('catalog_read_view_probe.ipp')) + '"\n// SQL_VIEW_NATIVE_PROBE_END\n'
        hook += entry + '  // SQL_VIEW_NATIVE_PROBE_BEGIN\n  if (::getenv("SEEKDB_SQL_VIEW_NATIVE_PROBE") != nullptr) {\n    const int ret = run_catalog_read_view_probe();\n    if (ret != OB_SUCCESS) { return ret; }\n  }\n  // SQL_VIEW_NATIVE_PROBE_END\n'
        assert content.count(entry) == 1
        content = content.replace(entry, hook)
        content = content.replace('#include \"rootserver/fork_table/namespace_fork_kernel_prototype.h\"', '#include \"rootserver/fork_table/namespace_fork_kernel_prototype.h\"\n// SQL_VIEW_NATIVE_PROBE_BEGIN\n#include \"storage/tx/ob_trans_service.h\"\n// SQL_VIEW_NATIVE_PROBE_END')
    if content != source.read_text(): source.write_text(content)

    for name, anchor, code in BLOCKS:
        path=ROOT/name; content=path.read_text()
        block='// SQL_VIEW_PROBE_BEGIN\n'+code+'// SQL_VIEW_PROBE_END\n'
        if args.action=='enable':
            assert block not in content and content.count(anchor)==1, name
            content=content.replace(anchor, block+anchor)
        else:
            assert content.count(block)==1, name
            content=content.replace(block, '')
        path.write_text(content)
if __name__=='__main__': main()
