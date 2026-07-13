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

# define USING_LOG_PREFIX SERVER
#include "observer/virtual_table/ob_all_virtual_schema_slot.h"

namespace oceanbase
{
namespace observer
{
void ObAllVirtualSchemaSlot::reset_slot_infos_(common::ObIAllocator &allocator)
{
  for (int64_t i = 0; i < schema_slot_infos_.count(); ++i) {
    const char *ptr = schema_slot_infos_.at(i).get_mod_ref_infos().ptr();
    if (OB_NOT_NULL(ptr)) {
      allocator.free(const_cast<char*>(ptr));
    }
    schema_slot_infos_.at(i).reset();
  }
  schema_slot_infos_.reset();
}

int ObAllVirtualSchemaSlot::get_next_slot_info_(ObSchemaSlot &schema_slot)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator_ is null", KR(ret));
  } else if (OB_INVALID_INDEX == slot_idx_) {
    if (OB_FAIL(schema_service_.get_tenant_slot_info(*allocator_, 1UL, schema_slot_infos_))) {
      LOG_WARN("fail to get tenant slot info", KR(ret));
      reset_slot_infos_(*allocator_);
    } else {
      slot_idx_ = 0;
    }
  }
  if (OB_SUCC(ret)) {
    if (slot_idx_ >= schema_slot_infos_.count()) {
      reset_slot_infos_(*allocator_);
      ret = OB_ITER_END;
    } else {
      schema_slot = schema_slot_infos_[slot_idx_++];
    }
  }
  return ret;
}

int ObAllVirtualSchemaSlot::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObSchemaSlot schema_slot;

  if (OB_FAIL(get_next_slot_info_(schema_slot))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail to get next tenant_info", KR(ret));
    }
  }
  if (OB_SUCC(ret)) {
    
    const int64_t slot_id = schema_slot.get_slot_id();
    const int64_t total_ref_cnt = schema_slot.get_ref_cnt();
    const int64_t schema_version = schema_slot.get_schema_version();
    const int64_t schema_count = schema_slot.get_schema_count();
    const int64_t allocator_idx = schema_slot.get_allocator_idx();
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case SLOT_ID: {
          cur_row_.cells_[i].set_int(static_cast<int64_t>(slot_id));
          break;
        }
        case SCHEMA_VERSION: {
          cur_row_.cells_[i].set_int(static_cast<int64_t>(schema_version));
          break;
        }
        case SCHEMA_COUNT: {
          cur_row_.cells_[i].set_int(static_cast<int64_t>(schema_count));
          break;
        }
        case REF_CNT: { 
          cur_row_.cells_[i].set_int(static_cast<int64_t>(total_ref_cnt));
          break;
        }
        case REF_INFO: {
          if (OB_NOT_NULL(schema_slot.get_mod_ref_infos().ptr())) {
            cur_row_.cells_[i].set_varchar(schema_slot.get_mod_ref_infos());
          } else {
            cur_row_.cells_[i].set_varchar("");
          }
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(
                                                ObCharset::get_default_charset()));
          break;
        }
        case ALLOCATOR_IDX: {
          cur_row_.cells_[i].set_int(static_cast<int64_t>(allocator_idx));
          break;
        }
        default : {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid col_id", KR(ret), K(col_id));
        }
      }
    }

    if (OB_SUCC(ret)) {
      row = &cur_row_;
    }
  }
  return ret;
}
} /* namespace observer */
} /* namespace oceanbase */
