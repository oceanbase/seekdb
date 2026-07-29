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

#ifndef OCEANBASE_ROOTSERVER_OB_ADMIN_JOB_TABLE_OPERATOR_H_
#define OCEANBASE_ROOTSERVER_OB_ADMIN_JOB_TABLE_OPERATOR_H_

#include "lib/net/ob_addr.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/storage/ob_admin_job_table_storage.h"
#include "rootserver/ob_admin_job_type.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
namespace sqlclient
{
class ObMySQLResult;
}
}
namespace rootserver
{
enum ObAdminJobStatus
{
  JOB_STATUS_INVALID = 0,
  JOB_STATUS_INPROGRESS,
  JOB_STATUS_SUCCESS,
  JOB_STATUS_FAILED,
  JOB_STATUS_SKIP_CHECKING_LS_STATUS,
  JOB_STATUS_MAX
};

class ObAdminJobTableOperator
{
public:
  static const char* get_job_type_str(ObAdminJobType job_type);
  static ObAdminJobType get_job_type(const common::ObString &job_type_str);
  static ObAdminJobStatus get_job_status(const common::ObString &job_status_str);
  static bool is_valid_job_type(const ObAdminJobType &admin_job_type);
public:
  ObAdminJobTableOperator();
  virtual ~ObAdminJobTableOperator() = default;
  int init();

  // create a new job with the specified properties
  // @return job_id will be -1 on error
  int create_job(ObAdminJobType job_type, int64_t &job_id);
  int find_job(const ObAdminJobType job_type, int64_t &job_id);
  int complete_job(int64_t job_id, int result_code);
  int get_job_count(const ObAdminJobType job_type, int64_t &job_count);

  // misc
  int64_t get_max_job_id() const { return max_job_id_; }
  void reset_max_job_id() { max_job_id_ = -1; }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObAdminJobTableOperator);
  // function members
  int alloc_job_id(int64_t &job_id);
  int load_max_job_id(int64_t &max_job_id);
private:
  // data members
  bool inited_;
  int64_t max_job_id_;
  common::ObLatch latch_;
  share::ObAdminJobTableStorage storage_;
};

class ObAdminJobTableOperatorSingleton
{
public:
  static ObAdminJobTableOperator &get_instance();
};

} // end namespace rootserver
} // end namespace oceanbase

#define THE_ADMIN_JOB_TABLE ::oceanbase::rootserver::ObAdminJobTableOperatorSingleton::get_instance()

#define ADMIN_JOB_CREATE_WITH_RET(job_id, job_type)           \
  ({                                                          \
    int tmp_ret = ::oceanbase::common::OB_SUCCESS;            \
    job_id = ::oceanbase::common::OB_INVALID_ID;              \
    tmp_ret = THE_ADMIN_JOB_TABLE.create_job(job_type, job_id); \
    tmp_ret;                                                  \
  })

#define ADMIN_JOB_COMPLETE(job_id, result_code)               \
  THE_ADMIN_JOB_TABLE.complete_job(job_id, result_code)

#define ADMIN_JOB_FIND(job_type, job_id)                                 \
  ({                                                                     \
    int tmp_ret = ::oceanbase::common::OB_SUCCESS;                       \
    tmp_ret = THE_ADMIN_JOB_TABLE.find_job(JOB_TYPE_ ## job_type, job_id); \
    tmp_ret;                                                             \
  })

#define GET_ADMIN_JOB_COUNT(job_type, job_count)                                 \
  ({                                                                             \
    int tmp_ret = ::oceanbase::common::OB_SUCCESS;                               \
    tmp_ret = THE_ADMIN_JOB_TABLE.get_job_count(JOB_TYPE_ ## job_type, job_count); \
    tmp_ret;                                                                     \
  })

#endif /* OCEANBASE_ROOTSERVER_OB_ADMIN_JOB_TABLE_OPERATOR_H_ */
