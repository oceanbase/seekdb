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
#ifndef OCEANBASE_STORAGE_OB_STORAGE_RPC_ARG_H_
#define OCEANBASE_STORAGE_OB_STORAGE_RPC_ARG_H_
// moved from share/ob_rpc_struct.h:RPC arguments that embed storage types by value
// (owner=storage;ns obrpc unchanged;RPC declaration remains in share proxy,vertical split for task 5)
#include "share/ob_rpc_struct.h"
#include "storage/tablet/ob_tablet_binding_mds_user_data.h"
#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"
namespace oceanbase
{
namespace obcall
{
struct ObBatchGetTabletBindingRes final
{
  OB_UNIS_VERSION(1);
public:
  ObBatchGetTabletBindingRes() : binding_datas_() {}
  ~ObBatchGetTabletBindingRes() {}
public:
  bool is_valid() const { return binding_datas_.count() > 0; }
  TO_STRING_KV(K_(binding_datas));
public:
  common::ObSArray<storage::ObTabletBindingMdsUserData> binding_datas_;
};

struct ObDDLLocalBuildArg final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLLocalBuildArg() :
      source_tablet_id_(), dest_tablet_id_(),
      source_table_id_(OB_INVALID_ID), dest_schema_id_(OB_INVALID_ID),
      schema_version_(0), snapshot_version_(0), ddl_type_(0), task_id_(0), parallelism_(0), execution_id_(-1), tablet_task_id_(0),
      data_format_version_(0), dest_schema_version_(0), lob_col_idxs_()
  {}
  bool is_valid() const;
  int assign(const ObDDLLocalBuildArg &other);
  TO_STRING_KV(K_(source_tablet_id), K_(dest_tablet_id),
    K_(source_table_id), K_(dest_schema_id), K_(schema_version), K_(snapshot_version), K_(ddl_type),
    K_(task_id), K_(parallelism), K_(execution_id), K_(tablet_task_id), K_(data_format_version),
    K_(dest_schema_version), K_(lob_col_idxs));
public:
  ObTabletID source_tablet_id_;
  ObTabletID dest_tablet_id_;
  int64_t source_table_id_;
  int64_t dest_schema_id_;
  int64_t schema_version_;
  int64_t snapshot_version_;
  int64_t ddl_type_;
  int64_t task_id_;
  int64_t parallelism_;
  int64_t execution_id_;
  int64_t tablet_task_id_;
  uint64_t data_format_version_;
  int64_t dest_schema_version_;
  ObSArray<uint64_t> lob_col_idxs_;
};

struct ObDDLLocalBuildResult final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLLocalBuildResult()
    : ret_code_(OB_SUCCESS), row_inserted_(0), row_scanned_(0), physical_row_count_(0)
  {}
  ~ObDDLLocalBuildResult() = default;
  TO_STRING_KV(K_(ret_code), K_(row_inserted), K_(row_scanned), K_(physical_row_count))
public:
  int64_t ret_code_;
  int64_t row_inserted_;
  int64_t row_scanned_;
  int64_t physical_row_count_;
};

}  // namespace obcall
}  // namespace oceanbase
#endif
