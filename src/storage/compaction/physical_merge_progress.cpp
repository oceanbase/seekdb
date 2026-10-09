/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "storage/compaction/physical_merge_progress.h"
#include "storage/compaction/physical_merge_candidate.h"
#include "storage/ls/ob_ls.h"
#include "storage/ls/ob_ls_tablet_service.h"
#include "storage/tablet/ob_tablet.h"
#include "share/tablet/ob_tablet_meta_table_storage.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace compaction
{
int check_physical_merge_progress(storage::ObLS &ls, share::ObSQLiteConnectionPool &reports,
    int64_t target, volatile bool &stop, bool &finished)
{
  using namespace common;
  using namespace storage;
  using namespace share;
  int ret = OB_SUCCESS;
  finished = false;
  ObArray<ObTabletID> tablet_ids;
  ObTabletMetaTableStorage report_storage;
  const SCN readable = ls.get_ls_wrs_handler()->get_ls_weak_read_ts();
  if (target <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ls.is_offline() || !readable.is_valid() || readable.get_val_for_tx() < target) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(ls.get_tablet_svr()->get_all_tablet_ids(true, tablet_ids))) {
  } else if (OB_FAIL(report_storage.init(&reports))) {
  } else {
    lib::ob_sort(tablet_ids.begin(), tablet_ids.end());
    finished = true;
    const int64_t batch_size = 256;
    ObSEArray<ObTabletID, batch_size> batch_ids;
    ObArray<ObTabletRuntimeInfo> batch_reports;
    for (int64_t first = 0; OB_SUCC(ret) && finished && !stop && first < tablet_ids.count(); first += batch_size) {
      batch_ids.reuse();
      const int64_t end = MIN(first + batch_size, tablet_ids.count());
      for (int64_t i = first; OB_SUCC(ret) && i < end; ++i) {
        ret = batch_ids.push_back(tablet_ids.at(i));
      }
      if (FAILEDx(report_storage.batch_get(batch_ids, batch_reports))) {
      }
      int64_t report_index = 0;
      for (int64_t i = first; OB_SUCC(ret) && finished && !stop && i < end; ++i) {
        const ObTabletID &id = tablet_ids.at(i);
        ObTabletHandle handle;
        PhysicalMergeCandidate candidate;
        if (OB_FAIL(ls.get_tablet_svr()->get_tablet(id, handle, 0, ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
          if (ret == OB_TABLET_NOT_EXIST) { ret = OB_SUCCESS; }
        } else if (OB_FAIL(candidate.load(*handle.get_obj()))) {
        } else if (candidate.participates(target)) {
          while (report_index < batch_reports.count() && batch_reports.at(report_index).get_tablet_id() < id) {
            ++report_index;
          }
          const ObTabletRuntimeInfo *report = report_index < batch_reports.count()
              ? &batch_reports.at(report_index) : nullptr;
          if (!handle.get_obj()->is_data_complete() || report == nullptr || !candidate.matches(*report)) {
            finished = false;
          } else if (report->get_status() == ObTabletRuntimeInfo::SCN_STATUS_ERROR) {
            ret = OB_CHECKSUM_ERROR;
          } else if (report->get_snapshot_version() < target) {
            finished = false;
          }
          if (!finished) {
            LOG_INFO("physical merge result pending", K(id), K(target),
                "create_transaction_id", candidate.create_transaction_id,
                "create_version", candidate.create_version, "layout_id", candidate.layout_id,
                "data_complete", handle.get_obj()->is_data_complete(), KPC(report));
          }
        }
      }
    }
  }
  if (stop) { ret = OB_CANCELED; }
  if (ret != OB_SUCCESS) { finished = false; }
  return ret;
}
}
}
