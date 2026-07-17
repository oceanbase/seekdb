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

#ifndef OB_DOCID_COMPARE_H_
#define OB_DOCID_COMPARE_H_

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

class DocidCompare
{
public:
  DocidCompare() : is_ascending_(true) {}

  int init(bool is_ascending = true)
  {
    int ret = OB_SUCCESS;
    is_ascending_ = is_ascending;
    return ret;
  }

  int operator()(int64_t left_doc_id, int64_t right_doc_id, int &cmp_ret) const
  {
    int ret = OB_SUCCESS;
    if (left_doc_id < right_doc_id) {
      cmp_ret = is_ascending_ ? -1 : 1;
    } else if (left_doc_id > right_doc_id) {
      cmp_ret = is_ascending_ ? 1 : -1;
    } else {
      cmp_ret = 0;
    }
    return ret;
  }

private:
  bool is_ascending_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OB_DOCID_COMPARE_H_ */
