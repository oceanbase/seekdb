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

#ifndef _STORAGE_DDL_OB_DDL_DAG_THREAD_POOL_
#define _STORAGE_DDL_OB_DDL_DAG_THREAD_POOL_

#include "share/ob_thread_pool.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}

namespace storage
{
class ObDDLIndependentDag;

class ObDDLDagThreadPool : public share::ObThreadPool
{
public:
  ObDDLDagThreadPool() : is_inited_(false), ddl_dag_(nullptr), session_info_(nullptr) {}
  int init(const int64_t thread_count, ObDDLIndependentDag *ddl_dag, sql::ObSQLSessionInfo *session_info);
  virtual void run1() override;

private:
  bool is_inited_;
  ObDDLIndependentDag *ddl_dag_;
  sql::ObSQLSessionInfo *session_info_;
};


}// namespace storage
}// namespace oceanbase

#endif//_STORAGE_DDL_OB_DDL_DAG_THREAD_POOL_
