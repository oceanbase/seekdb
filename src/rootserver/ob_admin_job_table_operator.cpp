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

#define USING_LOG_PREFIX RS
#include "ob_admin_job_table_operator.h"
#include "share/storage/ob_admin_job_table_storage.h"
#include "share/ob_server_struct.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::rootserver;

namespace {
const char *get_job_status_str_(const int result_code)
{
  const char *status = "FAILED";
  if (OB_SUCCESS == result_code) {
    status = "SUCCESS";
  }
  return status;
}
} // namespace

static const char* job_type_str_array[JOB_TYPE_MAX] = {
  NULL,
  "RESTORE_TENANT",
  "CREATE_INNER_SCHEMA",
  "LOAD_MYSQL_SYS_PACKAGE",
};

bool ObAdminJobTableOperator::is_valid_job_type(const ObAdminJobType &admin_job_type)
{
  return admin_job_type > ObAdminJobType::JOB_TYPE_INVALID && admin_job_type < ObAdminJobType::JOB_TYPE_MAX;
}

const char* ObAdminJobTableOperator::get_job_type_str(ObAdminJobType job_type)
{
  STATIC_ASSERT(ARRAYSIZEOF(job_type_str_array) == JOB_TYPE_MAX,
                "type string array size mismatch with enum ObAdminJobType");

  const char* str = NULL;
  if (is_valid_job_type(job_type)) {
    str = job_type_str_array[job_type];
  }
  return str;
}

ObAdminJobType ObAdminJobTableOperator::get_job_type(const common::ObString &job_type_str)
{
  ObAdminJobType ret_job_type = JOB_TYPE_INVALID;
  for (int i = 0; i < static_cast<int>(JOB_TYPE_MAX); ++i) {
    if (NULL != job_type_str_array[i]
        && 0 == job_type_str.case_compare(job_type_str_array[i])) {
      ret_job_type = static_cast<ObAdminJobType>(i);
      break;
    }
  }
  return ret_job_type;
}

static const char* job_status_str_array[JOB_STATUS_MAX] = {
  NULL,
  "INPROGRESS",
  "SUCCESS",
  "FAILED",
  "SKIP_CHECKING_LS_STATUS",
};

ObAdminJobStatus ObAdminJobTableOperator::get_job_status(const common::ObString &job_status_str)
{
  ObAdminJobStatus ret_job_status = JOB_STATUS_INVALID;
  for (int i = 0; i < static_cast<int>(JOB_STATUS_MAX); ++i) {
    if (NULL != job_status_str_array[i]
        && 0 == job_status_str.case_compare(job_status_str_array[i])) {
      ret_job_status = static_cast<ObAdminJobStatus>(i);
      break;
    }
  }
  return ret_job_status;
}


ObAdminJobTableOperator::ObAdminJobTableOperator()
    :inited_(false),
     max_job_id_(-1)
{}

int ObAdminJobTableOperator::init()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), KP(GCTX.meta_db_pool_));
  } else if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K(inited_));
  } else if (OB_FAIL(storage_.init(GCTX.meta_db_pool_))) {
  } else {
    inited_ = true;
    LOG_INFO("admin job table operator inited");
  }
  return ret;
}

int ObAdminJobTableOperator::create_job(ObAdminJobType job_type, int64_t &job_id)
{
  int ret = OB_SUCCESS;
  const char* job_type_str = NULL;
  if (!is_valid_job_type(job_type)
      || NULL == (job_type_str = get_job_type_str(job_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid job type", K(ret), K(job_type), K(job_type_str));
  } else if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(alloc_job_id(job_id))) {
  } else {
    share::ObAdminJobEntry entry;
    entry.job_id_ = job_id;
    entry.job_type_ = common::ObString::make_string(job_type_str);
    entry.job_status_ = common::ObString::make_string(job_status_str_array[JOB_STATUS_INPROGRESS]);
    entry.result_code_ = 0;
    if (OB_FAIL(storage_.create_job(entry))) {
    } else {
      LOG_INFO("local DDL service job started", K(job_id), K(entry));
    }
  }
  return ret;
}

int ObAdminJobTableOperator::find_job(
    const ObAdminJobType job_type,
    int64_t &job_id)
{
  int ret = OB_SUCCESS;
  const char* job_type_str = NULL;
  job_id = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!is_valid_job_type(job_type)
      || NULL == (job_type_str = get_job_type_str(job_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid job type", K(ret), K(job_type), K(job_type_str));
  } else if (OB_FAIL(storage_.find_job(common::ObString::make_string(job_type_str), job_id))) {
  }
  return ret;
}

int ObAdminJobTableOperator::get_job_count(
    const ObAdminJobType job_type,
    int64_t &job_count)
{
  int ret = OB_SUCCESS;
  const char* job_type_str = NULL;
  job_count = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!is_valid_job_type(job_type)
      || NULL == (job_type_str = get_job_type_str(job_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid job type", K(ret), K(job_type), K(job_type_str));
  } else if (OB_FAIL(storage_.get_job_count(common::ObString::make_string(job_type_str), job_count))) {
  }
  return ret;
}

int ObAdminJobTableOperator::complete_job(int64_t job_id, int result_code)
{
  int ret = OB_SUCCESS;
  const char *status_str = get_job_status_str_(result_code);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K(inited_));
  } else if (OB_FAIL(storage_.complete_job(job_id, common::ObString::make_string(status_str), result_code))) {
  }
  return ret;
}

int ObAdminJobTableOperator::load_max_job_id(int64_t &max_job_id)
{
  int ret = OB_SUCCESS;
  max_job_id = -1;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(storage_.get_max_job_id(max_job_id))) {
  }
  return ret;
}

int ObAdminJobTableOperator::alloc_job_id(int64_t &job_id)
{
  int ret = OB_SUCCESS;
  if (ATOMIC_LOAD(&max_job_id_) < 0) {
    ObLatchWGuard guard(latch_, ObLatchIds::DEFAULT_MUTEX);
    if (max_job_id_ < 0) {
      int64_t max_job_id = 0;
      if (OB_FAIL(load_max_job_id(max_job_id)) || max_job_id < 0) {
        LOG_WARN("failed to load max job id from the table", K(ret), K(max_job_id));
      } else {
        LOG_INFO("load the max job id", K(max_job_id));
        (void)ATOMIC_SET(&max_job_id_, max_job_id);
        job_id = ATOMIC_AAF(&max_job_id_, 1);
      }
    } else {
      job_id = ATOMIC_AAF(&max_job_id_, 1);
    }
  } else {
    job_id = ATOMIC_AAF(&max_job_id_, 1);
  }
  return ret;
}

ObAdminJobTableOperator &ObAdminJobTableOperatorSingleton::get_instance()
{
  static ObAdminJobTableOperator the_one;
  return the_one;
}
