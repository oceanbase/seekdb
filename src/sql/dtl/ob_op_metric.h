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

#ifndef OB_OP_METRIC_H
#define OB_OP_METRIC_H

#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase {
namespace sql {
// Use model: record the time of the first/last (row, buffer) input and output, such as receive receiving the first row of data, such as dtl receiving the first buffer etc
//         At the same time provide the calculation of interval time, through the switch to control whether to calculate these statistics, such as dtl write buffer time etc
class ObOpMetric
{
  OB_UNIS_VERSION(1);
public:
  ObOpMetric() :
    enable_audit_(false), id_(-1), type_(MetricType::DEFAULT_MAX), interval_cnt_(0), interval_start_time_(0), interval_end_time_(0),
    exec_time_(0), flag_(0), first_in_ts_(0), first_out_ts_(0), last_in_ts_(0), last_out_ts_(0), counter_(0), eof_(false)
  {}
  virtual ~ObOpMetric() {}

  enum MetricType {
    OP = 0,
    DTL = 1,
    DEFAULT_MAX = 2
  };
  ObOpMetric &operator = (const ObOpMetric &other)
  {
    enable_audit_ = other.enable_audit_;
    id_ = other.id_;
    type_ = other.type_;
    exec_time_ = other.exec_time_;
    first_in_ts_ = other.first_in_ts_;
    first_out_ts_ = other.first_out_ts_;
    last_in_ts_ = other.last_in_ts_;
    last_out_ts_ = other.last_out_ts_;
    counter_ = other.counter_;
    eof_ = other.eof_;
    return *this;
  }

  void init(bool enable_audit) { enable_audit_ = enable_audit; }

  void mark_first_in();
  void mark_first_out();
  void mark_eof();

  OB_INLINE void set_first_in_ts(int64_t first_in_ts) { first_in_ts_ = first_in_ts; }
  OB_INLINE void set_first_out_ts(int64_t first_out_ts ) { first_out_ts_ = first_out_ts; }
  OB_INLINE void set_last_in_ts(int64_t last_in_ts) { last_in_ts_ = last_in_ts; }
  OB_INLINE void set_last_out_ts(int64_t last_out_ts) { last_out_ts_ = last_out_ts; }

  OB_INLINE int64_t get_first_in_ts() const { return first_in_ts_; }
  OB_INLINE int64_t get_first_out_ts() const { return first_out_ts_; }
  OB_INLINE int64_t get_last_in_ts() const { return last_in_ts_; }
  OB_INLINE int64_t get_last_out_ts() const { return last_out_ts_; }
  OB_INLINE bool get_eof() const { return eof_; }

  OB_INLINE void count() { ++counter_; }
  OB_INLINE void count(int64_t cnt) { counter_ += cnt; }
  int64_t get_counter() { return counter_; }

  void set_audit(bool enable_audit) { enable_audit_ = enable_audit; }
  bool get_enable_audit() { return enable_audit_; }
  void set_id(int64_t id) { id_ = id; }
  int64_t get_id() { return id_; }

  int get_type() { return static_cast<int>(type_); }
  void set_type(MetricType type) { type_ = type; }

  void mark_interval_start(int64_t interval = 1);
  void mark_interval_end(int64_t *out_exec_time = nullptr, int64_t interval = 1);
  OB_INLINE int64_t get_exec_time() { return exec_time_; }

  TO_STRING_KV(K_(id), K_(type), K_(first_in_ts), K_(first_out_ts), K_(last_in_ts), K_(last_out_ts), K_(counter), K_(exec_time), K_(eof));
private:
  static const int64_t FIRST_IN = 0x01;
  static const int64_t FIRST_OUT = 0x02;
  static const int64_t LAST_IN = 0x4;
  static const int64_t LAST_OUT = 0x08;
  static const int64_t DTL_EOF = 0x10;

  bool enable_audit_;
  int64_t id_;
  MetricType type_;
  // Every INTERVAL times calculate once
  static const int64_t INTERVAL = 1;
  int64_t interval_cnt_;
  int64_t interval_start_time_;
  int64_t interval_end_time_;
  int64_t exec_time_;

  int64_t flag_;
  int64_t first_in_ts_;
  int64_t first_out_ts_;
  int64_t last_in_ts_;
  int64_t last_out_ts_;

  int64_t counter_;
  bool eof_;
};

OB_INLINE void ObOpMetric::mark_first_in()
{
  if (enable_audit_ && !(flag_ & FIRST_IN)) {
    first_in_ts_ = common::ObTimeUtility::current_time();
    flag_ |= FIRST_IN;
  }
}

OB_INLINE void ObOpMetric::mark_first_out()
{
  if (enable_audit_ && !(flag_ & FIRST_OUT)) {
    first_out_ts_ = common::ObTimeUtility::current_time();
    flag_ |= FIRST_OUT;
  }
}

OB_INLINE void ObOpMetric::mark_interval_start(int64_t interval)
{
#ifndef NDEBUG
  if (enable_audit_) {
    if (INTERVAL == interval) {
      interval_start_time_ = common::ObTimeUtility::current_time();
    } else if (0 == interval_cnt_ % interval) {
      interval_start_time_ = common::ObTimeUtility::current_time();
    }
    ++interval_cnt_;
  }
#else
  UNUSED(interval);
#endif
}

OB_INLINE void ObOpMetric::mark_interval_end(int64_t *out_exec_time, int64_t interval)
{
#ifndef NDEBUG
  if (enable_audit_) {
    if (INTERVAL == interval) {
      interval_end_time_ = common::ObTimeUtility::current_time();
      if (nullptr != out_exec_time) {
        *out_exec_time += (interval_end_time_ - interval_start_time_);
      } else {
        exec_time_ += (interval_end_time_ - interval_start_time_);
      }
      // Reuse the previous start time
      interval_start_time_ = interval_end_time_;
    } else if (INTERVAL - 1 == interval_cnt_ % interval) {
      interval_end_time_ = common::ObTimeUtility::current_time();
      if (nullptr != out_exec_time) {
        *out_exec_time += (interval_end_time_ - interval_start_time_);
      } else {
        exec_time_ += (interval_end_time_ - interval_start_time_);
      }
      // Reuse the previous start time
      interval_start_time_ = interval_end_time_;
    }
  }
#else
  UNUSED(out_exec_time);
  UNUSED(interval);
#endif
}

OB_INLINE void ObOpMetric::mark_eof()
{
  if (enable_audit_ && !(flag_ & DTL_EOF)) {
    eof_ = true;
    flag_ |= DTL_EOF;
  }
}

}  // sql
}  // oceanbase

#endif /* OB_OP_METRIC_H */
