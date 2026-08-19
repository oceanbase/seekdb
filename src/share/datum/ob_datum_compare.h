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

#ifndef OCEANBASE_SHARE_DATUM_OB_DATUM_COMPARE_H_
#define OCEANBASE_SHARE_DATUM_OB_DATUM_COMPARE_H_

#include "share/datum/ob_datum_funcs.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace common
{

inline ObDatumCmpFuncType get_datum_cmp_func(const ObObjMeta &column_type,
                                             const ObObjMeta &parameter_type)
{
  ObDatumCmpFuncType cmp_func = nullptr;
  const bool not_both_lob_storage =
      column_type.is_lob_storage() ^ parameter_type.is_lob_storage();
  if (column_type.get_type_class() != parameter_type.get_type_class()
      || not_both_lob_storage) {
    cmp_func = ObDatumFuncs::get_nullsafe_cmp_func(
        column_type.get_type(),
        parameter_type.get_type(),
        NULL_FIRST,
        column_type.get_collation_type(),
        column_type.get_scale(),
        column_type.has_lob_header() || parameter_type.has_lob_header());
  } else {
    ObDatumBasicFuncs *basic_funcs = ObDatumFuncs::get_basic_func(
        column_type.get_type(), column_type.get_collation_type());
    cmp_func = basic_funcs->null_first_cmp_;
  }
  return cmp_func;
}

struct ObDatumComparator
{
  ObDatumComparator(ObDatumCmpFuncType cmp_func,
                    int &ret,
                    bool &equal,
                    const ObDatumAccessContext *access_ctx,
                    bool reverse = false)
      : cmp_func_(cmp_func),
        ret_(ret),
        equal_(equal),
        access_ctx_(access_ctx),
        reverse_(reverse)
  {}

  bool operator()(const ObDatum &left, const ObDatum &right)
  {
    int &ret = ret_;
    int cmp_ret = 0;
    if (OB_FAIL(ret)) {
    } else if (!reverse_ && OB_FAIL(cmp_func_(left, right, cmp_ret, access_ctx_))) {
      COMMON_LOG(WARN, "failed to compare datum", K(ret), K(left), K(right), KP(cmp_func_));
    } else if (reverse_ && OB_FAIL(cmp_func_(right, left, cmp_ret, access_ctx_))) {
      COMMON_LOG(WARN, "failed to compare datum", K(ret), K(left), K(right), KP(cmp_func_));
    } else if (0 == cmp_ret && !equal_) {
      equal_ = true;
    }
    return reverse_ ? cmp_ret > 0 : cmp_ret < 0;
  }

private:
  ObDatumCmpFuncType cmp_func_;
  int &ret_;
  bool &equal_;
  const ObDatumAccessContext *access_ctx_;
  bool reverse_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_DATUM_OB_DATUM_COMPARE_H_
