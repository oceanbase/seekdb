/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
