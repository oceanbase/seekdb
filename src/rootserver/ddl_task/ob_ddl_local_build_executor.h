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

#ifndef OCEANBASE_ROOTSERVICE_OB_DDL_LOCAL_BUILD_EXECUTOR_H
#define OCEANBASE_ROOTSERVICE_OB_DDL_LOCAL_BUILD_EXECUTOR_H

#include "lib/container/ob_array.h"
#include "storage/ob_storage_rpc_arg.h"
#include "common/ob_tablet_id.h"
#include "share/ob_ddl_common.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace rootserver
{

struct ObDDLLocalBuildExecutorParam final
{
public:
  ObDDLLocalBuildExecutorParam ()
    : ddl_type_(share::DDL_INVALID),
      source_tablet_ids_(),
      dest_tablet_ids_(),
      source_table_ids_(OB_INVALID_ID),
      dest_table_ids_(OB_INVALID_ID),
      source_schema_versions_(0),
      dest_schema_versions_(0),
      snapshot_version_(0),
      task_id_(0),
      parallelism_(0),
      execution_id_(-1),
      data_format_version_(0),
      lob_col_idxs_()
  {}
  ~ObDDLLocalBuildExecutorParam () = default;
  bool is_valid() const {
    bool is_valid = ddl_type_ != share::DDL_INVALID &&
                     source_tablet_ids_.count() > 0 &&
                     dest_tablet_ids_.count() == source_tablet_ids_.count() &&
                     source_table_ids_.count() == source_tablet_ids_.count() &&
                     dest_table_ids_.count() == source_tablet_ids_.count() &&
                     source_schema_versions_.count() == source_tablet_ids_.count() &&
                     dest_schema_versions_.count() == source_tablet_ids_.count() &&
                     snapshot_version_ > 0 &&
                     task_id_ > 0 &&
                     parallelism_ > 0 &&
                     execution_id_ >= 0 &&
                     data_format_version_ > 0;
    return is_valid;
  }

  TO_STRING_KV(K_(ddl_type), K_(source_tablet_ids),
               K_(dest_tablet_ids), K_(source_table_ids), K_(dest_table_ids),
               K_(source_schema_versions), K_(dest_schema_versions), K_(snapshot_version),
               K_(task_id), K_(parallelism), K_(execution_id),
               K_(data_format_version), K_(lob_col_idxs));
public:

  share::ObDDLType ddl_type_;
  ObArray<ObTabletID> source_tablet_ids_;
  ObSArray<ObTabletID> dest_tablet_ids_;
  ObSArray<uint64_t> source_table_ids_;
  ObSArray<uint64_t> dest_table_ids_;
  ObSArray<uint64_t> source_schema_versions_;
  ObSArray<uint64_t> dest_schema_versions_;
  int64_t snapshot_version_;
  int64_t task_id_;
  int64_t parallelism_;
  int64_t execution_id_;
  int64_t data_format_version_;
  ObSArray<uint64_t> lob_col_idxs_;
};

enum class ObDDLBuildStat
{
  BUILD_INIT = 0,
  BUILD_REQUESTED = 1,
  BUILD_SUCCEED = 2,
  BUILD_RETRY = 3,
  BUILD_FAILED = 4
};

struct ObDDLBuildCtx final
{
public:
  static const int64_t BUILD_HEART_BEAT_TIME = 10 * 1000 * 1000;
  ObDDLBuildCtx()
    : is_inited_(false),
      ddl_type_(share::DDL_INVALID),
      src_table_id_(OB_INVALID_ID),
      dest_table_id_(OB_INVALID_ID),
      src_schema_version_(0),
      dest_schema_version_(0),
      tablet_task_id_(0),
      src_tablet_id_(ObTabletID::INVALID_TABLET_ID),
      dest_tablet_id_(),
      stat_(ObDDLBuildStat::BUILD_INIT),
      ret_code_(OB_SUCCESS),
      heart_beat_time_(0),
      row_inserted_(0),
      row_scanned_(0),
      physical_row_count_(0)
  { }
  ~ObDDLBuildCtx() = default;
  int init(const ObDDLLocalBuildExecutorParam &param, const int64_t tablet_idx);
  void reset_build_stat();
  bool is_valid() const;
  int check_need_schedule(bool &need_schedule) const;
  TO_STRING_KV(K(is_inited_), K(ddl_type_), K(src_table_id_),
               K(src_schema_version_), K(dest_schema_version_),
               K(dest_table_id_), K(tablet_task_id_),
               K(src_tablet_id_), K(dest_tablet_id_), K(stat_), K(ret_code_),
               K(heart_beat_time_), K(row_inserted_), K(row_scanned_), K(physical_row_count_));

public:
  bool is_inited_;
  share::ObDDLType ddl_type_;
  int64_t src_table_id_;
  int64_t dest_table_id_;
  int64_t src_schema_version_;
  int64_t dest_schema_version_;
  int64_t tablet_task_id_;
  ObTabletID src_tablet_id_;
  ObTabletID dest_tablet_id_;
  ObDDLBuildStat stat_;
  int64_t ret_code_;
  int64_t heart_beat_time_;
  int64_t row_inserted_;
  int64_t row_scanned_;
  int64_t physical_row_count_;
};

class ObDDLLocalBuildExecutor
{
public:
  ObDDLLocalBuildExecutor()
    : is_inited_(false),
      ddl_type_(share::ObDDLType::DDL_INVALID),
      ddl_task_id_(0),
      snapshot_version_(0),
      parallelism_(0),
      execution_id_(0),
      data_format_version_(0),
      lob_col_idxs_(),
      build_ctxs_(),
      lock_()
  {}
  ~ObDDLLocalBuildExecutor() = default;
  int build(const ObDDLLocalBuildExecutorParam &param);
  int check_build_end(const bool need_checksum, bool &is_end, int64_t &ret_code);
  int update_build_progress(const ObTabletID &tablet_id,
                            const int ret_code,
                            const int64_t row_scanned,
                            const int64_t row_inserted,
                            const int64_t physical_row_count);
  int get_progress(int64_t &row_inserted, int64_t &physical_row_count_, double& percent);

  TO_STRING_KV(K(is_inited_), K(ddl_type_),
               K(ddl_task_id_), K(snapshot_version_), K(parallelism_),
               K(execution_id_), K(data_format_version_),
               K(lob_col_idxs_), K(build_ctxs_));
private:
  int schedule_task();
  int construct_request_arg(
      const ObDDLBuildCtx &build_ctx,
      obcall::ObDDLLocalBuildArg &arg) const;
  int construct_build_ctxs(
      const ObDDLLocalBuildExecutorParam &param,
      ObArray<ObDDLBuildCtx> &build_ctxs) const;
  int update_build_ctx_status(
      ObDDLBuildCtx &build_ctx,
      const int64_t ret_code,
      const int64_t row_scanned,
      const int64_t row_inserted,
      const int64_t row_count,
      const bool is_schedule_result);
  int get_build_ctx(
      const ObTabletID &tablet_id,
      ObDDLBuildCtx *&build_ctx,
      bool &is_found);

private:
  bool is_inited_;

  share::ObDDLType ddl_type_;
  int64_t ddl_task_id_;
  int64_t snapshot_version_;
  int64_t parallelism_;
  int64_t execution_id_;
  int64_t data_format_version_;
  ObSArray<uint64_t> lob_col_idxs_;
  ObArray<ObDDLBuildCtx> build_ctxs_; // NOTE hold lock before access
  ObSpinLock lock_; // NOTE keep local service calls out of lock scope
};

}  // end namespace rootserver
}  // end namespace oceanbase

#endif  // OCEANBASE_ROOTSERVICE_OB_DDL_LOCAL_BUILD_EXECUTOR_H
