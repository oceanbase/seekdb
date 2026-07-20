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

#ifndef OCEANBASE_SORT_WRAPPER_H_
#define OCEANBASE_SORT_WRAPPER_H_

#include <algorithm>
#include "lib/utility/ob_tracepoint.h"
#include "lib/oblog/ob_log_module.h"
namespace oceanbase
{
namespace lib
{
template <class Iterator, class Compare>
void ob_sort(Iterator first, Iterator last, Compare comp)
{
  int ret = OB_E(EventTable::EN_CHECK_SORT_CMP) OB_SUCCESS;
  if (OB_FAIL(ret) && std::is_empty<Compare>::value) {
    ret = OB_SUCCESS;
    for (Iterator iter = first; OB_SUCC(ret) && iter != last; ++iter) {
      if (comp(*iter, *iter)) {
        ret = common::OB_ERR_UNEXPECTED;
        OB_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED,"check irreflexivity failed");
      }
    }
  }
  std::sort(first, last, comp);
}

template <class Iterator>
void ob_sort(Iterator first, Iterator last)
{
  using ValueType = typename std::iterator_traits<Iterator>::value_type;
  struct Compare
  {
    bool operator()(ValueType& l, ValueType& r)
    {
      return l < r;
    }
  };
  ob_sort(first, last, Compare());
}
} // end of namespace lib
} // end of namespace oceanbase
#endif
