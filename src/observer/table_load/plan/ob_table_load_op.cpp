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

#include "observer/table_load/plan/ob_table_load_op.h"

namespace oceanbase
{
namespace observer
{
using namespace common;

ObTableLoadOp::ObTableLoadOp(ObTableLoadPlan *plan)
  : plan_(plan),
    op_type_(ObTableLoadOpType::INVALID_OP_TYPE),
    parent_(nullptr),
    start_time_(0),
    end_time_(0)
{
  childs_.set_block_allocator(ModulePageAllocator(plan_->get_allocator()));
}

ObTableLoadOp::ObTableLoadOp(ObTableLoadOp *parent)
  : plan_(parent->plan_),
    op_type_(ObTableLoadOpType::INVALID_OP_TYPE),
    parent_(parent),
    start_time_(0),
    end_time_(0)
{
  childs_.set_block_allocator(ModulePageAllocator(plan_->get_allocator()));
}

int64_t ObTableLoadOp::simple_to_string(char *buf, int64_t buf_len, const bool show_childs) const
{
  int64_t pos = 0;
  J_OBJ_START();
  databuff_printf(buf, buf_len, pos, "%p", reinterpret_cast<const void *>(this));
  J_COMMA();
  databuff_printf(buf, buf_len, pos, "%s", ObTableLoadOpType::get_type_string(op_type_));
  if (show_childs && !childs_.empty()) {
    J_COMMA();
    J_NAME("childs");
    J_COLON();
    J_ARRAY_START();
    databuff_printf(buf, buf_len, pos, "cnt:%ld, ", childs_.count());
    for (int64_t i = 0; i < childs_.count(); ++i) {
      ObTableLoadOp *child = childs_.at(i);
      if (0 == i) {
        databuff_printf(buf, buf_len, pos, "%ld:", i);
      } else {
        databuff_printf(buf, buf_len, pos, ", %ld:", i);
      }
      pos += child->simple_to_string(buf + pos, buf_len - pos);
    }
    J_ARRAY_END();
  }
  J_OBJ_END();
  return pos;
}

} // namespace observer
} // namespace oceanbase
