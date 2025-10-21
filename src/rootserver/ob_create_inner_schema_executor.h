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

#ifndef OB_CREATE_INNER_SCHEMA_EXECUTOR_H_
#define OB_CREATE_INNER_SCHEMA_EXECUTOR_H_

#include "lib/thread/ob_async_task_queue.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/schema/ob_multi_version_schema_service.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace obrpc
{
class ObCommonRpcProxy;
}
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace rootserver
{
class ObCreateInnerSchemaExecutor;
class ObDDLService;
class ObCreateInnerSchemaTask : public share::ObAsyncTask
{
public:
  explicit ObCreateInnerSchemaTask(ObCreateInnerSchemaExecutor &executor)
    : executor_(&executor)
  {}
  virtual ~ObCreateInnerSchemaTask() = default;
  virtual int64_t get_deep_copy_size() const;
  share::ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const;
  virtual int process() override;
private:
  ObCreateInnerSchemaExecutor *executor_;
};

class ObCreateInnerSchemaExecutor
{
public:
  ObCreateInnerSchemaExecutor();
  ~ObCreateInnerSchemaExecutor() = default;
  int init(share::schema::ObMultiVersionSchemaService &schema_service,
           common::ObMySQLProxy &sql_proxy,
           obrpc::ObCommonRpcProxy &rpc_proxy);
  int execute();
  int can_execute();
  void start();
  int stop();
  static int do_create_inner_schema_by_tenant(
      uint64_t tenant_id,
      oceanbase::lib::Worker::CompatMode compat_mode,
      share::schema::ObSchemaGetterGuard &schema_guard,
      common::ObMySQLProxy *sql_proxy,
      obrpc::ObCommonRpcProxy *rpc_proxy);
private:
  int set_execute_mark();
  int do_create_inner_schema();
  int check_stop();
  private:
  bool is_inited_;
  bool is_stopped_;
  bool execute_;
  common::SpinRWLock rwlock_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  common::ObMySQLProxy *sql_proxy_;
  obrpc::ObCommonRpcProxy *rpc_proxy_;
  DISALLOW_COPY_AND_ASSIGN(ObCreateInnerSchemaExecutor);
};

}  // end namespace rootserver
}  // end namespace oceanbase

#endif  // OB_CREATE_INNER_SCHEMA_EXECUTOR_H_
