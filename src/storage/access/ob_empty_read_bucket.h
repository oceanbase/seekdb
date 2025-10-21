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

#ifndef OCEANBASE_STORAGE_OB_EMPTY_READ_BUCKET_H_
#define OCEANBASE_STORAGE_OB_EMPTY_READ_BUCKET_H_

#include <stdint.h>
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace storage
{
struct ObEmptyReadCell
{
  ObEmptyReadCell(): count_(0), hashcode_(0), build_time_(0), is_waiting_(false) {}
  virtual ~ObEmptyReadCell() {}
  void reset()
  {
    count_ = 0;
    hashcode_ = 0;
    build_time_ = 0;
    is_waiting_ = false;
  }
  void set(
      const uint64_t hashcode,
      const int64_t inc_val)
  {
    count_ = static_cast<int32_t>(inc_val);
    hashcode_ = hashcode;
    build_time_ = ObTimeUtility::current_time();
    is_waiting_ = false;
  }
  int inc_and_fetch(
      const uint64_t hashcode,
      const int64_t inc_val,
      uint64_t &cur_cnt)
  {
    int ret = OB_SUCCESS;
    if(hashcode_ != hashcode) {
      if(!build_time_ || ObTimeUtility::current_time() - ELIMINATE_TIMEOUT_US > build_time_){
        set(hashcode, inc_val);
        cur_cnt = count_;
      } else {
        //bucket is in use recently,ignore in 2min
        cur_cnt = 1;
      }
    } else {
      count_ += inc_val; 
      cur_cnt = count_; 
    }
    return ret;
  }
  int inc_and_fetch(
      const uint64_t hashcode,
      uint64_t &cur_cnt)
  {
    return inc_and_fetch(hashcode, 1, cur_cnt); 
  }
  bool check_timeout()
  {
    bool bool_ret = false;
    int64_t cur_time = ObTimeUtility::current_time();
    if (cur_time - ELIMINATE_TIMEOUT_US > build_time_) {
      bool_ret = true;
      count_ /= 2;
      build_time_ = cur_time;
    }
    return bool_ret;
  }
  bool check_waiting()
  {
    return is_waiting_;
  }
  void set_waiting()
  {
    is_waiting_ = true;
  }
  TO_STRING_KV(K_(count), K_(hashcode), K_(build_time), K_(is_waiting));
  static const int64_t ELIMINATE_TIMEOUT_US = 1000 * 1000 * 120; //2min
  volatile int32_t count_;
  volatile uint64_t hashcode_;
  volatile int64_t build_time_;
  volatile bool is_waiting_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObEmptyReadCell);
};

class ObEmptyReadBucket
{
public:
  ObEmptyReadBucket();
  virtual ~ObEmptyReadBucket();
  static int mtl_init(ObEmptyReadBucket *&bucket);
  static void mtl_destroy(ObEmptyReadBucket *&bucket);
  int init(const int64_t lower_bound);
  void destroy();
  OB_INLINE bool is_valid() const { return NULL != buckets_; }
  int get_cell(const uint64_t hashcode, ObEmptyReadCell *&cell);

  void reset();

private:
  OB_INLINE uint64_t get_bucket_size() const { return bucket_size_; }
  static const int64_t MIN_REBUILD_PERIOD_US = 1000 * 1000 * 120; //2min
  static constexpr int64_t BUCKET_SIZE_LIMIT = 1<<20;//1048576
  static constexpr int64_t BUCKET_SIZE_LOWER_LIMIT = 1<<14;//16384
  ObArenaAllocator allocator_;
  ObEmptyReadCell *buckets_;
  uint64_t bucket_size_;
};
} // namespace oceanbase
} // namespace storage

#endif // OCEANBASE_STORAGE_OB_EMPTY_READ_BUCKET_H_
