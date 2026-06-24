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
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace observer
{
void ObAllVirtualSchemaSlot::reset(common::ObIAllocator &allocator, common::ObIArray<ObSchemaSlot> &tenant_slot_infos) 
{
  const char *ptr = NULL;
  common::ObString str;
  int ret = OB_SUCCESS;
  int len = tenant_slot_infos.count();

  for (int64_t i = 0; i < len && OB_SUCC(ret); ++i) {
    ptr = ((tenant_slot_infos.at(i)).get_mod_ref_infos()).ptr();
    if (OB_NOT_NULL(ptr)) {
      allocator.free(const_cast<char*>(ptr));
    }
    (tenant_slot_infos.at(i)).reset();
  }
  tenant_slot_infos.reset();
}

int ObAllVirtualSchemaSlot::inner_open()
{
  const ObAddr &addr = GCTX.self_addr();
  int ret = OB_SUCCESS;
  
  if (false == addr.ip_to_string(ip_buffer_, sizeof(ip_buffer_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to convert ip to string", KR(ret), K(addr));
  }
  return ret;
}

int ObAllVirtualSchemaSlot::get_next_tenant_slot_info(ObSchemaSlot &schema_slot) {
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator_ is null", KR(ret));
  } else if (slot_idx_ >= schema_slot_infos_.count()) {
    do {
      reset(*allocator_, schema_slot_infos_);
      if (++t_loop_idx_ >= 1) {
        ret = OB_ITER_END;
      } else {
        
        if (OB_SUCCESS != (tmp_ret = schema_service_.get_tenant_slot_info(*allocator_, 1UL, schema_slot_infos_))) {
          LOG_WARN("fail to get tenant slot info", KR(tmp_ret), K(1UL));
          reset(*allocator_, schema_slot_infos_);
        } else {
          slot_idx_ = 0;
        }
      }
    } while (0 == schema_slot_infos_.count() && OB_SUCC(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(slot_idx_ < 0 || slot_idx_ >= schema_slot_infos_.count())) {
      ret = OB_ERROR_OUT_OF_RANGE;
      LOG_WARN("slot_idx_ out of range", KR(ret), K(slot_idx_));
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

  if (OB_FAIL(get_next_tenant_slot_info(schema_slot))) {
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
