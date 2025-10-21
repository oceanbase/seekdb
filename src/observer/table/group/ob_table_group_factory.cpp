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

#include "ob_table_group_factory.h"
namespace oceanbase
{

namespace table
{
int ObTableGroupOpFactory::alloc(ObTableGroupType op_type, ObITableOp *&op)
{
  int ret = OB_SUCCESS;
  if (op_type < 0 || op_type >= ObTableGroupType::TYPE_MAX) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "invalid op type", K(ret), K(op_type));
  } else {
    ObLockGuard<ObSpinLock> guard(locks_[op_type]);
    op = nullptr;
    op = free_list_[op_type].remove_first();
    if (OB_ISNULL(op)) {
      if (OB_FAIL(GROUP_OP_ALLOC[op_type](allocator_, op))) {
        LOG_WARN("fail to alloc op", K(ret), K(op_type));
      }
    }
    used_list_[op_type].add_last(op);
  }
  return ret;
}

void ObTableGroupOpFactory::free(ObITableOp *op)
{
 if (NULL != op) {
    ObTableGroupType op_type = op->type();
    if (op_type > 0 && op_type < ObTableGroupType::TYPE_MAX) {
      op->reset();
      ObLockGuard<ObSpinLock> guard(locks_[op_type]);
      used_list_[op_type].remove(op);
      free_list_[op_type].add_last(op);
    } else {
      LOG_DEBUG("[ObTableGroupOpFactory] invalid op type", K(op_type));
    }
  }
}


void ObTableGroupOpFactory::free_all()
{
  for (int64_t i = 0; i < ObTableGroupType::TYPE_MAX; i++) {
    ObLockGuard<ObSpinLock> guard(locks_[i]);
    ObITableOp *op = nullptr;
    const ObTableGroupType type = static_cast<ObTableGroupType>(i);
    while(nullptr != (op = used_list_[type].remove_first())) {
      op->~ObITableOp();
      allocator_.free(op);
    }
    while(nullptr != (op = free_list_[type].remove_first())) {
      op->~ObITableOp();
      allocator_.free(op);
    }
  }
}
} // end namespace table
} // end namespace oceanbase
