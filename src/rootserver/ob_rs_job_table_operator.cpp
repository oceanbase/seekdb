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
#include "ob_rs_job_table_operator.h"
#include "share/ob_upgrade_utils.h"
#include "share/storage/ob_rootservice_job_table_storage.h"
#include "observer/ob_server_struct.h"
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

const char* const ObRsJobTableOperator::TABLE_NAME = "__all_rootservice_job";

static const char* job_type_str_array[JOB_TYPE_MAX] = {
  NULL,
  "ALTER_TENANT_LOCALITY",
  "ROLLBACK_ALTER_TENANT_LOCALITY", // deprecated in V4.2
  "MIGRATE_UNIT",
  "DELETE_SERVER",
  "SHRINK_RESOURCE_TENANT_UNIT_NUM", // deprecated in V4.2
  "RESTORE_TENANT",
  "UPGRADE_STORAGE_FORMAT_VERSION",
  "STOP_UPGRADE_STORAGE_FORMAT_VERSION",
  "CREATE_INNER_SCHEMA",
  "UPGRADE_POST_ACTION",
  "UPGRADE_SYSTEM_VARIABLE",
  "UPGRADE_SYSTEM_TABLE",
  "UPGRADE_BEGIN",
  "UPGRADE_VIRTUAL_SCHEMA",
  "UPGRADE_SYSTEM_PACKAGE",
  "UPGRADE_ALL_POST_ACTION",
  "UPGRADE_INSPECTION",
  "UPGRADE_END",
  "UPGRADE_ALL",
  "ALTER_RESOURCE_TENANT_UNIT_NUM",
  "ALTER_TENANT_PRIMARY_ZONE",
  "UPGRADE_FINISH",
  "LOAD_MYSQL_SYS_PACKAGE",
  "LOAD_ORACLE_SYS_PACKAGE",
};

bool ObRsJobTableOperator::is_valid_job_type(const ObRsJobType &rs_job_type)
{
  return rs_job_type > ObRsJobType::JOB_TYPE_INVALID && rs_job_type < ObRsJobType::JOB_TYPE_MAX;
}

const char* ObRsJobTableOperator::get_job_type_str(ObRsJobType job_type)
{
  STATIC_ASSERT(ARRAYSIZEOF(job_type_str_array) == JOB_TYPE_MAX,
                "type string array size mismatch with enum ObRsJobType");

  const char* str = NULL;
  if (is_valid_job_type(job_type)) {
    str = job_type_str_array[job_type];
  }
  return str;
}

ObRsJobType ObRsJobTableOperator::get_job_type(const common::ObString &job_type_str)
{
  ObRsJobType ret_job_type = JOB_TYPE_INVALID;
  for (int i = 0; i < static_cast<int>(JOB_TYPE_MAX); ++i) {
    if (NULL != job_type_str_array[i]
        && 0 == job_type_str.case_compare(job_type_str_array[i])) {
      ret_job_type = static_cast<ObRsJobType>(i);
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

ObRsJobStatus ObRsJobTableOperator::get_job_status(const common::ObString &job_status_str)
{
  ObRsJobStatus ret_job_status = JOB_STATUS_INVALID;
  for (int i = 0; i < static_cast<int>(JOB_STATUS_MAX); ++i) {
    if (NULL != job_status_str_array[i]
        && 0 == job_status_str.case_compare(job_status_str_array[i])) {
      ret_job_status = static_cast<ObRsJobStatus>(i);
      break;
    }
  }
  return ret_job_status;
}


ObRsJobTableOperator::ObRsJobTableOperator()
    :inited_(false),
     max_job_id_(-1)
{}

int ObRsJobTableOperator::init()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), KP(GCTX.meta_db_pool_));
  } else if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K(inited_));
  } else if (OB_FAIL(storage_.init(GCTX.meta_db_pool_))) {
    LOG_WARN("fail to init storage", KR(ret));
  } else {
    inited_ = true;
    LOG_INFO("__all_rootservice_job table operator inited");
  }
  return ret;
}

int ObRsJobTableOperator::create_job(ObRsJobType job_type, int64_t &job_id)
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
    LOG_WARN("failed to alloc job id", K(ret), K(job_id));
  } else {
    const int64_t now = ObTimeUtility::current_time();
    share::ObRootServiceJobEntry entry;
    entry.job_id_ = job_id;
    entry.job_type_ = common::ObString::make_string(job_type_str);
    entry.job_status_ = common::ObString::make_string(job_status_str_array[JOB_STATUS_INPROGRESS]);
    entry.result_code_ = 0;
    if (OB_FAIL(storage_.create_job(entry))) {
      LOG_WARN("failed to create rs job in sqlite", K(ret));
    } else {
      LOG_INFO("rootservice job started", K(job_id), K(entry));
    }
  }
  return ret;
}

int ObRsJobTableOperator::find_job(
    const ObRsJobType job_type,
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
    LOG_WARN("fail to find rootservice job", KR(ret), K(job_type));
  }
  return ret;
}

int ObRsJobTableOperator::get_job_count(
    const ObRsJobType job_type,
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
    LOG_WARN("fail to find rootservice job", KR(ret), K(job_type));
  }
  return ret;
}

int ObRsJobTableOperator::complete_job(int64_t job_id, int result_code)
{
  int ret = OB_SUCCESS;
  const char *status_str = get_job_status_str_(result_code);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K(inited_));
  } else if (OB_FAIL(storage_.complete_job(job_id, common::ObString::make_string(status_str), result_code))) {
    LOG_WARN("failed to complete rs job in sqlite", K(ret), K(job_id), K(result_code));
  }
  return ret;
}

int ObRsJobTableOperator::load_max_job_id(int64_t &max_job_id)
{
  int ret = OB_SUCCESS;
  max_job_id = -1;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(storage_.get_max_job_id(max_job_id))) {
    LOG_WARN("fail to load max job id and row count", KR(ret));
  }
  return ret;
}

int ObRsJobTableOperator::alloc_job_id(int64_t &job_id)
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

ObRsJobTableOperator &ObRsJobTableOperatorSingleton::get_instance()
{
  static ObRsJobTableOperator the_one;
  return the_one;
}
