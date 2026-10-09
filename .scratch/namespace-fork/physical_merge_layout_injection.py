#!/usr/bin/env python3
"""Trace real merge layout selection and physical eligibility in a test binary."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser()
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
changes = (
    ('src/storage/compaction/ob_medium_compaction_func.cpp',
     '  return ret;\n}\n\nint ObMediumCompactionScheduleFunc::choose_major_snapshot(',
     '''  if (getenv("SEEKDB_PHYSICAL_MERGE_LAYOUT_PROBE") != nullptr) {
    fprintf(stderr, "PHYSICAL_MERGE_LAYOUT tablet=%lu G=%lu C=%ld F=%ld V=%ld ret=%d\\n",
        tablet.get_tablet_id().id(), candidate.layout_id, candidate.create_version,
        target.get_val_for_tx(), storage_schema.get_schema_version(), ret);
    fflush(stderr);
  }
'''),
    ('src/storage/compaction/ob_schedule_status_cache.cpp',
     '  return ret;\n}\n\nvoid ObTabletStatusCache::inner_init_could_schedule_new_round(',
     '''  if (getenv("SEEKDB_PHYSICAL_MERGE_LAYOUT_PROBE") != nullptr && merge_version > 1) {
    fprintf(stderr, "PHYSICAL_MERGE_MEMBER tablet=%lu C=%ld F=%ld member=%d satisfied=%d complete=%d state=%d ret=%d\\n",
        tablet_id.id(), candidate.create_version, merge_version, participates_in_round_,
        round_satisfied(), tablet.is_data_complete(), execute_state_, ret);
    fflush(stderr);
  }
'''),
    ('src/storage/compaction/ob_medium_compaction_func.cpp',
     '  medium_info.storage_schema_.reset();\n  if (access == nullptr) {',
     '''  if (getenv("SEEKDB_MEDIUM_FORCE_OLD_TARGET") != nullptr) {
    medium_info.medium_snapshot_ = 1;
  }
  const int64_t probe_proposed = medium_info.medium_snapshot_;
'''),
    ('src/storage/compaction/ob_medium_compaction_func.cpp',
     '  return ret;\n}\n\nint ObMediumCompactionScheduleFunc::prepare_medium_info(',
     '''  if (getenv("SEEKDB_PHYSICAL_MERGE_LAYOUT_PROBE") != nullptr) {
    fprintf(stderr, "PHYSICAL_MERGE_MEDIUM tablet=%lu G=%lu C=%ld proposed=%ld B=%ld V=%ld ret=%d\\n",
        tablet.get_tablet_id().id(), candidate.layout_id, candidate.create_version,
        probe_proposed, medium_info.medium_snapshot_, medium_info.storage_schema_.get_schema_version(), ret);
    fflush(stderr);
  }
'''),
)
sources = {}
for relative, _, _ in changes:
    path = root / relative
    sources[relative] = re.sub(r'^// LOCAL_MERGE_LAYOUT_BEGIN\n.*?^// LOCAL_MERGE_LAYOUT_END\n',
                              '', path.read_text(), flags=re.M | re.S)
for relative, anchor, code in changes:
    source = sources[relative]
    if a.action == 'enable':
        assert source.count(anchor) == 1, relative
        source = source.replace(anchor, '// LOCAL_MERGE_LAYOUT_BEGIN\n' + code +
                                '// LOCAL_MERGE_LAYOUT_END\n' + anchor)
    sources[relative] = source
for relative, source in sources.items():
    (root / relative).write_text(source)
print('Physical merge layout hooks', a.action)
