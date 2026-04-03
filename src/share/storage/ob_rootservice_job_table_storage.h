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

#ifndef OCEANBASE_SHARE_STORAGE_OB_ROOTSERVICE_JOB_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_ROOTSERVICE_JOB_TABLE_STORAGE_H_

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
namespace share
{

struct ObRootServiceJobEntry
{
  int64_t gmt_create_;
  int64_t gmt_modified_;
  int64_t job_id_;
  common::ObString job_type_;
  common::ObString job_status_;
  int64_t result_code_;

  ObRootServiceJobEntry()
    : gmt_create_(0),
      gmt_modified_(0),
      job_id_(0),
      job_type_(),
      job_status_(),
      result_code_(0)
  {}

  void reset()
  {
    gmt_create_ = 0;
    gmt_modified_ = 0;
    job_id_ = 0;
    job_type_.reset();
    job_status_.reset();
    result_code_ = 0;
  }
  TO_STRING_EMPTY();
};

class ObRootServiceJobTableStorage
{
public:
  ObRootServiceJobTableStorage();
  virtual ~ObRootServiceJobTableStorage();

  int init(ObSQLiteConnectionPool *pool);
  bool is_inited() const { return nullptr != pool_; }

  int create_job(const ObRootServiceJobEntry &entry);
  int complete_job(const int64_t job_id, const common::ObString &job_status, const int64_t result_code);
  int find_job(const common::ObString &job_type, int64_t &job_id);
  int get_job_count(const common::ObString &job_type, int64_t &job_count);
  int get_max_job_id(int64_t &max_job_id);

private:
  int create_table_if_not_exists_();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObRootServiceJobTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_ROOTSERVICE_JOB_TABLE_STORAGE_H_
