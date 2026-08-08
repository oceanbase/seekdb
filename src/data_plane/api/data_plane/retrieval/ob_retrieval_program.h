/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_DATA_PLANE_RETRIEVAL_OB_RETRIEVAL_PROGRAM_H_
#define OCEANBASE_DATA_PLANE_RETRIEVAL_OB_RETRIEVAL_PROGRAM_H_

#include <cmath>
#include <stdint.h>

#include "common/datum/ob_datum.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace data_plane
{
// Requests are staged into owned contiguous arrays.  This explicit ceiling
// keeps count validation and size multiplication bounded before any input is
// traversed or allocated.
static constexpr int64_t RETRIEVAL_MAX_REQUEST_ITEMS = 1024 * 1024;

namespace detail
{
class ObIRetrievalCorpusBinding;
}

// Document ids cross the seam as datum values; neither the interface nor its
// callers may assume an integer representation.  A view is borrowed for the
// duration documented by the enclosing request or batch.
struct ObRetrievalDocIdView
{
  ObRetrievalDocIdView() : datum_(nullptr) {}
  explicit ObRetrievalDocIdView(const common::ObDatum &datum) : datum_(&datum) {}

  bool is_valid() const
  {
    return nullptr != datum_ && !datum_->is_null() && !datum_->is_nop();
  }

  const common::ObDatum *datum_;
};

// Owned representation used by batches, lookup requests and staged runs.
// The bound matches the existing DAS domain-id protocol; oversized values
// fail instead of being truncated.
class ObRetrievalDocId
{
public:
  static constexpr int64_t MAX_DOC_ID_BYTES = 40;

  ObRetrievalDocId() : buffer_{}, datum_(buffer_, 0, false), valid_(false) {}
  ObRetrievalDocId(const ObRetrievalDocId &other)
    : buffer_{}, datum_(buffer_, 0, false), valid_(false)
  {
    copy_from(other);
  }
  ObRetrievalDocId(ObRetrievalDocId &&other)
    : buffer_{}, datum_(buffer_, 0, false), valid_(false)
  {
    copy_from(other);
    other.reset();
  }
  ~ObRetrievalDocId() = default;

  ObRetrievalDocId &operator=(const ObRetrievalDocId &other)
  {
    if (this != &other) {
      copy_from(other);
    }
    return *this;
  }
  ObRetrievalDocId &operator=(ObRetrievalDocId &&other)
  {
    if (this != &other) {
      copy_from(other);
      other.reset();
    }
    return *this;
  }

  int assign(const ObRetrievalDocIdView &view)
  {
    int ret = OB_SUCCESS;
    int64_t pos = 0;
    reset();
    if (OB_UNLIKELY(!view.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(datum_.deep_copy(
        *view.datum_, buffer_, MAX_DOC_ID_BYTES, pos))) {
      reset();
    } else {
      valid_ = true;
    }
    return ret;
  }

  void reset()
  {
    MEMSET(buffer_, 0, MAX_DOC_ID_BYTES);
    datum_ = common::ObDatum(buffer_, 0, false);
    valid_ = false;
  }

  bool is_valid() const { return valid_; }
  ObRetrievalDocIdView view() const
  {
    return valid_ ? ObRetrievalDocIdView(datum_) : ObRetrievalDocIdView();
  }
  const common::ObDatum &datum() const { return datum_; }

private:
  void copy_from(const ObRetrievalDocId &other)
  {
    reset();
    if (other.valid_) {
      MEMCPY(buffer_, other.buffer_, MAX_DOC_ID_BYTES);
      datum_ = other.datum_;
      datum_.ptr_ = buffer_;
      valid_ = true;
    }
  }

private:
  char buffer_[MAX_DOC_ID_BYTES];
  common::ObDatum datum_;
  bool valid_;
};

// Query-owned lowering produces versioned neutral intent.  It is never an
// encoded SQL pointer or object layout.  compile() deep-copies the bytes.
struct ObRetrievalIntentView
{
  ObRetrievalIntentView() : dialect_(0), version_(0), data_(nullptr), size_(0) {}

  bool is_empty() const { return 0 == size_; }
  bool is_valid() const
  {
    return (0 == size_ && 0 == dialect_ && 0 == version_ && nullptr == data_)
        || (size_ > 0 && dialect_ > 0 && version_ > 0 && nullptr != data_);
  }

  uint16_t dialect_;
  uint16_t version_;
  const uint8_t *data_;
  int64_t size_;
};

struct ObRetrievalTermView
{
  ObRetrievalTermView() : token_(nullptr), weight_(1.0) {}

  bool is_valid() const
  {
    return nullptr != token_ && !token_->is_null() && !token_->is_nop()
        && std::isfinite(weight_);
  }

  const common::ObDatum *token_;
  double weight_;
};

enum ObRetrievalQueryMode : uint8_t
{
  RETRIEVAL_QUERY_WEIGHTED_TERMS = 0,
  RETRIEVAL_QUERY_NATURAL_LANGUAGE,
  RETRIEVAL_QUERY_BOOLEAN
};

// These describe observable result behaviour, not implementation classes.
// The implementation remains free to use DAAT, BMW, TAAT or another engine.
enum ObRetrievalPlanIntent : uint8_t
{
  RETRIEVAL_ORDERED_SCAN = 0,
  RETRIEVAL_BOUNDED_TOP_K,
  RETRIEVAL_ACCUMULATE
};

enum ObRetrievalResultOrder : uint8_t
{
  RETRIEVAL_DOC_ID_ASC = 0,
  RETRIEVAL_SCORE_ASC_TIES_UNSPECIFIED,
  RETRIEVAL_UNSPECIFIED,
  RETRIEVAL_LOOKUP_INPUT_ORDER
};

struct ObRetrievalCompileRequest
{
  ObRetrievalCompileRequest()
    : terms_(nullptr),
      term_count_(0),
      query_mode_(RETRIEVAL_QUERY_WEIGHTED_TERMS),
      plan_intent_(RETRIEVAL_ORDERED_SCAN),
      minimum_should_match_(0),
      candidate_limit_(0),
      max_batch_rows_(1),
      field_boost_(1.0),
      need_query_max_score_(false),
      boolean_intent_(),
      filter_intent_()
  {}

  bool is_valid() const
  {
    bool valid = term_count_ >= 0
        && term_count_ <= RETRIEVAL_MAX_REQUEST_ITEMS
        && ((0 == term_count_ && nullptr == terms_)
            || (term_count_ > 0 && nullptr != terms_))
        && query_mode_ >= RETRIEVAL_QUERY_WEIGHTED_TERMS
        && query_mode_ <= RETRIEVAL_QUERY_BOOLEAN
        && plan_intent_ >= RETRIEVAL_ORDERED_SCAN
        && plan_intent_ <= RETRIEVAL_ACCUMULATE
        && minimum_should_match_ >= 0
        && minimum_should_match_ <= term_count_
        && candidate_limit_ >= 0
        && max_batch_rows_ > 0
        && max_batch_rows_ <= RETRIEVAL_MAX_REQUEST_ITEMS
        && std::isfinite(field_boost_)
        && field_boost_ > 0.0
        && boolean_intent_.is_valid()
        && filter_intent_.is_valid()
        && ((RETRIEVAL_QUERY_BOOLEAN == query_mode_ && !boolean_intent_.is_empty())
            || (RETRIEVAL_QUERY_BOOLEAN != query_mode_ && boolean_intent_.is_empty()));
    for (int64_t i = 0; valid && i < term_count_; ++i) {
      valid = terms_[i].is_valid();
    }
    return valid;
  }

  const ObRetrievalTermView *terms_;
  int64_t term_count_;
  ObRetrievalQueryMode query_mode_;
  ObRetrievalPlanIntent plan_intent_;
  int64_t minimum_should_match_;
  int64_t candidate_limit_;
  int64_t max_batch_rows_;
  double field_boost_;
  bool need_query_max_score_;
  ObRetrievalIntentView boolean_intent_;
  ObRetrievalIntentView filter_intent_;
};

struct ObRetrievalCompileInfo
{
  ObRetrievalCompileInfo()
    : scan_order_(RETRIEVAL_UNSPECIFIED), max_batch_rows_(0)
  {}
  ObRetrievalResultOrder scan_order_;
  int64_t max_batch_rows_;
};

struct ObRetrievalDocRangeView
{
  ObRetrievalDocRangeView()
    : lower_(), upper_(), has_lower_(false), has_upper_(false),
      include_lower_(true), include_upper_(true)
  {}

  bool is_valid() const
  {
    return (!has_lower_ || lower_.is_valid())
        && (!has_upper_ || upper_.is_valid());
  }

  ObRetrievalDocIdView lower_;
  ObRetrievalDocIdView upper_;
  bool has_lower_;
  bool has_upper_;
  bool include_lower_;
  bool include_upper_;
};

struct ObRetrievalLookupKeyView
{
  ObRetrievalLookupKeyView() : doc_id_(), input_ordinal_(-1) {}

  bool is_valid() const { return doc_id_.is_valid() && input_ordinal_ >= 0; }

  ObRetrievalDocIdView doc_id_;
  int64_t input_ordinal_;
};

enum ObRetrievalRunKind : uint8_t
{
  RETRIEVAL_SCAN_RUN = 0,
  RETRIEVAL_LOOKUP_RUN
};

struct ObRetrievalRunRequest
{
  ObRetrievalRunRequest()
    : kind_(RETRIEVAL_SCAN_RUN),
      ranges_(nullptr),
      range_count_(0),
      lookup_keys_(nullptr),
      lookup_key_count_(0),
      group_ordinal_(0),
      offset_(0),
      limit_(-1)
  {}

  bool is_valid() const
  {
    bool valid = kind_ >= RETRIEVAL_SCAN_RUN && kind_ <= RETRIEVAL_LOOKUP_RUN
        && range_count_ >= 0 && range_count_ <= RETRIEVAL_MAX_REQUEST_ITEMS
        && lookup_key_count_ >= 0
        && lookup_key_count_ <= RETRIEVAL_MAX_REQUEST_ITEMS
        && group_ordinal_ >= 0 && offset_ >= 0 && limit_ >= -1;
    if (valid && RETRIEVAL_SCAN_RUN == kind_) {
      valid = 0 == lookup_key_count_ && nullptr == lookup_keys_
          && ((0 == range_count_ && nullptr == ranges_)
              || (range_count_ > 0 && nullptr != ranges_));
      for (int64_t i = 0; valid && i < range_count_; ++i) {
        valid = ranges_[i].is_valid();
      }
    } else if (valid) {
      valid = 0 == range_count_ && nullptr == ranges_
          // Lookup is a positional total mapping: exactly one output is
          // produced for every input key, including misses and duplicates.
          && 0 == offset_ && -1 == limit_
          && ((0 == lookup_key_count_ && nullptr == lookup_keys_)
              || (lookup_key_count_ > 0 && nullptr != lookup_keys_));
      for (int64_t i = 0; valid && i < lookup_key_count_; ++i) {
        valid = lookup_keys_[i].is_valid();
      }
    }
    return valid;
  }

  ObRetrievalRunKind kind_;
  const ObRetrievalDocRangeView *ranges_;
  int64_t range_count_;
  const ObRetrievalLookupKeyView *lookup_keys_;
  int64_t lookup_key_count_;
  int64_t group_ordinal_;
  int64_t offset_;
  int64_t limit_;
};

struct ObRetrievalRunInfo
{
  ObRetrievalRunInfo()
    : actual_order_(RETRIEVAL_UNSPECIFIED), run_generation_(0)
  {}
  ObRetrievalResultOrder actual_order_;
  uint64_t run_generation_;
};

struct ObRetrievalMatch
{
  ObRetrievalMatch()
    : doc_id_(), score_(0.0), input_ordinal_(-1), matched_(true)
  {}
  ObRetrievalDocId doc_id_;
  double score_;
  int64_t input_ordinal_;
  bool matched_;
};

// The execution owns matches.  A batch view remains valid until the next
// non-const call on its program.  Lookup runs emit one match per input key,
// including duplicates and misses (matched_=false, score_=0).
struct ObRetrievalBatchView
{
  ObRetrievalBatchView()
    : matches_(nullptr), count_(0), end_(false),
      actual_order_(RETRIEVAL_UNSPECIFIED), run_generation_(0),
      has_query_max_score_(false), query_max_score_(0.0)
  {}

  const ObRetrievalMatch *matches_;
  int64_t count_;
  bool end_;
  ObRetrievalResultOrder actual_order_;
  uint64_t run_generation_;
  bool has_query_max_score_;
  double query_max_score_;
};

enum ObRetrievalProgramState : uint8_t
{
  RETRIEVAL_PROGRAM_EMPTY = 0,
  RETRIEVAL_PROGRAM_COMPILED,
  RETRIEVAL_PROGRAM_READY,
  RETRIEVAL_PROGRAM_RUNNING,
  RETRIEVAL_PROGRAM_END,
  RETRIEVAL_PROGRAM_FAILED
};

// Move-only ownership capsule.  Its provider-side SPI lives in the separate
// ob_retrieval_program_spi.h; ordinary callers cannot inspect or downcast it.
class ObRetrievalCorpus
{
public:
  ObRetrievalCorpus();
  ~ObRetrievalCorpus();
  ObRetrievalCorpus(ObRetrievalCorpus &&other);
  ObRetrievalCorpus &operator=(ObRetrievalCorpus &&other);

  bool is_valid() const;
  void reset();

private:
  ObRetrievalCorpus(const ObRetrievalCorpus &) = delete;
  ObRetrievalCorpus &operator=(const ObRetrievalCorpus &) = delete;
  void adopt(detail::ObIRetrievalCorpusBinding *binding);

private:
  detail::ObIRetrievalCorpusBinding *binding_;
  friend class ObRetrievalCorpusFactory;
  friend class ObRetrievalProgram;
};

// External deep-module facade.  Its allocator must outlive the Program.
// compile() owns immutable query intent;
// start() stages one run on the current or replacement corpus; pull() is the
// only data operation.  No posting, scorer, algorithm, scan or storage type is
// part of this interface.
class ObRetrievalProgram
{
public:
  ObRetrievalProgram();
  ~ObRetrievalProgram();
  ObRetrievalProgram(ObRetrievalProgram &&other);
  ObRetrievalProgram &operator=(ObRetrievalProgram &&other);

  static int compile(
      common::ObIAllocator &allocator,
      const ObRetrievalCompileRequest &request,
      ObRetrievalProgram &program,
      ObRetrievalCompileInfo *info = nullptr);

  // Same-corpus restart.  The request is deep-copied before it is opened.
  int start(const ObRetrievalRunRequest &run, ObRetrievalRunInfo &info);

  // Initial bind or transactional corpus replacement.  candidate is consumed
  // only after a new execution is completely staged; failure leaves both the
  // current program and candidate unchanged.
  int start(
      const ObRetrievalRunRequest &run,
      ObRetrievalCorpus &candidate,
      ObRetrievalRunInfo &info);

  int pull(const int64_t max_rows, ObRetrievalBatchView &batch);

  bool is_valid() const;
  ObRetrievalProgramState state() const;
  int first_error() const;
  void reset();

private:
  ObRetrievalProgram(const ObRetrievalProgram &) = delete;
  ObRetrievalProgram &operator=(const ObRetrievalProgram &) = delete;
  class Impl;
  Impl *impl_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_RETRIEVAL_OB_RETRIEVAL_PROGRAM_H_
