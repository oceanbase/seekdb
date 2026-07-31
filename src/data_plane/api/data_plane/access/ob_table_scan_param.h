/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_SCAN_PARAM_H_
#define OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_SCAN_PARAM_H_

#include "common/ob_common_types.h"
#include "share/transaction/ob_tx_id.h"
#include "data_plane/transaction/ob_tx_read_snapshot.h"
#include "lib/container/ob_iarray.h"
#include "data_plane/access/ob_tablet_scan.h"

namespace oceanbase
{
namespace transaction
{
class ObTxDesc;
}
namespace blocksstable
{
struct ObDatumRange;
}
namespace share
{
namespace schema
{
class ObTableParam;
}
}
namespace storage
{

class ObITableReadInfo;
struct ObMdsReadInfoCollector;

class ScanResumePoint
{
public:
  int init(bool *is_paused);
  void destroy()
  {
    reset_ranges();
    allocator_.reset();
  }
  bool is_paused() const
  {
    return nullptr != is_paused_ && ATOMIC_LOAD(is_paused_);
  }
  void set_paused()
  {
    if (nullptr != is_paused_) {
      ATOMIC_STORE(is_paused_, true);
    }
  }
  void clear_paused()
  {
    if (nullptr != is_paused_) {
      ATOMIC_STORE(is_paused_, false);
    }
  }
  bool empty() const { return ranges_.empty(); }
  int add_range(const ObITableReadInfo &read_info,
                const blocksstable::ObDatumRange &datum_range);
  void reset_ranges() { ranges_.reset(); }
  common::ObSEArray<common::ObNewRange, 1> &get_ranges() { return ranges_; }

private:
  bool *is_paused_{nullptr};
  common::ObSEArray<common::ObNewRange, 1> ranges_;
  common::ObArenaAllocator allocator_;
};

// Broad transitional scan request shared by DAS and the data plane.  Its
// public placement stops implementation-header fan-out; field reduction can
// proceed independently after the boundary is enforced.
class ObTableScanParam : public common::ObVTableScanParam
{
public:
  ObTableScanParam()
    : common::ObVTableScanParam(),
      trans_desc_(nullptr),
      snapshot_(),
      tx_id_(),
      tx_lock_timeout_(-1),
      table_param_(nullptr),
      allocator_(&CURRENT_CONTEXT->get_arena_allocator()),
      need_scn_(false),
      need_switch_param_(false),
      is_mds_query_(false),
      is_thread_scope_(true),
      tx_seq_base_(-1),
      read_version_range_(),
      need_update_tablet_param_(false),
      in_row_cache_threshold_(common::DEFAULT_MAX_MULTI_GET_CACHE_AWARE_ROW_NUM),
      scan_resume_point_(nullptr),
      mds_collector_(nullptr),
      row_scan_cnt_(nullptr),
      enable_new_false_range_(false)
  {}
  virtual ~ObTableScanParam() {}

  transaction::ObTxDesc *trans_desc_;
  transaction::ObTxReadSnapshot snapshot_;
  transaction::ObTransID tx_id_;
  int64_t tx_lock_timeout_;
  const share::schema::ObTableParam *table_param_;
  common::ObIAllocator *allocator_;
  common::SampleInfo sample_info_;
  bool need_scn_;
  bool need_switch_param_;
  bool is_mds_query_;
  OB_INLINE virtual bool is_valid() const
  {
    return snapshot_.valid_ && ObVTableScanParam::is_valid()
        && (!is_mds_query_ || nullptr != mds_collector_);
  }
  void destroy() override
  {
    ObVTableScanParam::destroy();
  }

  bool is_thread_scope_;
  int64_t tx_seq_base_;
  common::ObVersionRange read_version_range_;
  bool need_update_tablet_param_;
  int64_t in_row_cache_threshold_;
  ScanResumePoint *scan_resume_point_;
  ObMdsReadInfoCollector *mds_collector_;
  uint64_t *row_scan_cnt_;
  bool enable_new_false_range_;

  DECLARE_VIRTUAL_TO_STRING;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableScanParam);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_SCAN_PARAM_H_
