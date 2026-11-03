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

#include "share/storage/ob_admin_job_table_storage.h"
#include "share/storage/local_store.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace share
{

ObAdminJobTableStorage::ObAdminJobTableStorage()
  : store_(nullptr)
{
}

ObAdminJobTableStorage::~ObAdminJobTableStorage()
{
}

int ObAdminJobTableStorage::init(LocalStore *store)
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

int ObAdminJobTableStorage::create_job(const ObAdminJobEntry &entry)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t now = ObTimeUtility::current_time();
    const int64_t gmt_create = (entry.gmt_create_ > 0) ? entry.gmt_create_ : now;
    const int64_t gmt_modified = (entry.gmt_modified_ > 0) ? entry.gmt_modified_ : gmt_create;
    const auto insert_request = LocalRequest::insert(admin_job::schema())
        .set(admin_job::GmtCreate, gmt_create)
        .set(admin_job::GmtModified, gmt_modified)
        .set(admin_job::JobId, entry.job_id_)
        .set(admin_job::JobType, LocalValue::text(entry.job_type_.empty() ? "" : entry.job_type_.ptr(), entry.job_type_.length()))
        .set(admin_job::JobStatus, LocalValue::text(entry.job_status_.empty() ? "" : entry.job_status_.ptr(), entry.job_status_.length()))
        .set(admin_job::ResultCode, entry.result_code_);
    ret = store_->execute(insert_request);
  }
  LOG_INFO("finish create job", KR(ret));
  return ret;
}

int ObAdminJobTableStorage::complete_job(
    const int64_t job_id,
    const common::ObString &job_status,
    const int64_t result_code)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (job_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t gmt_modified = ObTimeUtility::current_time();
    const auto update_request = LocalRequest::update(admin_job::schema())
        .set(admin_job::GmtModified, gmt_modified)
        .set(admin_job::JobStatus, LocalValue::text(job_status.empty() ? "" : job_status.ptr(), job_status.length()))
        .set(admin_job::ResultCode, result_code)
        .eq(admin_job::JobId, job_id);
    ret = store_->execute(update_request);
  }
  LOG_INFO("finish complete job", KR(ret), K(job_id), K(job_status), K(result_code));
  return ret;
}

int ObAdminJobTableStorage::get_max_job_id(int64_t &max_job_id)
{
  int ret = OB_SUCCESS;
  max_job_id = -1;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request =
      LocalRequest::select(admin_job::schema(), {admin_job::JobId}).aggregate(local::Aggregate::Max);
    auto row_processor = [&](LocalRowReader &reader) -> int {
      max_job_id = reader.get_int64();
      return OB_ITER_END;
    };
    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        max_job_id = 0;
      } else {
      }
    }
  }
  LOG_INFO("finish get max job id and row count", KR(ret), K(max_job_id));
  return ret;
}

int ObAdminJobTableStorage::get_job_count(const common::ObString &job_type, int64_t &job_count)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(job_type.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto select_request = LocalRequest::select(admin_job::schema(), {})
        .aggregate(local::Aggregate::Count)
        .eq(admin_job::JobType, LocalValue::text(job_type.ptr(), job_type.length()));
    auto row_processor = [&](LocalRowReader &reader) -> int {
      job_count = reader.get_int64();
      return OB_ITER_END;
    };
    if (OB_FAIL(store_->query(select_request, row_processor))) {
    }
  }
  LOG_INFO("finish get job count", KR(ret), K(job_type), K(job_count));
  return ret;
}

int ObAdminJobTableStorage::find_job(const common::ObString &job_type, int64_t &job_id)
{
  int ret = OB_SUCCESS;
  job_id = -1;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(job_type.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto select_request = LocalRequest::select(admin_job::schema(), {admin_job::JobId})
        .eq(admin_job::JobStatus, "INPROGRESS")
        .eq(admin_job::JobType, LocalValue::text(job_type.ptr(), job_type.length()));
    auto row_processor = [&](LocalRowReader &reader) -> int {
      job_id = reader.get_int64();
      return OB_ITER_END;
    };
    if (OB_FAIL(store_->query(select_request, row_processor))) {
    } else if (OB_UNLIKELY(job_id < 1)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  LOG_INFO("finish find job", KR(ret), K(job_id), K(job_type));
  return ret;
}

} // namespace share
} // namespace oceanbase
