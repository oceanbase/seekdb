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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_ROW_STORE_MGR_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_ROW_STORE_MGR_H_

#include "sql/engine/basic/ob_temp_row_store.h"

namespace oceanbase
{
namespace sql
{

class ObSortRowStoreMgr
{
public:
  ObSortRowStoreMgr(ObTempRowStore &sk_store, ObTempRowStore *addon_store)
    : sk_store_(sk_store), addon_store_(addon_store)
  {}

  void reset()
  {
    sk_store_.reset();
    if (nullptr != addon_store_) {
      addon_store_->reset();
    }
  }

  int64_t get_row_cnt() const
  {
    return sk_store_.get_row_cnt();
  }

  int64_t get_file_size() const
  {
    return sk_store_.get_file_size() + (nullptr == addon_store_ ? 0 : addon_store_->get_file_size());
  }

  int64_t get_mem_hold() const
  {
    return sk_store_.get_mem_hold() + (nullptr == addon_store_ ? 0 : addon_store_->get_mem_hold());
  }

  int64_t get_payload_io_size() const
  {
    return nullptr == addon_store_ ? 0 : addon_store_->get_file_size();
  }

private:
  ObTempRowStore &sk_store_;
  ObTempRowStore *addon_store_;
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_ROW_STORE_MGR_H_ */
