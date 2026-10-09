#!/usr/bin/env python3
"""Local pause of one replica's baseline and native progress observation."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('action', choices=('enable', 'disable'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]


def edit(path, anchor, code):
    path = root / path
    text = re.sub(r'^[ \t]*// LOCAL_BASELINE_PROGRESS_BEGIN\n.*?^[ \t]*// LOCAL_BASELINE_PROGRESS_END\n',
                  '', path.read_text(), flags=re.M | re.S)
    if args.action == 'enable':
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '// LOCAL_BASELINE_PROGRESS_BEGIN\n' + code +
                            '// LOCAL_BASELINE_PROGRESS_END\n')
    path.write_text(text)


edit('src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp',
     'int NamespaceForkKernelPrototype::schedule_baseline_impl(const ObTablet &tablet, int depth) {\n',
     '''  if (const char *path = getenv("SEEKDB_BASELINE_PAUSE")) {
    FILE *input = fopen(path, "r");
    if (input) {
      unsigned long target = 0;
      const bool paused = fscanf(input, "%lu", &target) == 1
          && (target == 0 || target == tablet.get_tablet_meta().tablet_id_.id());
      fclose(input);
      if (paused) { return OB_SUCCESS; }
    }
  }
''')
edit('src/observer/virtual_table/ob_all_virtual_tablet_info.cpp',
     '          // ref_tablet_id\n          cur_row_.cells_[i].set_int(0);\n',
     '''          if (getenv("SEEKDB_BASELINE_PROGRESS_PROBE")) {
            const auto &fork = tablet_meta.fork_info_;
            cur_row_.cells_[i].set_int(fork.is_valid() && !fork.is_complete()
                ? fork.get_fork_src_tablet_id().id() : 0);
          }
''')
