#!/usr/bin/env python3
"""Removable controls for candidate-list renewal and readable-horizon gating."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
local = Path(__file__).resolve().parent
root = local.parents[1]
tag = 'LOCAL_MERGE_CANDIDATES_PROBE'


def block(body):
    return '// ' + tag + '_BEGIN\n' + body + '// ' + tag + '_END\n'


patches = {
    'ob_compaction_schedule_iterator.cpp': [(
        '  int ret = ls_->get_tablet_svr()->get_all_tablet_ids(is_major_/*except_ls_inner_tablet*/, tablet_ids_.array_);\n',
        '''  if (is_major_ && OB_SUCC(ret) && getenv("SEEKDB_MERGE_CANDIDATES_PROBE") != nullptr) {
    const int64_t target = candidate_probe_value("target");
    bool found = false;
    for (int64_t i = 0; i < tablet_ids_.array_.count(); ++i) {
      found |= tablet_ids_.array_.at(i).id() == target;
    }
    const int64_t f = ObBasicMergeScheduler::get_merge_scheduler()->get_frozen_version();
    candidate_probe_log("ENUM", f, tablet_ids_.array_.count(), found);
    char pause[4096];
    if (candidate_probe_path("pause", pause) && unlink(pause) == 0) {
      // Preserve an unfinished old candidate list across the next batch.
      max_batch_tablet_cnt_ = 1;
      candidate_probe_log("PAUSED", f, tablet_ids_.array_.count(), target);
      char release[4096];
      if (candidate_probe_path("release", release)) {
        for (int i = 0; i < 18000 && access(release, F_OK) != 0; ++i) { usleep(10000); }
      }
      candidate_probe_log("RELEASED", f);
    }
  }
''')],
    'ob_medium_loop.cpp': [(
        '  add_event_and_diagnose(func);\n',
        '  candidate_probe_log("BATCH", merge_version_, tablet_iter_.is_scan_finish(), tablet_iter_.database_merge_finish());\n')],
    'ob_schedule_status_cache.cpp': [(
        '      weak_read_ts_ = ls->get_ls_wrs_handler()->get_ls_weak_read_ts();\n',
        '''      const int64_t hold_after = candidate_probe_value("horizon");
      if (hold_after > 0 && merge_version > hold_after) {
        candidate_probe_log("BLOCK", merge_version, weak_read_ts_.get_val_for_tx());
        weak_read_ts_.convert_for_tx(merge_version - 1);
      }
''')],
    'ob_compaction_schedule_util.cpp': [(
        '      frozen_version_ = broadcast_version;\n',
        '      candidate_probe_log("REQUEST", broadcast_version);\n')],
}
for filename, insertions in patches.items():
    path = root / 'src/storage/compaction' / filename
    text = path.read_text()
    text = re.sub(r'^// ' + tag + r'_BEGIN\n.*?^// ' + tag + r'_END\n', '', text,
                  flags=re.M | re.S)
    if a.action == 'enable':
        anchor = 'namespace oceanbase\n{'
        assert text.count(anchor) == 1, filename
        text = text.replace(anchor, block('#include "storage/compaction/ob_compaction_schedule_util.h"\n'
                                         '#include "' + str(local / 'merge_candidates_probe.ipp') + '"\n') + anchor)
        for anchor, body in insertions:
            assert text.count(anchor) == 1, (filename, anchor)
            text = text.replace(anchor, anchor + block(body))
    path.write_text(text)
print('Candidate probe ' + a.action + 'd')
