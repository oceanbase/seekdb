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

#define USING_LOG_PREFIX SHARE

#include "share/storage/ob_merge_info_table_storage.h"
#include "share/storage/local_store.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_merge_info.h"

namespace oceanbase
{
namespace share
{

ObMergeInfoTableStorage::ObMergeInfoTableStorage()
  : store_(nullptr)
{
}

ObMergeInfoTableStorage::~ObMergeInfoTableStorage()
{
}

int ObMergeInfoTableStorage::init(LocalStore *store)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(store_ = store)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!store_->is_inited()) {
    ret = OB_NOT_INIT;
  }
  if (OB_FAIL(ret)) {
    store_ = nullptr;
  }
  return ret;
}

int ObMergeInfoTableStorage::insert_or_update(const ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto upsert_request = LocalRequest::upsert(merge_info::schema()).set(merge_info::Id, int64_t(0))
        .set(merge_info::FrozenScn, global_merge_info.frozen_scn().get_val_for_inner_table_field())
        .set(merge_info::GlobalBroadcastScn, global_merge_info.global_broadcast_scn().get_val_for_inner_table_field())
        .set(merge_info::IsMergeError, global_merge_info.is_merge_error_.get_value())
        .set(merge_info::LastMergedScn, global_merge_info.last_merged_scn().get_val_for_inner_table_field())
        .set(merge_info::MergeStatus, global_merge_info.merge_status_.get_value())
        .set(merge_info::ErrorType, global_merge_info.error_type_.get_value())
        .set(merge_info::SuspendMerging, global_merge_info.suspend_merging_.get_value())
        .set(merge_info::MergeStartTime, global_merge_info.merge_start_time_.get_value())
        .set(merge_info::LastMergedTime, global_merge_info.last_merged_time_.get_value());

    ret = store_->execute(upsert_request);
  }
  return ret;
}

int ObMergeInfoTableStorage::get(ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  global_merge_info.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request =
      LocalRequest::select(merge_info::schema(), {merge_info::FrozenScn, merge_info::GlobalBroadcastScn, merge_info::IsMergeError, merge_info::LastMergedScn, merge_info::MergeStatus, merge_info::ErrorType, merge_info::SuspendMerging, merge_info::MergeStartTime, merge_info::LastMergedTime}).take(int64_t(1));

    auto row_processor = [&](LocalRowReader &reader) -> int {
      uint64_t frozen_scn_val = reader.get_int64();
      uint64_t global_broadcast_scn_val = reader.get_int64();
      int64_t is_merge_error = reader.get_int64();
      uint64_t last_merged_scn_val = reader.get_int64();
      int64_t merge_status = reader.get_int64();
      int64_t error_type = reader.get_int64();
      int64_t suspend_merging = reader.get_int64();
      int64_t merge_start_time = reader.get_int64();
      int64_t last_merged_time = reader.get_int64();

      global_merge_info.frozen_scn_.set_scn(frozen_scn_val);
      global_merge_info.global_broadcast_scn_.set_scn(global_broadcast_scn_val);
      global_merge_info.is_merge_error_.set_val(is_merge_error, false);
      global_merge_info.last_merged_scn_.set_scn(last_merged_scn_val);
      global_merge_info.merge_status_.set_val(merge_status, false);
      global_merge_info.error_type_.set_val(error_type, false);
      global_merge_info.suspend_merging_.set_val(suspend_merging, false);
      global_merge_info.merge_start_time_.set_val(merge_start_time, false);
      global_merge_info.last_merged_time_.set_val(last_merged_time, false);
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase

