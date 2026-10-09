#!/usr/bin/env python3
"""Deterministic freeze/fork barriers; absent from production sources."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]


def edit(file, anchor, code, before=False):
    path = root / file
    text = re.sub(r'^[ \t]*// LOCAL_FREEZE_PREPARATION_BEGIN\n.*?^[ \t]*// LOCAL_FREEZE_PREPARATION_END\n',
                  '', path.read_text(), flags=re.M | re.S)
    if a.action == 'enable':
        assert text.count(anchor) == 1, (file, anchor)
        block = '// LOCAL_FREEZE_PREPARATION_BEGIN\n' + code + '// LOCAL_FREEZE_PREPARATION_END\n'
        text = text.replace(anchor, block + anchor if before else anchor + block)
    path.write_text(text)


# One block in this file so enable/disable remains idempotent.
path = root / 'src/rootserver/freeze/ob_major_merge_info_manager.cpp'
text = re.sub(r'^[ \t]*// LOCAL_FREEZE_PREPARATION_BEGIN\n.*?^[ \t]*// LOCAL_FREEZE_PREPARATION_END\n',
              '', path.read_text(), flags=re.M | re.S)
if a.action == 'enable':
    anchors = [
        ('int ObMajorMergeInfoManager::set_freeze_info(const ObMajorFreezeReason freeze_reason)\n', '''
static void freeze_preparation_barrier(const char *phase, int64_t deadline)
{
  const char *path = getenv("SEEKDB_FREEZE_PREPARATION_BARRIER");
  if (path == nullptr) { return; }
  bool announced = false;
  while (ObTimeUtility::current_time() < deadline) {
    FILE *input = fopen(path, "r");
    char value[32] = {};
    const bool hold = input && fscanf(input, "%31s", value) == 1 && strcmp(value, phase) == 0;
    if (input) { fclose(input); }
    if (!hold) { break; }
    if (!announced) {
      fprintf(stderr, "FREEZE_PREPARATION_BARRIER phase=%s\\n", phase);
      fflush(stderr);
      announced = true;
    }
    ob_usleep(10000);
  }
}
'''),
        ('      ret = try_set_freeze_info(freeze_reason, deadline, needs_recheck);\n',
         '      freeze_preparation_barrier("before_lock", deadline);\n'),
        ('      const int64_t check_start = ObTimeUtility::current_time();\n',
         '      freeze_preparation_barrier("locked", deadline);\n'),
    ]
    for anchor, code in anchors:
        assert text.count(anchor) == 1
        text = text.replace(anchor, '// LOCAL_FREEZE_PREPARATION_BEGIN\n' + code +
                            '// LOCAL_FREEZE_PREPARATION_END\n' + anchor)
path.write_text(text)

path = root / 'src/rootserver/ob_local_management_service.cpp'
text = re.sub(r'^[ \t]*// LOCAL_FREEZE_PREPARATION_BEGIN\n.*?^[ \t]*// LOCAL_FREEZE_PREPARATION_END\n',
              '', path.read_text(), flags=re.M | re.S)
if a.action == 'enable':
    anchors = [
        ('#include "rootserver/freeze/ob_major_freeze_helper.h"\n',
         '#include "storage/tx_storage/ob_memstore_freezer.h"\n'
         '#include "storage/tx_storage/ob_memstore_freezer_local_dispatch.h"\n'),
        ('int ObLocalManagementService::major_freeze()\n{\n', '''  if (getenv("SEEKDB_FREEZE_PRESSURE_PROBE")) {
    storage::ObMemstoreFreezeArg arg;
    arg.freeze_type_ = storage::MAJOR_FREEZE;
    arg.try_frozen_scn_ = INT64_MAX;
    const int ret = storage::dispatch_freeze(arg);
    auto *freezer = share::server_service<storage::ObMemstoreFreezer>();
    fprintf(stderr, "FREEZE_PREPARATION_PRESSURE ret=%d retry=%d\\n", ret,
        freezer == nullptr ? -1 : int(freezer->get_retry_major_info().is_valid()));
    fflush(stderr);
    return ret;
  }
'''),
    ]
    for anchor, code in anchors:
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '// LOCAL_FREEZE_PREPARATION_BEGIN\n' + code +
                            '// LOCAL_FREEZE_PREPARATION_END\n')
path.write_text(text)

edit('src/rootserver/fork_table/namespace_maintenance.cpp',
     'int NamespaceMaintenance::materialize_inherited_tablets()\n{\n',
     '''  if (const char *path = getenv("SEEKDB_MATERIALIZATION_PAUSE")) {
    FILE *input = fopen(path, "r");
    if (input) { fclose(input); return OB_SUCCESS; }
  }
''')

edit('src/rootserver/fork_table/instance_namespace_metadata.cpp',
     '  id = high + 1;\n',
     '''  if (const char *path = getenv("SEEKDB_NS_ALLOCATION_PAUSE")) {
    bool announced = false;
    while (ObTimeUtility::current_time() < THIS_WORKER.get_timeout_ts()) {
      FILE *input = fopen(path, "r");
      if (!input) { break; }
      fclose(input);
      if (!announced) {
        fprintf(stderr, "FREEZE_PREPARATION_FORK_ALLOCATION_LOCKED\\n");
        fflush(stderr);
        announced = true;
      }
      ob_usleep(10000);
    }
  }
''')
