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

#ifndef OCEANBASE_SHARE_OB_QUERY_REQUEST_H_
#define OCEANBASE_SHARE_OB_QUERY_REQUEST_H_

#include "ob_request_base.h"

namespace oceanbase
{
namespace share
{

class ObQueryReqFromJson : public ObReqFromJson
{
public :
  ObQueryReqFromJson()
    : ObReqFromJson(), select_items_(), group_items_(),
      having_items_(), score_items_(), inner_score_items_(), outer_score_items_(), outer_condition_items_(), sub_score_item_seq_(0), output_all_columns_(true),
      score_alias_() {}
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> select_items_;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> group_items_;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> having_items_;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> score_items_;
  // for sub query
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> inner_score_items_;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> outer_score_items_;
  common::ObSEArray<ObReqExpr *, 4, common::ModulePageAllocator, true> outer_condition_items_;
  uint64_t sub_score_item_seq_;
  int translate(char *buf, int64_t buf_len, int64_t &res_len);
  int add_score_item(ObIAllocator &alloc, ObReqExpr *score_item);
  inline bool is_score_item_exist() { return !score_items_.empty(); }
  bool output_all_columns_;
  common::ObString score_alias_;
  common::ObSEArray<common::ObString, 4, common::ModulePageAllocator, true> match_idxs_;
};

}  // namespace share
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_OB_QUERY_REQUEST_H_
