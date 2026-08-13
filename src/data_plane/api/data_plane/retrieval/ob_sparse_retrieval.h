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

#ifndef OCEANBASE_DATA_PLANE_RETRIEVAL_OB_SPARSE_RETRIEVAL_H_
#define OCEANBASE_DATA_PLANE_RETRIEVAL_OB_SPARSE_RETRIEVAL_H_

#include "common/datum/ob_datum.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace data_plane
{

// A source-owned id view.  It remains valid until the next mutating call on
// the source that returned it (next/advance/reuse/reset/destroy).
struct ObSparseRetrievalIdView
{
  ObSparseRetrievalIdView() : datum_(nullptr) {}
  explicit ObSparseRetrievalIdView(const common::ObDatum &datum) : datum_(&datum) {}
  bool is_valid() const { return nullptr != datum_; }
  const common::ObDatum *datum_;
  TO_STRING_KV(KP_(datum));
};

// Owned id used whenever a match survives a source call.  This deliberately
// has the same bounded representation as the existing domain-id protocol.
class ObSparseRetrievalId
{
public:
  static constexpr int64_t MAX_ID_BYTES = 40;

  ObSparseRetrievalId() : buffer_{}, datum_(buffer_, 0, false) {}
  ObSparseRetrievalId(const ObSparseRetrievalId &other)
    : buffer_{}, datum_(other.datum_)
  {
    MEMCPY(buffer_, other.buffer_, MAX_ID_BYTES);
    datum_.ptr_ = buffer_;
  }
  ~ObSparseRetrievalId() = default;

  ObSparseRetrievalId &operator=(const ObSparseRetrievalId &other)
  {
    if (this != &other) {
      MEMCPY(buffer_, other.buffer_, MAX_ID_BYTES);
      datum_ = other.datum_;
      datum_.ptr_ = buffer_;
    }
    return *this;
  }

  int assign(const ObSparseRetrievalIdView &view)
  {
    int ret = OB_SUCCESS;
    int64_t pos = 0;
    if (OB_ISNULL(view.datum_)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMSET(buffer_, 0, MAX_ID_BYTES);
      ret = datum_.deep_copy(*view.datum_, buffer_, MAX_ID_BYTES, pos);
      if (OB_SUCCESS != ret) {
        reset();
      }
    }
    return ret;
  }

  void reset()
  {
    MEMSET(buffer_, 0, MAX_ID_BYTES);
    datum_ = common::ObDatum(buffer_, 0, false);
  }
  ObSparseRetrievalIdView view() const { return ObSparseRetrievalIdView(datum_); }
  const common::ObDatum &datum() const { return datum_; }
  TO_STRING_KV(K_(datum));

private:
  char buffer_[MAX_ID_BYTES];
  common::ObDatum datum_;
};

struct ObSparseRetrievalEntryView
{
  ObSparseRetrievalEntryView() : id_(), score_(0.0) {}
  ObSparseRetrievalIdView id_;
  double score_;
  TO_STRING_KV(K_(id), K_(score));
};

struct ObSparseRetrievalMatch
{
  ObSparseRetrievalMatch() : id_(), score_(0.0) {}
  ObSparseRetrievalId id_;
  double score_;
  TO_STRING_KV(K_(id), K_(score));
};

// A block-source-owned score bound for the closed id interval [min_id_,
// max_id_].  The views remain valid until the next mutating call on the block
// source that returned them.
struct ObSparseRetrievalBlockView
{
  ObSparseRetrievalBlockView()
    : min_id_(), max_id_(), score_upper_bound_(0.0)
  {}
  ObSparseRetrievalIdView min_id_;
  ObSparseRetrievalIdView max_id_;
  double score_upper_bound_;
  TO_STRING_KV(K_(min_id), K_(max_id), K_(score_upper_bound));
};

// Every source must emit strictly increasing, unique ids.  advance_to is
// monotonic and returns the first id >= target.  OB_ITER_END is terminal;
// any other read error is sticky until reuse succeeds.
class ObISparseRetrievalSource
{
public:
  virtual ~ObISparseRetrievalSource() = default;
  virtual int next(ObSparseRetrievalEntryView &entry) = 0;
  virtual int advance_to(
      const ObSparseRetrievalIdView &target,
      ObSparseRetrievalEntryView &entry) = 0;
  virtual int max_score(double &score) const
  {
    UNUSED(score);
    return OB_NOT_SUPPORTED;
  }
  virtual int reuse(const bool switch_source) = 0;
  virtual void reset() = 0;
  virtual void destroy() = 0;
  VIRTUAL_TO_STRING_KV(KP(this));
};

// Block navigation is deliberately independent from the exact posting-list
// cursor.  advance_to never changes the paired ObISparseRetrievalSource and
// returns the first block intersecting [target, +inf) when inclusive is true,
// or (target, +inf) otherwise.  OB_ITER_END is terminal; every other read
// error is sticky until reuse succeeds.
class ObISparseRetrievalBlockSource
{
public:
  virtual ~ObISparseRetrievalBlockSource() = default;
  virtual int max_score(double &score) = 0;
  virtual int advance_to(
      const ObSparseRetrievalIdView &target,
      const bool inclusive,
      ObSparseRetrievalBlockView &block) = 0;
  virtual int reuse(const bool switch_source) = 0;
  virtual void reset() = 0;
  virtual void destroy() = 0;
  VIRTUAL_TO_STRING_KV(KP(this));
};

// One ordering policy is shared by every source and the merge core.
class ObISparseRetrievalIdOps
{
public:
  virtual ~ObISparseRetrievalIdOps() = default;
  virtual int compare(
      const ObSparseRetrievalIdView &left,
      const ObSparseRetrievalIdView &right,
      int &cmp_result) const = 0;
  virtual void destroy() = 0;
  VIRTUAL_TO_STRING_KV(KP(this));
};

// Optional dynamic pre-filter.  Implementations may reference mutable state,
// but the port object itself is owned by the cursor after factory success.
class ObISparseRetrievalFilter
{
public:
  virtual ~ObISparseRetrievalFilter() = default;
  virtual int accept(const ObSparseRetrievalIdView &id, bool &accepted) const = 0;
  virtual void destroy() = 0;
  VIRTUAL_TO_STRING_KV(KP(this));
};

struct ObSparseRetrievalDaaTRequest
{
  ObSparseRetrievalDaaTRequest()
    : allocator_(nullptr),
      sources_(nullptr),
      id_ops_(nullptr),
      filter_(nullptr),
      dimension_weights_(nullptr),
      candidate_limit_(0),
      max_batch_size_(1)
  {}

  // The source pointer values are transferred only when create_daat succeeds;
  // the source/weight containers themselves are inspected only during create.
  common::ObIAllocator *allocator_;
  common::ObIArray<ObISparseRetrievalSource *> *sources_;
  ObISparseRetrievalIdOps *id_ops_;
  ObISparseRetrievalFilter *filter_;
  const common::ObIArray<double> *dimension_weights_;
  int64_t candidate_limit_;
  int64_t max_batch_size_;
};

struct ObSparseRetrievalBlockMaxWandRequest
{
  ObSparseRetrievalBlockMaxWandRequest()
    : allocator_(nullptr),
      sources_(nullptr),
      block_sources_(nullptr),
      id_ops_(nullptr),
      filter_(nullptr),
      dimension_weights_(nullptr),
      candidate_limit_(0),
      max_batch_size_(1)
  {}

  // Pointer values are transferred only when create_block_max_wand succeeds;
  // the containers and weights are copied/inspected during the create call.
  common::ObIAllocator *allocator_;
  common::ObIArray<ObISparseRetrievalSource *> *sources_;
  common::ObIArray<ObISparseRetrievalBlockSource *> *block_sources_;
  ObISparseRetrievalIdOps *id_ops_;
  ObISparseRetrievalFilter *filter_;
  const common::ObIArray<double> *dimension_weights_;
  int64_t candidate_limit_;
  int64_t max_batch_size_;
};

class ObSparseRetrievalCursor
{
public:
  virtual ~ObSparseRetrievalCursor() = default;
  virtual int next(const ObSparseRetrievalMatch *&match) = 0;
  virtual int next_batch(
      const int64_t capacity,
      const ObSparseRetrievalMatch *&matches,
      int64_t &count) = 0;
  virtual int reuse(const bool switch_source = false) = 0;
  virtual void reset() = 0;
  virtual void destroy() = 0;
  VIRTUAL_TO_STRING_KV(KP(this));
};

class ObSparseRetrievalHandle
{
public:
  ObSparseRetrievalHandle() : cursor_(nullptr) {}
  ~ObSparseRetrievalHandle() { reset(); }

  bool is_valid() const { return nullptr != cursor_; }
  int next(const ObSparseRetrievalMatch *&match)
  {
    return is_valid() ? cursor_->next(match) : OB_NOT_INIT;
  }
  int next_batch(
      const int64_t capacity,
      const ObSparseRetrievalMatch *&matches,
      int64_t &count)
  {
    return is_valid() ? cursor_->next_batch(capacity, matches, count) : OB_NOT_INIT;
  }
  int reuse(const bool switch_source = false)
  {
    return is_valid() ? cursor_->reuse(switch_source) : OB_NOT_INIT;
  }
  void reset()
  {
    if (nullptr != cursor_) {
      cursor_->destroy();
      cursor_ = nullptr;
    }
  }

private:
  void adopt(ObSparseRetrievalCursor *cursor) { cursor_ = cursor; }
  ObSparseRetrievalCursor *cursor_;
  friend class ObSparseRetrievalFactory;
  DISALLOW_COPY_AND_ASSIGN(ObSparseRetrievalHandle);
};

class ObSparseRetrievalFactory
{
public:
  // On success the returned handle owns every port in request.  On failure
  // ownership remains with the caller.
  static int create_daat(
      const ObSparseRetrievalDaaTRequest &request,
      ObSparseRetrievalHandle &handle);
  static int create_block_max_wand(
      const ObSparseRetrievalBlockMaxWandRequest &request,
      ObSparseRetrievalHandle &handle);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_RETRIEVAL_OB_SPARSE_RETRIEVAL_H_
