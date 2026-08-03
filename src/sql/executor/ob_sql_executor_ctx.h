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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_SQL_EXECUTOR_CTX_H_
#define OCEANBASE_SQL_EXECUTOR_OB_SQL_EXECUTOR_CTX_H_

#include "sql/executor/ob_execute_result.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
} // namespace schema
} // namespace share

namespace sql
{

class ObSqlExecutorCtx
{
  OB_UNIS_VERSION(1);
public:
  ObSqlExecutorCtx();
  virtual ~ObSqlExecutorCtx();

  inline ObExecuteResult &get_execute_result()
  {
    return execute_result_;
  }
  inline void set_query_begin_schema_version(const int64_t schema_version)
  {
    query_begin_schema_version_ = schema_version;
  }
  inline int64_t get_query_begin_schema_version() const
  {
    return query_begin_schema_version_;
  }
  inline void set_retry_times(int64_t retry_times)
  {
    retry_times_ = retry_times;
  }
  inline int64_t get_retry_times() const
  {
    return retry_times_;
  }
  void set_expected_worker_cnt(int64_t cnt) { expected_worker_cnt_ = cnt; }
  int64_t get_expected_worker_cnt() const { return expected_worker_cnt_; }
  void set_minimal_worker_cnt(int64_t cnt) { minimal_worker_cnt_ = cnt; }
  int64_t get_minimal_worker_cnt() const { return minimal_worker_cnt_; }
  void set_admited_worker_cnt(int64_t cnt) { admited_worker_cnt_ = cnt; } // alias
  int64_t get_admited_worker_cnt() const { return admited_worker_cnt_; } // alias
private:
  // Holds the root operator result returned by the executor.
  ObExecuteResult execute_result_;
  // PX records the expected number of threads required for the entire Query, as well as the actual number of threads allocated
  int64_t expected_worker_cnt_; // query expected worker count computed by optimizer
  int64_t minimal_worker_cnt_;  // minimal worker count to support execute this query
  int64_t admited_worker_cnt_; // query final used worker count admitted by admission
  // The number of retries
  int64_t retry_times_;
public:
  int64_t query_begin_schema_version_; // Latest global database schema version at query start
  share::schema::ObMultiVersionSchemaService *schema_service_;


  DISALLOW_COPY_AND_ASSIGN(ObSqlExecutorCtx);
  TO_STRING_KV(K(retry_times_), K(expected_worker_cnt_),
      K(admited_worker_cnt_), K(query_begin_schema_version_),
      K(minimal_worker_cnt_));
};

}
}
#endif // OCEANBASE_SQL_EXECUTOR_OB_SQL_EXECUTOR_CTX_H_
//// end of header file
