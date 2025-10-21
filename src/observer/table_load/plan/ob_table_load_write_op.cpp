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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/plan/ob_table_load_write_op.h"
#include "observer/table_load/dag/ob_table_load_dag_direct_write.h"
#include "observer/table_load/dag/ob_table_load_dag_pre_sort_write.h"
#include "observer/table_load/dag/ob_table_load_dag_store_write.h"

namespace oceanbase
{
namespace observer
{
int ObTableLoadWriteOp::build(ObTableLoadTableOp *table_op,
                              const ObTableLoadWriteType::Type write_type,
                              ObTableLoadWriteOp *&write_op)
{
  int ret = OB_SUCCESS;
  write_op = nullptr;
  switch (write_type) {
    case ObTableLoadWriteType::DIRECT_WRITE: {
      ObTableLoadDirectWriteOp *direct_write_op = nullptr;
      if (OB_FAIL(table_op->alloc_op(direct_write_op))) {
        LOG_WARN("fail to alloc op", KR(ret));
      } else {
        write_op = direct_write_op;
      }
      break;
    }
    case ObTableLoadWriteType::STORE_WRITE: {
      ObTableLoadStoreWriteOp *store_write_op = nullptr;
      if (OB_FAIL(table_op->alloc_op(store_write_op))) {
        LOG_WARN("fail to alloc op", KR(ret));
      } else {
        write_op = store_write_op;
      }
      break;
    }
    case ObTableLoadWriteType::PRE_SORT_WRITE: {
      ObTableLoadPreSortWriteOp *pre_sort_write_op = nullptr;
      if (OB_FAIL(table_op->alloc_op(pre_sort_write_op))) {
        LOG_WARN("fail to alloc op", KR(ret));
      } else {
        write_op = pre_sort_write_op;
      }
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected write type", KR(ret), K(write_type));
      break;
  }
  return ret;
}

// direct_write
ObTableLoadDirectWriteOp::ObTableLoadDirectWriteOp(ObTableLoadTableBaseOp *parent)
  : ObTableLoadWriteOp(parent), write_channel_(nullptr)
{
  op_type_ = ObTableLoadOpType::DIRECT_WRITE_OP;
}

ObTableLoadDirectWriteOp::~ObTableLoadDirectWriteOp()
{
  ObTableLoadDirectWriteOpFinishTask::reset_op(this);
}

// store_write
ObTableLoadStoreWriteOp::ObTableLoadStoreWriteOp(ObTableLoadTableBaseOp *parent)
  : ObTableLoadWriteOp(parent), write_channel_(nullptr)
{
  op_type_ = ObTableLoadOpType::STORE_WRITE_OP;
}

ObTableLoadStoreWriteOp::~ObTableLoadStoreWriteOp()
{
  ObTableLoadStoreWriteOpFinishTask::reset_op(this);
}

// pre_sort_write
ObTableLoadPreSortWriteOp::ObTableLoadPreSortWriteOp(ObTableLoadTableBaseOp *parent)
  : ObTableLoadWriteOp(parent), write_channel_(nullptr)
{
  op_type_ = ObTableLoadOpType::PRE_SORT_WRITE_OP;
}

ObTableLoadPreSortWriteOp::~ObTableLoadPreSortWriteOp()
{
  ObTableLoadPreSortWriteOpFinishTask::reset_op(this);
}

} // namespace observer
} // namespace oceanbase
