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

#include "share/storage/ob_rootservice_job_table_storage.h"

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
class ObRsJobTableOperator
{
public:
  ObRsJobTableOperator();
  virtual ~ObRsJobTableOperator() = default;
  int init();

  int create_system_package_load_job(int64_t &job_id);
  int find_system_package_load_job(int64_t &job_id);
  int complete_system_package_load_job(int64_t job_id, int result_code);
  int get_system_package_load_job_count(int64_t &job_count);

  // misc
  int64_t get_max_job_id() const { return max_job_id_; }
  void reset_max_job_id() { max_job_id_ = -1; }
private:
  static const char* const SYSTEM_PACKAGE_LOAD_JOB_TYPE;
  static const char* const JOB_STATUS_INPROGRESS;
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
  share::ObLocalManagementServiceJobTableStorage storage_;
};

class ObRsJobTableOperatorSingleton
{
public:
  static ObRsJobTableOperator &get_instance();
};

} // end namespace rootserver
} // end namespace oceanbase

#define THE_RS_JOB_TABLE ::oceanbase::rootserver::ObRsJobTableOperatorSingleton::get_instance()

#endif /* _OB_RS_JOB_TABLE_OPERATOR_H */
