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

#include "ob_all_virtual_ctx_memory_info.h"

#include "lib/alloc/memory_dump.h"

namespace oceanbase
{
using namespace common;

namespace observer
{
ObAllVirtualCtxMemoryInfo::ObAllVirtualCtxMemoryInfo()
    : has_start_(false)
{
}

ObAllVirtualCtxMemoryInfo::~ObAllVirtualCtxMemoryInfo()
{
  reset();
}

void ObAllVirtualCtxMemoryInfo::reset()
{
  has_start_ = false;
}

int ObAllVirtualCtxMemoryInfo::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (has_start_) {
    // do nothing
  } else {
    {
      for (int ctx_id = 0; OB_SUCC(ret) && ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
        auto ctx_allocator = ObMallocAllocator::get_instance()->get_ctx_allocator(ctx_id);
        if (OB_ISNULL(ctx_allocator)) {
          // do nothing
        } else {
          ret = add_row(ctx_id, ctx_allocator->get_hold(), ctx_allocator->get_used(),
                        ctx_allocator->get_limit());
        }
      }
    }
    if (OB_SUCC(ret)) {
      scanner_it_ = scanner_.begin();
      has_start_ = true;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get next row", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

int ObAllVirtualCtxMemoryInfo::add_row(int64_t ctx_id, int64_t hold, int64_t used, int64_t limit)
{
  int ret = OB_SUCCESS;
  ObObj *cells = nullptr;
  if (OB_ISNULL(cells = cur_row_.cells_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      const uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case CTX_ID: {
          cells[i].set_int(ctx_id);
          break;
        }
        case CTX_NAME: {
          cells[i].set_varchar(get_global_ctx_info().get_ctx_name(ctx_id));
          cells[i].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case HOLD: {
          cells[i].set_int(hold);
          break;
        }
        case USED: {
          cells[i].set_int(used);
          break;
        }
        case LIMIT: {
          cells[i].set_int(limit);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
          break;
        }
      }
    } // iter column end
    if (OB_SUCC(ret)) {
      // scanner maximum supports 64M, therefore overflow is not considered for now
      if (OB_FAIL(scanner_.add_row(cur_row_))) {
        SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
        if (OB_SIZE_OVERFLOW == ret) {
          ret = OB_SUCCESS;
        }
      }
    }
  }
  return ret;
}

} // observer
} // oceanbase
