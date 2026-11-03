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

#include "share/ob_column_checksum_error_operator.h"
#include "share/storage/ob_column_checksum_error_info_table_storage.h"
#include "share/storage/local_store.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

// Static storage instance
ObColumnChecksumErrorInfoTableStorage ObColumnChecksumErrorOperator::storage_;
LocalStore *ObColumnChecksumErrorOperator::local_store_ = nullptr;

int ObColumnChecksumErrorOperator::init(LocalStore &store)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(storage_.init(&store))) {
  } else {
    local_store_ = &store;
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////

bool ObColumnChecksumErrorInfo::is_valid() const
{
  return (true) && (frozen_scn_.is_valid())
         && (data_table_id_ != OB_INVALID_ID) && (index_table_id_ != OB_INVALID_ID);
}

///////////////////////////////////////////////////////////////////////////////

int ObColumnChecksumErrorOperator::insert_column_checksum_err_info(
    ObISQLClient &sql_client,
    const ObColumnChecksumErrorInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    ret = storage_.insert(info);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObColumnChecksumErrorOperator::delete_column_checksum_err_info(
    ObISQLClient &sql_client,
    const SCN &min_frozen_scn)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!min_frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = storage_.delete_expired(min_frozen_scn, INT64_MAX);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObColumnChecksumErrorOperator::delete_column_checksum_err_info_by_scn(
    common::ObISQLClient &sql_client,
    const int64_t compaction_scn)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(compaction_scn <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // Use local storage - delete by exact frozen_scn

    const auto delete_request = LocalRequest::erase(checksum_error::schema())
        .eq(checksum_error::FrozenScn, compaction_scn);

    ret = local_store_->execute(delete_request);
  }
  return ret;
}

int ObColumnChecksumErrorOperator::check_exist_ckm_error_table(const int64_t compaction_scn, bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (OB_UNLIKELY(compaction_scn <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request = LocalRequest::select(checksum_error::schema(), {})
        .aggregate(local::Aggregate::Count)
        .eq(checksum_error::FrozenScn, compaction_scn);

    int64_t count = 0;
    auto row_processor = [&](LocalRowReader &reader) -> int {
      count = reader.get_int64();
      return OB_SUCCESS;
    };

    if (OB_FAIL(local_store_->query(select_request, row_processor))) {
    } else if (count > 0) {
      exist = true;
      LOG_INFO("exist ckm error info", K(count), K(compaction_scn));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
