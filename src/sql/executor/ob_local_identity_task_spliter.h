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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_LOCAL_IDENTITY_TASK_SPLITER_
#define OCEANBASE_SQL_EXECUTOR_OB_LOCAL_IDENTITY_TASK_SPLITER_

#include "sql/executor/ob_task_spliter.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlan;
class ObTaskInfo;
// This class is only used for generating a single local task, and during the executor stage if it is determined that
// Split type is ObTaskSpliter::LOCAL_IDENTITY_SPLIT will be directly optimized out, not going through the split job process,
// Equivalent to most functions of this class not being called
class ObLocalIdentityTaskSpliter : public ObTaskSpliter
{
public:
  ObLocalIdentityTaskSpliter();
  virtual ~ObLocalIdentityTaskSpliter();
  virtual int get_next_task(ObTaskInfo *&task);
  inline virtual TaskSplitType get_type() const { return ObTaskSpliter::LOCAL_IDENTITY_SPLIT; }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObLocalIdentityTaskSpliter);
private:
  ObTaskInfo *task_;
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_LOCAL_IDENTITY_TASK_SPLITER_ */
//// end of header file

