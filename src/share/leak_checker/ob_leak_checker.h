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
#ifndef OCEANBASE_SHARE_OB_LEAK_CHECKER_H_
#define OCEANBASE_SHARE_OB_LEAK_CHECKER_H_
#include "lib/hash/ob_hashmap.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace share
{

template <typename Key, typename Value>
class ObBaseLeakChecker
{
  typedef common::ObLinearHashMap<Key, Value> leak_checker_map_t;
  struct Printer
  {
    bool operator()(const Key &k, const Value &v)
    {
      bool ret = true;
      COMMON_LOG(INFO, "LEAK_CHECKER ",
                 "key:", k,
                 "value:", v);
      return ret;
    }
  };
public:
  ObBaseLeakChecker();
  ~ObBaseLeakChecker();
  int init();
  void reset();
  void record(const Key &k, const Value &v, const int64_t max_cnt=INT64_MAX);
  void release(const Key &k, Value &value);
  template <typename Function> int for_each(Function &fn);
  void print();
  bool is_empty()
  { return (ATOMIC_LOAD(&total_size_) == 0); }
private:
  static constexpr int MEMORY_LIMIT = 128L << 20;
  static constexpr int MAP_SIZE_LIMIT = MEMORY_LIMIT / sizeof(Value);
  leak_checker_map_t checker_info_;
  int64_t total_size_;
};

template<typename Key, typename Value>
ObBaseLeakChecker<Key, Value>::ObBaseLeakChecker()
{
  total_size_ = 0;
}

template<typename Key, typename Value>
ObBaseLeakChecker<Key, Value>::~ObBaseLeakChecker()
{
  reset();
}

template<typename Key, typename Value>
int ObBaseLeakChecker<Key, Value>::init()
{
  ObMemAttr attr("leakChecker", ObCtxIds::DEFAULT_CTX_ID);
  int ret = checker_info_.init(attr);
  if (OB_FAIL(ret)) {
  } else {
    COMMON_LOG(INFO, "leak checker init succ");
  }
  return ret;
}

template<typename Key, typename Value>
void ObBaseLeakChecker<Key, Value>::reset()
{
  checker_info_.reset();
  total_size_ = 0;
}

template<typename Key, typename Value>
void ObBaseLeakChecker<Key, Value>::record(const Key &k, const Value &v, const int64_t max_cnt)
{
  INIT_SUCC(ret);
  if (total_size_ < OB_MIN(MAP_SIZE_LIMIT, max_cnt)) {
    if (OB_FAIL(checker_info_.insert(k, v))) {
    } else {
      ATOMIC_INC(&total_size_);
    }
  }
}

template<typename Key, typename Value>
void ObBaseLeakChecker<Key, Value>::release(const Key &k, Value &value)
{
  INIT_SUCC(ret);
  if (OB_FAIL(checker_info_.erase(k, value))) {
  } else {
    ATOMIC_DEC(&total_size_);
  }
}

template<typename Key, typename Value>
void ObBaseLeakChecker<Key, Value>::print()
{
  Printer fn;
  for_each(fn);
}

template <typename Key, typename Value>
template <typename Function>
int ObBaseLeakChecker<Key, Value>::for_each(Function &fn)
{
  int ret = OB_SUCCESS;
  ret = checker_info_.for_each(fn);
  return ret;
}

}  // storage
}  // oceanbase
#endif  // OCEANBASE_SHARE_OB_LEAK_CHECKER_H_
