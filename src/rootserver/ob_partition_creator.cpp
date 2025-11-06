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

#define USING_LOG_PREFIX BOOTSTRAP

#include "rootserver/ob_partition_creator.h"
#include "rootserver/ob_bootstrap.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_define.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace rootserver
{

ObPartitionCreator::ObPartitionCreator()
    : bootstrap_(nullptr),
      table_schemas_(nullptr),
      task_submitted_(false),
      task_completed_(false),
      task_result_(OB_SUCCESS)
{
}

ObPartitionCreator::~ObPartitionCreator()
{
  destroy();
}

int ObPartitionCreator::init(ObBootstrap* bootstrap, common::ObIArray<share::schema::ObTableSchema>* table_schemas)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(bootstrap) || OB_ISNULL(table_schemas)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("bootstrap or table_schemas is null", K(ret), KP(bootstrap), KP(table_schemas));
  } else if (OB_FAIL(set_thread_count(get_thread_count()))) {
    LOG_WARN("failed to set thread count", K(ret));
  } else if (OB_FAIL(start())) {
    LOG_WARN("failed to start partition creator", K(ret));
  } else {
    bootstrap_ = bootstrap;
    table_schemas_ = table_schemas;
    LOG_INFO("ObPartitionCreator init success", "table_schemas_count", table_schemas_->count());
  }

  return ret;
}

void ObPartitionCreator::destroy()
{
  stop();
  wait();

  bootstrap_ = nullptr;
  table_schemas_ = nullptr;
  task_submitted_ = false;
  task_completed_ = false;
  task_result_ = OB_SUCCESS;
  LOG_INFO("ObPartitionCreator destroyed");
}

int ObPartitionCreator::submit_create_partitions_task()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(bootstrap_) || OB_ISNULL(table_schemas_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("bootstrap or table_schemas is null", K(ret), KP(bootstrap_), KP(table_schemas_));
  } else if (task_submitted_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task already submitted", K(ret));
  } else {
    task_submitted_ = true;
    task_completed_ = false;
    task_result_ = OB_SUCCESS;
  }

  return ret;
}

int ObPartitionCreator::wait_task_completion(int& ret)
{
  int wait_ret = OB_SUCCESS;

  if (!task_submitted_) {
    wait_ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no task submitted", K(wait_ret));
  } else {
    while (!task_completed_ && !has_set_stop()) {
      lib::Thread::update_loop_ts();
      ob_usleep(10 * 1000); // 10ms
    }

    if (task_completed_) {
      ret = task_result_;
      LOG_INFO("task completed", K(ret));
    } else {
      wait_ret = OB_TIMEOUT;
      LOG_WARN("wait task completion timeout", K(wait_ret));
    }
  }

  return wait_ret;
}

bool ObPartitionCreator::is_task_completed() const
{
  return task_completed_;
}

void ObPartitionCreator::run(int64_t idx)
{
  UNUSED(idx);
  int ret = OB_SUCCESS;
  LOG_INFO("ObPartitionCreator started", K(idx));
  while (!has_set_stop()) {
    if (task_submitted_ && !task_completed_) {
      if (OB_FAIL(process_create_partitions_task())) {
        LOG_WARN("failed to process create partitions task", K(ret));
        task_result_ = ret;
      } else {
        LOG_INFO("create partitions task executed successfully");
      }
      task_completed_ = true;
      break;
    } else {
      ob_usleep(100 * 1000); // 100ms
    }
  }
  LOG_INFO("ObPartitionCreator stopped", K(idx));
}

int ObPartitionCreator::process_create_partitions_task()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(bootstrap_) || OB_ISNULL(table_schemas_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("bootstrap or table_schemas is null", K(ret), KP(bootstrap_), KP(table_schemas_));
  } else if (OB_FAIL(bootstrap_->create_sys_table_partitions(*table_schemas_))) {
    LOG_WARN("create partitions failed", K(ret));
  } else {
    LOG_INFO("create partitions successfully");
  }

  return ret;
}

} // end namespace rootserver
} // end namespace oceanbase
