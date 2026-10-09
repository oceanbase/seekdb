#!/usr/bin/env python3
"""Pause the round checker and hold one report writer in a disposable binary."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser()
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
changes = (
    ('src/rootserver/freeze/ob_global_merge_manager.cpp',
     'int ObGlobalMergeManagerBase::try_update_global_last_merged_scn()\n{\n',
     '''  if (const char *path = getenv("SEEKDB_CHECKSUM_COMPLETION_GATE")) {
    if (FILE *gate = fopen(path, "r")) {
      fclose(gate);
      fprintf(stderr, "CHECKSUM_BEFORE_DURABLE_COMPLETION F=%ld\\n",
          global_merge_info_.global_broadcast_scn().get_val_for_tx());
      fflush(stderr);
      return OB_SUCCESS;
    }
  }
'''),
    ('src/rootserver/freeze/ob_major_merge_scheduler.cpp',
     '  DEBUG_SYNC(RS_VALIDATE_CHECKSUM);\n',
     '''  if (const char *path = getenv("SEEKDB_PHYSICAL_REPORT_GATE")) {
    FILE *gate = fopen(path, "r");
    if (gate != nullptr) { fclose(gate); return OB_SUCCESS; }
    fprintf(stderr, "PHYSICAL_ROUND_CHECK F=%ld\\n", global_broadcast_scn.get_val_for_tx());
    fflush(stderr);
  }
'''),
    ('src/share/ob_tablet_local_checksum_operator.cpp',
     '''int ObTabletLocalChecksumOperator::batch_update_with_trans(
    ObSQLiteConnection *conn,
    const common::ObIArray<ObTabletLocalChecksumItem> &items)
{
''',
     '''  if (const char *path = getenv("SEEKDB_PHYSICAL_REPORT_HOLD")) {
    FILE *input = fopen(path, "r");
    unsigned long target = 0;
    if (input != nullptr) {
      const bool have_target = fscanf(input, "%lu", &target) == 1;
      fclose(input);
      for (int64_t i = 0; have_target && i < items.count(); ++i) {
        if (items.at(i).tablet_id_.id() == target) { return OB_EAGAIN; }
      }
    }
  }
'''),
)
for relative, anchor, code in changes:
    path = root / relative
    source = re.sub(r'^// LOCAL_PHYSICAL_REPORT_BEGIN\n.*?^// LOCAL_PHYSICAL_REPORT_END\n',
                    '', path.read_text(), flags=re.M | re.S)
    if a.action == 'enable':
        assert source.count(anchor) == 1, relative
        source = source.replace(anchor, anchor + '// LOCAL_PHYSICAL_REPORT_BEGIN\n' + code +
                                '// LOCAL_PHYSICAL_REPORT_END\n')
    path.write_text(source)
print('Physical report hooks', a.action)
