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

#ifndef _OB_RS_JOB_TABLE_OPERATOR_H
#define _OB_RS_JOB_TABLE_OPERATOR_H 1

#include "lib/net/ob_addr.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/storage/ob_rootservice_job_table_storage.h"

namespace oceanbase
{
//namespace share
//{
//class ObRootServiceJobTableStorage;
//}
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
// @note modify ObRsJobTableOperator::job_type_str if you modify ObRsJobType
enum ObRsJobType
{
  JOB_TYPE_INVALID = 0,
  JOB_TYPE_ALTER_TENANT_LOCALITY,
  JOB_TYPE_ROLLBACK_ALTER_TENANT_LOCALITY, // deprecated in V4.2
  JOB_TYPE_MIGRATE_UNIT,
  JOB_TYPE_DELETE_SERVER,
  JOB_TYPE_SHRINK_RESOURCE_TENANT_UNIT_NUM, // deprecated in V4.2
  JOB_TYPE_RESTORE_TENANT,
  JOB_TYPE_UPGRADE_STORAGE_FORMAT_VERSION,
  JOB_TYPE_STOP_UPGRADE_STORAGE_FORMAT_VERSION,
  JOB_TYPE_CREATE_INNER_SCHEMA,
  JOB_TYPE_UPGRADE_POST_ACTION,
  JOB_TYPE_UPGRADE_SYSTEM_VARIABLE,
  JOB_TYPE_UPGRADE_SYSTEM_TABLE,
  JOB_TYPE_UPGRADE_BEGIN,
  JOB_TYPE_UPGRADE_VIRTUAL_SCHEMA,
  JOB_TYPE_UPGRADE_SYSTEM_PACKAGE,
  JOB_TYPE_UPGRADE_ALL_POST_ACTION,
  JOB_TYPE_UPGRADE_INSPECTION,
  JOB_TYPE_UPGRADE_END,
  JOB_TYPE_UPGRADE_ALL,
  JOB_TYPE_ALTER_RESOURCE_TENANT_UNIT_NUM,
  JOB_TYPE_ALTER_TENANT_PRIMARY_ZONE,
  JOB_TYPE_UPGRADE_FINISH,
  JOB_TYPE_LOAD_MYSQL_SYS_PACKAGE,
  JOB_TYPE_LOAD_ORACLE_SYS_PACKAGE,
  JOB_TYPE_MAX
};

enum ObRsJobStatus
{
  JOB_STATUS_INVALID = 0,
  JOB_STATUS_INPROGRESS,
  JOB_STATUS_SUCCESS,
  JOB_STATUS_FAILED,
  JOB_STATUS_SKIP_CHECKING_LS_STATUS,
  JOB_STATUS_MAX
};

class ObRsJobTableOperator
{
public:
  static const char* get_job_type_str(ObRsJobType job_type);
  static ObRsJobType get_job_type(const common::ObString &job_type_str);
  static ObRsJobStatus get_job_status(const common::ObString &job_status_str);
  static bool is_valid_job_type(const ObRsJobType &rs_job_type);
public:
  ObRsJobTableOperator();
  virtual ~ObRsJobTableOperator() = default;
  int init();

  // create a new job with the specified properties
  // @return job_id will be -1 on error
  int create_job(ObRsJobType job_type, int64_t &job_id);
  int find_job(const ObRsJobType job_type, int64_t &job_id);
  int complete_job(int64_t job_id, int result_code);
  int get_job_count(const ObRsJobType job_type, int64_t &job_count);

  // misc
  int64_t get_max_job_id() const { return max_job_id_; }
  void reset_max_job_id() { max_job_id_ = -1; }
private:
  // types and constants
  static const char* const TABLE_NAME;
  static const int64_t MAX_ROW_COUNT = 100000;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRsJobTableOperator);
  // function members
  int alloc_job_id(int64_t &job_id);
  int load_max_job_id(int64_t &max_job_id);
private:
  // data members
  bool inited_;
  int64_t max_job_id_;
  common::ObLatch latch_;
  share::ObRootServiceJobTableStorage storage_;
};

class ObRsJobTableOperatorSingleton
{
public:
  static ObRsJobTableOperator &get_instance();
};

} // end namespace rootserver
} // end namespace oceanbase

#define THE_RS_JOB_TABLE ::oceanbase::rootserver::ObRsJobTableOperatorSingleton::get_instance()

#define RS_JOB_CREATE_WITH_RET(job_id, job_type)              \
  ({                                                          \
    int tmp_ret = ::oceanbase::common::OB_SUCCESS;            \
    job_id = ::oceanbase::common::OB_INVALID_ID;              \
    tmp_ret = THE_RS_JOB_TABLE.create_job(job_type, job_id);  \
    tmp_ret;                                                  \
  })

#define RS_JOB_COMPLETE(job_id, result_code)                  \
  THE_RS_JOB_TABLE.complete_job(job_id, result_code)

#define RS_JOB_FIND(job_type, job_id)                                    \
  ({                                                                     \
    int tmp_ret = ::oceanbase::common::OB_SUCCESS;                       \
    tmp_ret = THE_RS_JOB_TABLE.find_job(JOB_TYPE_ ## job_type, job_id);  \
    tmp_ret;                                                             \
  })

#define GET_RS_JOB_COUNT(job_type, job_count)                                    \
  ({                                                                             \
    int tmp_ret = ::oceanbase::common::OB_SUCCESS;                               \
    tmp_ret = THE_RS_JOB_TABLE.get_job_count(JOB_TYPE_ ## job_type, job_count);  \
    tmp_ret;                                                                     \
  })

#endif /* _OB_RS_JOB_TABLE_OPERATOR_H */
