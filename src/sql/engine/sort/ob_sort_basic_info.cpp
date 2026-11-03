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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_sort_basic_info.h"
#include "sql/engine/expr/plugin_function_expr.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(ObSortFieldCollation, field_idx_, cs_type_, is_ascending_, null_pos_, is_not_null_);

int compare_sort_datums(const ObSortFieldCollation &collation,
    const ObSortCmpFunc &native, const ObIArray<ObExpr *> *expressions,
    ObEvalCtx *context, const ObDatum &left, const ObDatum &right, int &ordering,
    const ObDatumAccessContext *access)
{
  if (expressions) {
    if (collation.field_idx_ >= expressions->count() || !expressions->at(collation.field_idx_))
      return OB_INVALID_ARGUMENT;
    const ObExpr *expression = expressions->at(collation.field_idx_);
    if (expression->type_ == T_FUN_SYS_PLUGIN_TYPE_VALUE) {
      const auto *info = dynamic_cast<const PluginTypeValueExtraInfo *>(expression->extra_info_);
      if (!info || !info->valid()) return OB_INVALID_DATA;
      if (info->mode_ == PluginTypeValueExtraInfo::ORDERED && !left.is_null() && !right.is_null()) {
        if (!context) return OB_INVALID_ARGUMENT;
        bool handled = false;
        const int ret = PluginTypeValueExpr::compare_ordered(expression, *context, left, right, handled, ordering);
        return ret != OB_SUCCESS ? ret : handled ? OB_SUCCESS : OB_ERR_UNEXPECTED;
      }
    }
  }
  return native.cmp_func_ ? native.cmp_func_(left, right, ordering, access) : OB_INVALID_ARGUMENT;
}

} // end namespace sql
} // end namespace oceanbase
