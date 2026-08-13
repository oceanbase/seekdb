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
#include <cstdint>                                       // UINT64_MAX
#include "lib/ob_errno.h"                                // errno
#include "lib/utility/ob_print_utils.h"                  // TO_STRING_KV
#include "share/log/palf/log_define.h"
#include "share/log/palf/lsn.h"
namespace oceanbase
{
namespace palf
{
class LogIteratorInfo
{
public:
  LogIteratorInfo()
      : hot_cache_stat_(),
        read_io_cnt_(0), read_io_size_(0), read_disk_cost_ts_(0) {}
  ~LogIteratorInfo() {
    reset();
  }
  LogIteratorInfo &operator=(const LogIteratorInfo &iterator_info)
  {
    if (&iterator_info != this) {
      this->hot_cache_stat_ = iterator_info.hot_cache_stat_;
      this->read_io_cnt_ = iterator_info.read_io_cnt_;
      this->read_io_size_ = iterator_info.read_io_size_;
      this->read_disk_cost_ts_ = iterator_info.read_disk_cost_ts_;
    }
    return *this;
  }
  void reset() {
    hot_cache_stat_.reset();
    read_io_cnt_ = 0;
    read_io_size_ = 0;
    read_disk_cost_ts_ = 0;
  }
  void inc_cache_hit_cnt() { hot_cache_stat_.inc_hit_cnt(); }
  void inc_cache_miss_cnt() { hot_cache_stat_.inc_miss_cnt(); }
  void inc_cache_read_size(int64_t cache_read_size) { hot_cache_stat_.inc_cache_read_size(cache_read_size); }
  void inc_read_io_cnt() { read_io_cnt_++; }
  void inc_read_io_size(int64_t read_io_size) { read_io_size_ += read_io_size; }
  void inc_read_disk_cost_ts(int64_t read_disk_cost_ts) { read_disk_cost_ts_ += read_disk_cost_ts; }
  void set_start_lsn(const LSN &start_lsn) { start_lsn_ = start_lsn; }
  TO_STRING_KV(K_(hot_cache_stat),
               K_(read_io_cnt), K_(read_io_size), K_(read_disk_cost_ts), K_(start_lsn));

private:
  class IteratorCacheStat 
  {
  public:
    IteratorCacheStat() : hit_cnt_(0), miss_cnt_(0), cache_read_size_(0) {}
    ~IteratorCacheStat() { reset(); }
    IteratorCacheStat &operator=(const IteratorCacheStat &cache_stat) 
    {
      if (&cache_stat != this) {
        this->hit_cnt_ = cache_stat.hit_cnt_;
        this->miss_cnt_ = cache_stat.miss_cnt_;
        this->cache_read_size_ = cache_stat.cache_read_size_;
      }
      return *this;
    }
    void reset() {
      hit_cnt_ = 0;
      miss_cnt_ = 0;
      cache_read_size_ = 0;
    }
    void inc_hit_cnt() { hit_cnt_++; }
    void inc_miss_cnt() { miss_cnt_++; }
    void inc_cache_read_size(int64_t cache_read_size) { cache_read_size_ += cache_read_size; }
    double get_hit_ratio() const 
    { 
      int64_t total_cnt = (hit_cnt_ + miss_cnt_ == 0) ? 1 : hit_cnt_ + miss_cnt_;
      return hit_cnt_ * 1.0 /total_cnt; 
    }
    TO_STRING_KV(K_(hit_cnt), K_(miss_cnt), K_(cache_read_size), "hit_ratio", get_hit_ratio());
  private:
    int64_t hit_cnt_;
    int64_t miss_cnt_;
    int64_t cache_read_size_;
  };
private:
  // fields below are just for minotor.
  IteratorCacheStat hot_cache_stat_;
  int64_t read_io_cnt_;
  int64_t read_io_size_;
  int64_t read_disk_cost_ts_;
  LSN start_lsn_;
};
}
}
