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

#ifndef OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_CHUNK_H_
#define OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_CHUNK_H_

#include "sql/engine/basic/ob_temp_row_store.h"

namespace oceanbase {
namespace sql {
template <typename Store_Row, bool has_addon>
struct ObSortVecOpChunk : public common::ObDLinkBase<ObSortVecOpChunk<Store_Row, has_addon>>
{
  explicit ObSortVecOpChunk(const int64_t level) :
    level_(level), sk_row_iter_(), addon_row_iter_(), sk_row_(nullptr), addon_row_(nullptr)
  {}
  void reset_row_iter()
  {
    sk_row_iter_.reset();
    addon_row_iter_.reset();
  }
  int init_row_iter()
  {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(sk_row_iter_.init(&sk_store_))) {
      SQL_ENG_LOG(WARN, "init iterator failed", K(ret));
    } else if (has_addon && OB_FAIL(addon_row_iter_.init(&addon_store_))) {
      SQL_ENG_LOG(WARN, "init iterator failed", K(ret));
    }
    return ret;
  }
  int get_next_row()
  {
    int ret = common::OB_SUCCESS;
    int64_t read_rows = 0;
    const int64_t max_rows = 1;
    if (OB_FAIL(sk_row_iter_.get_next_batch(max_rows, read_rows,
                                            reinterpret_cast<const ObCompactRow **>(&sk_row_)))) {
      SQL_ENG_LOG(WARN, "get next row failed", K(ret));
    } else if (has_addon) {
      if (OB_FAIL(addon_row_iter_.get_next_batch(
            max_rows, read_rows, reinterpret_cast<const ObCompactRow **>(&addon_row_)))) {
        SQL_ENG_LOG(WARN, "get next row failed", K(ret));
      } else {
        const_cast<Store_Row *>(sk_row_)->set_addon_ptr(addon_row_, sk_store_.get_row_meta());
      }
    }
    return ret;
  }

public:
  int64_t level_;
  ObTempRowStore sk_store_;
  ObTempRowStore addon_store_;
  ObTempRowStore::Iterator sk_row_iter_;
  ObTempRowStore::Iterator addon_row_iter_;
  const Store_Row *sk_row_;
  const Store_Row *addon_row_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObSortVecOpChunk);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_SORT_VEC_OP_CHUNK_H_ */
