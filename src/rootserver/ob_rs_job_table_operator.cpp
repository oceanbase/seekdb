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
#include "share/storage/ob_rootservice_job_table_storage.h"
#include "observer/ob_server_struct.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::rootserver;

const char* const ObRsJobTableOperator::SYSTEM_PACKAGE_LOAD_JOB_TYPE = "LOAD_SYSTEM_PACKAGE";
const char* const ObRsJobTableOperator::JOB_STATUS_INPROGRESS = "INPROGRESS";


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

int ObRsJobTableOperator::create_system_package_load_job(int64_t &job_id)
{
  int ret = OB_SUCCESS;
  job_id = OB_INVALID_ID;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(alloc_job_id(job_id))) {
    LOG_WARN("failed to alloc job id", K(ret), K(job_id));
  } else {
    share::ObLocalManagementServiceJobEntry entry;
    entry.job_id_ = job_id;
    entry.job_type_ = common::ObString::make_string(SYSTEM_PACKAGE_LOAD_JOB_TYPE);
    entry.job_status_ = common::ObString::make_string(JOB_STATUS_INPROGRESS);
    entry.result_code_ = 0;
    if (OB_FAIL(storage_.create_job(entry))) {
      LOG_WARN("failed to create rs job in sqlite", K(ret));
    } else {
      LOG_INFO("rootservice job started", K(job_id), K(entry));
    }
  }
  return ret;
}

int ObRsJobTableOperator::find_system_package_load_job(int64_t &job_id)
{
  int ret = OB_SUCCESS;
  job_id = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(storage_.find_job(
                 common::ObString::make_string(SYSTEM_PACKAGE_LOAD_JOB_TYPE), job_id))) {
    LOG_WARN("fail to find system package load job", KR(ret));
  }
  return ret;
}

int ObRsJobTableOperator::get_system_package_load_job_count(int64_t &job_count)
{
  int ret = OB_SUCCESS;
  job_count = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(storage_.get_job_count(
                 common::ObString::make_string(SYSTEM_PACKAGE_LOAD_JOB_TYPE), job_count))) {
    LOG_WARN("fail to count system package load jobs", KR(ret));
  }
  return ret;
}

int ObRsJobTableOperator::complete_system_package_load_job(int64_t job_id, int result_code)
{
  int ret = OB_SUCCESS;
  const char *status_str = OB_SUCCESS == result_code ? "SUCCESS" : "FAILED";
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
