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

#pragma once
#include "common/datum/datum_value.h"
#include "share/datum/typed_datum_compare.h"
#include "query/parser/ob_item_type.h"
#include <utility>

namespace oceanbase { namespace sql {
// Text/NUMBER endpoints borrow the statement arena, as optimizer constants do.
// Scalar DOUBLE endpoints own bytes here; copying a range rebases those views.
struct SimpleRange
{
  SimpleRange() : scalar_storage_{0.0, 0.0} { set_whole_range(); }
  SimpleRange(const SimpleRange &other) : scalar_storage_{0.0, 0.0} { copy_from(other); }
  SimpleRange &operator=(const SimpleRange &other) {
    if (this != &other) { copy_from(other); }
    return *this;
  }
  void set_whole_range();
  void set_false_range();
  int compare_with_end(const SimpleRange &r, int &cmp) const;
  int compare_with_start(const SimpleRange &r, int &cmp) const;
  bool intersect(const SimpleRange &r);
  void set_bound(ObItemType type, common::DatumValue bound);
  void set_bound(ObItemType type, double bound);
  bool is_valid_range();
  bool is_superset(const SimpleRange &r) const;
  void multiply_double(double coefficient);
  TO_STRING_KV(K_(start), K_(end), K_(inclusive_start), K_(inclusive_end));
  common::DatumValue start_;
  common::DatumValue end_;
  bool inclusive_start_;
  bool inclusive_end_;
private:
  void assign_bound(common::DatumValue &target, const common::DatumValue &source, int slot) {
    target = source;
    if (!source.is_null() && source.is_double()) {
      scalar_storage_[slot] = source.get_double();
      target.datum_.set_string(reinterpret_cast<const char *>(&scalar_storage_[slot]), sizeof(double));
    }
  }
  void set_owned_double(common::DatumValue &target, double value, int slot) {
    scalar_storage_[slot] = value;
    target.meta_.set_double();
    target.datum_.set_string(reinterpret_cast<const char *>(&scalar_storage_[slot]), sizeof(double));
  }
  void normalize_double_storage() {
    const bool left_double = !start_.is_null() && start_.is_double();
    const bool right_double = !end_.is_null() && end_.is_double();
    // Capture both before writing either slot: a negative coefficient swaps
    // descriptors and their old pointers, not the backing scalar slots.
    const double left = left_double ? start_.get_double() : 0.0;
    const double right = right_double ? end_.get_double() : 0.0;
    if (left_double) {
      scalar_storage_[0] = left;
      start_.datum_.set_string(reinterpret_cast<const char *>(&scalar_storage_[0]), sizeof(double));
    }
    if (right_double) {
      scalar_storage_[1] = right;
      end_.datum_.set_string(reinterpret_cast<const char *>(&scalar_storage_[1]), sizeof(double));
    }
  }
  void copy_from(const SimpleRange &other) {
    MEMCPY(scalar_storage_, other.scalar_storage_, sizeof(scalar_storage_));
    start_ = other.start_; end_ = other.end_;
    inclusive_start_ = other.inclusive_start_; inclusive_end_ = other.inclusive_end_;
    for (common::DatumValue *endpoint : {&start_, &end_}) {
      for (int slot=0;slot<2;++slot) {
        if (endpoint->datum_.ptr_ == reinterpret_cast<const char *>(&other.scalar_storage_[slot])) {
          endpoint->datum_.ptr_ = reinterpret_cast<const char *>(&scalar_storage_[slot]);
          break;
        }
      }
    }
    normalize_double_storage();
  }
  double scalar_storage_[2];
};

inline void SimpleRange::set_whole_range()
{
  start_.set_min_value();
  end_.set_max_value();
  inclusive_start_ = false;
  inclusive_end_ = false;
}

inline void SimpleRange::set_false_range()
{
  start_.set_max_value();
  end_.set_min_value();
  inclusive_start_ = false;
  inclusive_end_ = false;
}

inline int SimpleRange::compare_with_end(const SimpleRange &r, int &cmp) const
{
  int ret = common::OB_SUCCESS;
  cmp = 0;
  if (end_.is_max_value()) {
    if (!r.end_.is_max_value()) {
      cmp = 1;
    }
  } else if (r.end_.is_max_value()) {
    cmp = -1;
  } else if (OB_FAIL(common::compare_datum_values(end_, r.end_, cmp))) {
  } else {
    if (0 == cmp) {
      if (inclusive_end_ && !r.inclusive_end_) {
        cmp = 1;
      } else if (!inclusive_end_ && r.inclusive_end_) {
        cmp = -1;
      }
    }
  }
  return ret;
}

inline int SimpleRange::compare_with_start(const SimpleRange &r, int &cmp) const
{
  int ret = common::OB_SUCCESS;
  cmp = 0;
  if (start_.is_min_value()) {
    if (!r.start_.is_min_value()) {
      cmp = -1;
    }
  } else if (r.start_.is_min_value()) {
    cmp = 1;
  } else if (OB_FAIL(common::compare_datum_values(start_, r.start_, cmp))) {
  } else {
    if (0 == cmp) {
      if (inclusive_start_ && !r.inclusive_start_) {
        cmp = -1;
      } else if (!inclusive_start_ && r.inclusive_start_) {
        cmp = 1;
      }
    }
  }
  return ret;
}

inline bool SimpleRange::intersect(const SimpleRange &r)
{
  bool bret = false;
  int ret = common::OB_SUCCESS;
  int cmp_start = 0;
  int cmp_end = 0;
  if (!common::can_compare_datum_values(start_, r.start_) || !common::can_compare_datum_values(end_, r.end_)) {
    // do nothing
  } else if (OB_FAIL(compare_with_start(r, cmp_start))) {
  } else if (OB_FAIL(compare_with_end(r, cmp_end))) {
  } else {
    bret = true;
    if (cmp_start == -1) {
      assign_bound(start_, r.start_, 0);
      inclusive_start_ = r.inclusive_start_;
    }
    if (cmp_end == 1) {
      assign_bound(end_, r.end_, 1);
      inclusive_end_ = r.inclusive_end_;
    }
  }
  return bret;
}

inline void SimpleRange::set_bound(ObItemType item_type, common::DatumValue bound)
{
  if (bound.is_null()) {
    if (T_OP_IS == item_type || T_OP_NSEQ == item_type) {
      start_.set_null();
      end_.set_null();
      inclusive_start_ = true;
      inclusive_end_ = true;
    } else if (T_OP_IS_NOT == item_type) {
      set_whole_range();
    } else {
      set_false_range();
    }
  } else if (T_OP_LE == item_type) {
    assign_bound(end_, bound, 1);
    inclusive_end_ = true;
  } else if (T_OP_LT == item_type) {
    assign_bound(end_, bound, 1);
    inclusive_end_ = false;
  } else if (T_OP_GE == item_type) {
    assign_bound(start_, bound, 0);
    inclusive_start_ = true;
  } else if (T_OP_GT == item_type) {
    assign_bound(start_, bound, 0);
    inclusive_start_ = false;
  } else if (T_OP_EQ == item_type || T_OP_NSEQ == item_type) {
    assign_bound(start_, bound, 0);
    assign_bound(end_, bound, 1);
    inclusive_start_ = true;
    inclusive_end_ = true;
  }
}

inline void SimpleRange::set_bound(ObItemType item_type, double bound)
{
  common::DatumValue value;
  value.meta_.set_double();
  value.datum_.set_string(reinterpret_cast<const char *>(&bound), sizeof(bound));
  set_bound(item_type, value);
}

inline bool SimpleRange::is_valid_range()
{
  bool bret = false;
  int cmp = 0;
  if (!common::can_compare_datum_values(start_, end_)) {
    bret = false;
  } else if (start_.is_null() && end_.is_null()) {
    bret = true;
  } else if (start_.is_max_value() || end_.is_min_value() ||
             start_.is_null() || end_.is_null()) {
    bret = false;
  } else if (start_.is_min_value() || end_.is_max_value()) {
    bret = true;
  } else if (common::OB_SUCCESS != common::compare_datum_values(start_, end_, cmp)) {
    bret = false;
  } else {
    if (-1 == cmp) {
      bret = true;
    } else if (1 == cmp) {
      bret = false;
    } else if (0 == cmp) {
      if (inclusive_start_ && inclusive_end_) {
        bret = true;
      } else {
        bret = false;
      }
    }
  }
  return bret;
}

inline bool SimpleRange::is_superset(const SimpleRange &r) const
{
  bool bret = false;
  int ret = common::OB_SUCCESS;
  int cmp_start = 0;
  int cmp_end = 0;
  if (!common::can_compare_datum_values(start_, r.start_) || !common::can_compare_datum_values(end_, r.end_)) {
    // do nothing
  } else if (OB_FAIL(compare_with_start(r, cmp_start))) {
  } else if (OB_FAIL(compare_with_end(r, cmp_end))) {
  } else {
    bret = cmp_start <= 0 && cmp_end >= 0;
  }
  return bret;
}

inline void SimpleRange::multiply_double(double coff)
{
  if (!start_.is_null() && start_.is_double()) {
    set_owned_double(start_, start_.get_double() * coff, 0);
  }
  if (!end_.is_null() && end_.is_double()) {
    set_owned_double(end_, end_.get_double() * coff, 1);
  }
  if (coff < 0) {
    std::swap(start_, end_);
    std::swap(inclusive_start_, inclusive_end_);
    normalize_double_storage();
    if (start_.is_min_value()) {
      start_.set_max_value();
    } else if (start_.is_max_value()) {
      start_.set_min_value();
    }
    if (end_.is_min_value()) {
      end_.set_max_value();
    } else if (end_.is_max_value()) {
      end_.set_min_value();
    }
  }
}

} }
