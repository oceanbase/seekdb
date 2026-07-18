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

#ifndef OCEANBASE_COMMON_OB_SIMPLE_ITERATOR_
#define OCEANBASE_COMMON_OB_SIMPLE_ITERATOR_

#include <stdint.h>
#include "lib/oblog/ob_log_module.h"
#include "lib/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_se_array_iterator.h"
#include "common/ob_simple_iterator.h"

namespace oceanbase
{
namespace common
{
template <typename T, const char *LABEL, int64_t LOCAL_ARRAY_SIZE>
class ObSimpleIterator
{
public:
  ObSimpleIterator() : item_arr_(LABEL, OB_MALLOC_NORMAL_BLOCK_SIZE) { reset(); }
  ~ObSimpleIterator() {};
  void reset();

  int push(const T &item);
  int set_ready();
  bool is_ready() const { return is_ready_; }
  int get_next(T &item);
private:
  bool is_ready_;
  typename common::ObSEArray<T, LOCAL_ARRAY_SIZE> item_arr_;
  typename common::ObSEArray<T, LOCAL_ARRAY_SIZE>::iterator it_;
};

template <typename T, const char *LABEL, int64_t LOCAL_ARRAY_SIZE>
void ObSimpleIterator<T, LABEL, LOCAL_ARRAY_SIZE>::reset()
{
  is_ready_ = false;
  item_arr_.reset();
}

template <typename T, const char *LABEL, int64_t LOCAL_ARRAY_SIZE>
int ObSimpleIterator<T, LABEL, LOCAL_ARRAY_SIZE>::push(const T &item)
{
  int ret = OB_SUCCESS;

  if (is_ready_) {
    OB_LOG(WARN, "ObSimpleIterator already ready, cannot push element");
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(item_arr_.push_back(item))) {
    OB_LOG(WARN, "item push back error", K(ret), K(item));
  } else {
    // do nothing
  }

  return ret;
}

template <typename T, const char *LABEL, int64_t LOCAL_ARRAY_SIZE>
int ObSimpleIterator<T, LABEL, LOCAL_ARRAY_SIZE>::set_ready()
{
  int ret = OB_SUCCESS;

  if (is_ready_) {
    OB_LOG(WARN, "ObSimpleIterator is already ready");
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_ready_ = true;
    //First record the first element that needs to be traversed in preparation for the iterative operation
    it_ = item_arr_.begin();
  }

  return ret;
}

/*
 * During the traversal, the first row returns item_arr_..begin();
 * The next one returns ++it, which helps in judging whether it has reached the end, avoiding the risk of array out-of-bounds.
 */
template <typename T, const char *LABEL, int64_t LOCAL_ARRAY_SIZE>
int ObSimpleIterator<T, LABEL, LOCAL_ARRAY_SIZE>::get_next(T &item)
{
  int ret = OB_SUCCESS;

  if (!is_ready_) {
    OB_LOG(WARN, "ObSimpleIterator is not ready");
    ret = OB_ERR_UNEXPECTED;
  } else if (item_arr_.end() == it_) {
//    OB_LOG(DEBUG, "array iterate end", "part_cnt", item_arr_.count(),
//        K_(item_arr));
    ret = OB_ITER_END;
  } else {
    item = *it_;
    it_++;
  }

  return ret;
}

} // common
} // oceanbase

#endif //OCEANBASE_COMMON_OB_SIMPLE_ITERATOR_
