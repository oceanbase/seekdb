/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_STORAGE_OB_STORAGE_STRUCT_H_
#define SRC_STORAGE_OB_STORAGE_STRUCT_H_

#include "blocksstable/ob_block_sstable_struct.h"
#include "common/ob_store_range.h"
#include "share/scn.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_table_schema.h"
#include "storage/ob_i_table.h"
#include "storage/ob_storage_schema.h"
#include "storage/tablet/ob_tablet_table_store_flag.h"
#include "storage/compaction/ob_compaction_util.h"
#include "storage/compaction/ob_medium_compaction_mgr.h"
#include "storage/ddl/ob_ddl_struct.h"
#include "storage/ob_tablet_local_status.h"
#include "storage/meta_mem/ob_tablet_handle.h"

namespace oceanbase
{

namespace share
{
struct ObDiagnoseLocation;
}

namespace storage
{
class ObStorageSchema;

typedef common::ObSEArray<common::ObStoreRowkey, common::OB_DEFAULT_MULTI_GET_ROWKEY_NUM> GetRowkeyArray;
typedef common::ObSEArray<common::ObStoreRange, common::OB_DEFAULT_MULTI_GET_ROWKEY_NUM> ScanRangeArray;

static const int64_t EXIST_READ_SNAPSHOT_VERSION = share::OB_MAX_SCN_TS_NS - 1;
static const int64_t MERGE_READ_SNAPSHOT_VERSION = share::OB_MAX_SCN_TS_NS - 2;
// static const int64_t BUILD_INDEX_READ_SNAPSHOT_VERSION = INT64_MAX - 6;
// static const int64_t WARM_UP_READ_SNAPSHOT_VERSION = INT64_MAX - 7;
static const int64_t GET_BATCH_ROWS_READ_SNAPSHOT_VERSION = share::OB_MAX_SCN_TS_NS - 8;
// static const int64_t GET_SCAN_COST_READ_SNAPSHOT_VERSION = INT64_MAX - 9;

#ifdef ERRSIM
struct ObErrsimBackfillPointType final
{
  OB_UNIS_VERSION(1);
public:
  enum TYPE
  {
    ERRSIM_POINT_NONE = 0,
    ERRSIM_START_BACKFILL_BEFORE = 1,
    ERRSIM_REPLACE_SWAP_BEFORE = 2,
    ERRSIM_REPLACE_AFTER = 3,
    ERRSIM_MODULE_MAX
  };
  ObErrsimBackfillPointType() : type_(ERRSIM_POINT_NONE) {}
  explicit ObErrsimBackfillPointType(const ObErrsimBackfillPointType::TYPE &type) : type_(type) {}
  ~ObErrsimBackfillPointType() = default;
  void reset();
  bool is_valid() const;
  bool operator == (const ObErrsimBackfillPointType &other) const;
  int hash(uint64_t &hash_val) const;
  int64_t hash() const;
  TO_STRING_KV(K_(type));
  TYPE type_;
};

class ObErrsimBackfillPoint final
{
public:
  ObErrsimBackfillPoint();
  virtual ~ObErrsimBackfillPoint();
  bool is_valid() const;
  void reset();
  int set_point_type(const ObErrsimBackfillPointType &point_type);
  int set_point_start_time(int64_t start_time);
  bool is_errsim_point(const ObErrsimBackfillPointType &point_type) const;
  int64_t get_point_start_time() { return point_start_time_; }
  TO_STRING_KV(K_(point_type), K_(point_start_time));
private:
  ObErrsimBackfillPointType point_type_;
  int64_t point_start_time_;
};
#endif

struct ObTabletReportStatus
{
  ObTabletReportStatus()
    : merge_snapshot_version_(0),
      cur_report_version_(0),
      data_checksum_(0),
      row_count_(0)
  {
  }
  ~ObTabletReportStatus() { };
  void reset()
  {
    merge_snapshot_version_ = 0;
    cur_report_version_ = 0;
    data_checksum_ = 0;
    row_count_ = 0;
  }
  bool need_report() const { return merge_snapshot_version_ > cur_report_version_; }
  TO_STRING_KV(K_(merge_snapshot_version), K_(cur_report_version), K_(data_checksum), K_(row_count));
  int64_t merge_snapshot_version_;
  int64_t cur_report_version_;
  int64_t data_checksum_;
  int64_t row_count_;
  OB_UNIS_VERSION(1);
};


struct ObReportStatus
{
  ObReportStatus()
    : data_version_(0), row_count_(0), row_checksum_(0), data_checksum_(0), data_size_(0),
      required_size_(0), snapshot_version_(0)
  {
  }
  TO_STRING_KV(K_(data_version), K_(row_count), K_(row_checksum),
      K_(data_checksum), K_(data_size), K_(required_size), K_(snapshot_version));
  void reset()
  {
    data_version_ = 0;
    row_count_ = 0;
    row_checksum_ = 0;
    data_checksum_ = 0;
    data_size_ = 0;
    required_size_ = 0;
    snapshot_version_ = 0;
  }
  int64_t data_version_;
  int64_t row_count_;
  int64_t row_checksum_;
  int64_t data_checksum_;
  int64_t data_size_;
  int64_t required_size_;
  int64_t snapshot_version_;
  OB_UNIS_VERSION(1);
};

struct ObPGReportStatus
{
  ObPGReportStatus() { reset(); }
  void reset()
  {
    data_version_ = 0;
    data_size_ = 0;
    required_size_ = 0;
    snapshot_version_ = 0;
  }
  TO_STRING_KV(K_(data_version), K_(data_size), K_(required_size),
    K_(snapshot_version));
  int64_t data_version_;
  int64_t data_size_;
  int64_t required_size_;
  int64_t snapshot_version_; //major frozen ts
  OB_UNIS_VERSION(1);
};

enum ObPartitionBarrierLogStateEnum
{
  BARRIER_LOG_INIT = 0,
  BARRIER_LOG_WRITTING,
  BARRIER_SOURCE_LOG_WRITTEN,
  BARRIER_DEST_LOG_WRITTEN
};

struct ObPartitionBarrierLogState final
{
public:
  ObPartitionBarrierLogState();
  ~ObPartitionBarrierLogState() = default;
  ObPartitionBarrierLogStateEnum &get_state() { return state_; }
  int64_t get_log_id() { return log_id_; }
  share::SCN get_scn() { return scn_; }
  int64_t get_schema_version() { return schema_version_; }
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(state));
private:
  ObPartitionBarrierLogStateEnum to_persistent_state() const;
private:
  ObPartitionBarrierLogStateEnum state_;
  int64_t log_id_;
  share::SCN scn_;
  int64_t schema_version_;
};

struct ObGetMergeTablesParam
{
  compaction::ObMergeType merge_type_;
  int64_t merge_version_;
  ObGetMergeTablesParam();
  bool is_valid() const;
  OB_INLINE bool is_major_valid() const
  {
    return compaction::is_major_merge_type(merge_type_) && merge_version_ > 0;
  }
  TO_STRING_KV("merge_type", merge_type_to_str(merge_type_), K_(merge_version));
};

struct ObGetMergeTablesResult
{
  common::ObVersionRange version_range_;
  ObTablesHandleArray handle_;
  int64_t merge_version_;
  bool update_tablet_directly_;
  bool schedule_major_;
  bool is_simplified_;
  share::ObScnRange scn_range_;
  share::ObDiagnoseLocation *error_location_;
  ObStorageSnapshotInfo snapshot_info_;
  //for backfill
  bool is_backfill_;
  share::SCN backfill_scn_;
  ObGetMergeTablesResult();
  bool is_valid() const;
  void reset_handle_and_range();
  void simplify_handle(); // called when schedule ExeMergeDag
  void reset();
  int assign(const ObGetMergeTablesResult &src);
  int copy_basic_info(const ObGetMergeTablesResult &src);
  share::SCN get_merge_scn() const;
  TO_STRING_KV(K_(version_range), K_(scn_range), K_(merge_version), K_(is_simplified),
      K_(handle), K_(update_tablet_directly), K_(schedule_major), K_(is_backfill), K_(backfill_scn));
};

struct ObDDLTableStoreParam final
{
public:
  ObDDLTableStoreParam();
  ~ObDDLTableStoreParam() = default;
  TO_STRING_KV(K_(keep_old_ddl_sstable), K_(update_with_major_flag),
               K_(ddl_start_scn), K_(ddl_commit_scn), K_(ddl_checkpoint_scn),
               K_(ddl_snapshot_version), K_(ddl_execution_id),
               K_(data_format_version), KP_(ddl_redo_callback),
               KP_(ddl_finish_callback), K(slice_sstables_.count()));

public:
  bool keep_old_ddl_sstable_;
  bool update_with_major_flag_; // when ddl first create major sstable, set TRUE
  share::SCN ddl_start_scn_;
  share::SCN ddl_commit_scn_;
  share::SCN ddl_checkpoint_scn_;
  int64_t ddl_snapshot_version_;
  int64_t ddl_execution_id_;
  int64_t data_format_version_;
  blocksstable::ObIMacroBlockFlushCallback *ddl_redo_callback_;
  blocksstable::ObIMacroBlockFlushCallback *ddl_finish_callback_;
  ObArray<const blocksstable::ObSSTable *> slice_sstables_;
};

struct ObCompactionTableStoreParam final
{
public:
  ObCompactionTableStoreParam();
  ObCompactionTableStoreParam(
    const compaction::ObMergeType merge_type,
    const share::SCN clog_checkpoint_scn,
    const bool need_report,
    const bool has_truncate_info);
  ~ObCompactionTableStoreParam() = default;
  bool is_valid() const;
  int assign(const ObCompactionTableStoreParam &other);
  TO_STRING_KV(K_(clog_checkpoint_scn), K_(need_report),
    "merge_type", merge_type_to_str(merge_type_), K_(has_truncate_info));
public:
  compaction::ObMergeType merge_type_;
  share::SCN clog_checkpoint_scn_;
  bool need_report_;
  bool has_truncate_info_;
};

struct UpdateUpperTransParam final
{
public:
  UpdateUpperTransParam();
  ~UpdateUpperTransParam();
  void reset();
  TO_STRING_KV(K_(new_upper_trans), K_(last_minor_end_scn));
public:
  ObIArray<int64_t> *new_upper_trans_;
  share::SCN last_minor_end_scn_;
};

struct ObUpdateTableStoreParam
{
  ObUpdateTableStoreParam(); // for compaction task only
  ObUpdateTableStoreParam(
    const int64_t snapshot_version,
    const int64_t multi_version_start,
    const ObStorageSchema *storage_schema,
    const blocksstable::ObSSTable *sstable = NULL,
    const bool allow_duplicate_sstable = false);
  ObUpdateTableStoreParam(
    const int64_t snapshot_version,
    const int64_t multi_version_start,
    const ObStorageSchema *storage_schema,
    const UpdateUpperTransParam upper_trans_param);
  int init_with_compaction_info(const ObCompactionTableStoreParam &comp_param);
  void set_upper_trans_param(const UpdateUpperTransParam upper_trans_param) { upper_trans_param_ = upper_trans_param; }
  bool is_valid() const;
  bool need_report_major() const;
  bool get_need_check_sstable() const { return is_minor_merge_type(compaction_info_.merge_type_); }
  #define PARAM_DEFINE_FUNC(var_type, param, var_name) \
    OB_INLINE var_type get_##var_name() const { return param. var_name##_; }
  #define COMP_PARAM_FUNC(var_type, var_name) \
    PARAM_DEFINE_FUNC(var_type, compaction_info_, var_name)
  COMP_PARAM_FUNC(compaction::ObMergeType, merge_type);
  COMP_PARAM_FUNC(share::SCN, clog_checkpoint_scn);
  PARAM_DEFINE_FUNC(bool, ddl_info_, update_with_major_flag);
  #undef COMP_PARAM_FUNC
  #undef PARAM_DEFINE_FUNC
  TO_STRING_KV(KP_(sstable), K_(snapshot_version), K_(multi_version_start),
               KPC_(storage_schema), K_(compaction_info),
               K_(ddl_info), K_(allow_duplicate_sstable), K_(upper_trans_param));
  ObCompactionTableStoreParam compaction_info_;
  ObDDLTableStoreParam ddl_info_;

  int64_t snapshot_version_;
  int64_t multi_version_start_;
  const ObStorageSchema *storage_schema_;
  const blocksstable::ObSSTable *sstable_;
  bool allow_duplicate_sstable_;
  UpdateUpperTransParam upper_trans_param_; // set upper_trans_param_ only when update upper_trans_version
};

struct ObForkTableStoreParam final
{
public:
  ObForkTableStoreParam();
  ~ObForkTableStoreParam();
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(snapshot_version), K_(multi_version_start), K_(merge_type), K_(clog_checkpoint_scn), K_(mds_checkpoint_scn));

public:
  int64_t snapshot_version_;
  int64_t multi_version_start_;
  compaction::ObMergeType merge_type_;
  share::SCN clog_checkpoint_scn_;
  share::SCN mds_checkpoint_scn_;
};

struct ObBatchUpdateTableStoreParam final
{
  ObBatchUpdateTableStoreParam();
  ~ObBatchUpdateTableStoreParam() = default;
  bool is_valid() const;
  void reset();

  TO_STRING_KV(K_(tables_handle),
      KP_(source_storage_schema), K_(tablet_fork_param));

  ObTablesHandleArray tables_handle_;
#ifdef ERRSIM
  ObErrsimBackfillPoint errsim_point_info_;
#endif
  const ObStorageSchema *source_storage_schema_;
  ObForkTableStoreParam tablet_fork_param_;

  DISALLOW_COPY_AND_ASSIGN(ObBatchUpdateTableStoreParam);
};

struct ObPartitionReadableInfo
{
  int64_t min_log_service_ts_;
  int64_t min_trans_service_ts_;
  int64_t min_replay_engine_ts_;

  int64_t generated_ts_;
  int64_t max_readable_ts_;
  bool force_;

  ObPartitionReadableInfo();
  ~ObPartitionReadableInfo();


  TO_STRING_KV(K(min_log_service_ts_),
               K(min_trans_service_ts_),
               K(min_replay_engine_ts_),
               K(generated_ts_),
               K(max_readable_ts_));
};

struct ObCreateSSTableParamExtraInfo
{
public:
  ObCreateSSTableParamExtraInfo()
    : column_default_checksum_(nullptr),
      column_cnt_(0)
  {
  }
  ~ObCreateSSTableParamExtraInfo() {}
  void reset()
  {
    column_default_checksum_ = nullptr;
    column_cnt_ = 0;
  }

  TO_STRING_KV(K_(column_default_checksum), K_(column_cnt));

  int64_t *column_default_checksum_;
  uint64_t column_cnt_;
};

struct ObTransTableStatus
{
public:
  ObTransTableStatus()
    : end_log_ts_(0),
      row_count_(0)
      {
      }
  int64_t end_log_ts_;
  int64_t row_count_;
};

}//storage
}//oceanbase


#endif /* SRC_STORAGE_OB_STORAGE_STRUCT_H_ */
