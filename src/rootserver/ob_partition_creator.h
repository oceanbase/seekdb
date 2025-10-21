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

#ifndef OCEANBASE_ROOTSERVER_OB_PARTITION_CREATOR_H_
#define OCEANBASE_ROOTSERVER_OB_PARTITION_CREATOR_H_

#include "lib/thread/thread_pool.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_define.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase {
namespace rootserver {
class ObBootstrap;
}
}

namespace oceanbase
{
namespace rootserver
{

class ObPartitionCreator : public lib::ThreadPool
{
public:
  ObPartitionCreator();
  virtual ~ObPartitionCreator();

  int init(ObBootstrap* bootstrap, common::ObIArray<share::schema::ObTableSchema>* table_schemas);
  void destroy();

  int submit_create_partitions_task();

  int wait_task_completion(int& ret);

  bool is_task_completed() const;

  static int64_t get_thread_count() { return 1; }

protected:
  virtual void run(int64_t idx) override;

private:
  int process_create_partitions_task();

private:
  ObBootstrap* bootstrap_;
  common::ObIArray<share::schema::ObTableSchema>* table_schemas_;
  bool task_submitted_;
  bool task_completed_;
  int task_result_;

  DISALLOW_COPY_AND_ASSIGN(ObPartitionCreator);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_PARTITION_CREATOR_H_
