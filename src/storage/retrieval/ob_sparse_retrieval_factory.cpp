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

#define USING_LOG_PREFIX STORAGE

#include "data_plane/retrieval/ob_sparse_retrieval.h"

#include <cmath>

#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

enum class CursorState : uint8_t
{
  READY = 0,
  MATERIALIZED,
  EXHAUSTED,
  FAILED,
  RESET,
};

struct SourceState
{
  SourceState() : entry_(), has_entry_(false), exhausted_(false) {}
  ObSparseRetrievalEntryView entry_;
  bool has_entry_;
  bool exhausted_;
  TO_STRING_KV(K_(entry), K_(has_entry), K_(exhausted));
};

class ObSparseRetrievalDaaTCursor final : public ObSparseRetrievalCursor
{
public:
  explicit ObSparseRetrievalDaaTCursor(common::ObIAllocator &allocator)
    : allocator_(&allocator),
      sources_(),
      source_states_(),
      id_ops_(nullptr),
      filter_(nullptr),
      dimension_weights_(),
      candidate_limit_(0),
      max_batch_size_(1),
      results_(),
      result_index_(0),
      saved_error_(OB_SUCCESS),
      state_(CursorState::RESET),
      owns_ports_(false)
  {}
  virtual ~ObSparseRetrievalDaaTCursor() = default;

  int init(const ObSparseRetrievalDaaTRequest &request);
  virtual int next(const ObSparseRetrievalMatch *&match) override;
  virtual int next_batch(
      const int64_t capacity,
      const ObSparseRetrievalMatch *&matches,
      int64_t &count) override;
  virtual int reuse(const bool switch_source = false) override;
  virtual void reset() override;
  virtual void destroy() override;

private:
  int materialize();
  int load_missing_entries(bool &all_exhausted);
  int find_min_source(int64_t &min_source_idx);
  int collect_current_id(const int64_t min_source_idx, ObSparseRetrievalMatch &match);
  int retain_candidate(const ObSparseRetrievalMatch &match);
  int sift_up(const int64_t start_idx);
  int sift_down(const int64_t heap_size, const int64_t start_idx);
  int sort_results();
  int compare_matches(
      const ObSparseRetrievalMatch &left,
      const ObSparseRetrievalMatch &right,
      int &cmp_result) const;
  int fail(const int error);
  void clear_algorithm_state();

private:
  common::ObIAllocator *allocator_;
  common::ObSEArray<ObISparseRetrievalSource *, 4> sources_;
  common::ObSEArray<SourceState, 4> source_states_;
  ObISparseRetrievalIdOps *id_ops_;
  ObISparseRetrievalFilter *filter_;
  common::ObSEArray<double, 4> dimension_weights_;
  int64_t candidate_limit_;
  int64_t max_batch_size_;
  common::ObSEArray<ObSparseRetrievalMatch, 16> results_;
  int64_t result_index_;
  int saved_error_;
  CursorState state_;
  bool owns_ports_;
};

int ObSparseRetrievalDaaTCursor::init(const ObSparseRetrievalDaaTRequest &request)
{
  int ret = OB_SUCCESS;
  if (CursorState::RESET != state_ || owns_ports_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("DAAT cursor initialized twice", K(ret));
  } else if (OB_ISNULL(request.allocator_)
      || OB_ISNULL(request.sources_)
      || OB_ISNULL(request.id_ops_)
      || request.candidate_limit_ < 0
      || request.max_batch_size_ <= 0
      || (OB_NOT_NULL(request.dimension_weights_)
          && request.dimension_weights_->count() != request.sources_->count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid DAAT request", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < request.sources_->count(); ++i) {
    if (OB_ISNULL(request.sources_->at(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("null DAAT source", K(ret), K(i));
    } else if (OB_FAIL(sources_.push_back(request.sources_->at(i)))) {
    } else if (OB_FAIL(source_states_.push_back(SourceState()))) {
    }
  }
  for (int64_t i = 0;
       OB_SUCC(ret) && OB_NOT_NULL(request.dimension_weights_)
           && i < request.dimension_weights_->count();
       ++i) {
    if (OB_FAIL(dimension_weights_.push_back(request.dimension_weights_->at(i)))) {
    }
  }
  if (OB_SUCC(ret)) {
    id_ops_ = request.id_ops_;
    filter_ = request.filter_;
    candidate_limit_ = request.candidate_limit_;
    max_batch_size_ = request.max_batch_size_;
    result_index_ = 0;
    saved_error_ = OB_SUCCESS;
    state_ = CursorState::READY;
    owns_ports_ = true;
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::fail(const int error)
{
  saved_error_ = error;
  state_ = CursorState::FAILED;
  return error;
}

int ObSparseRetrievalDaaTCursor::load_missing_entries(bool &all_exhausted)
{
  int ret = OB_SUCCESS;
  all_exhausted = true;
  for (int64_t i = 0; OB_SUCC(ret) && i < sources_.count(); ++i) {
    SourceState &source_state = source_states_.at(i);
    if (source_state.exhausted_) {
    } else if (!source_state.has_entry_) {
      ObSparseRetrievalEntryView entry;
      if (OB_FAIL(sources_.at(i)->next(entry))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          source_state.exhausted_ = true;
        } else {
          LOG_WARN("DAAT source failed", K(ret), K(i));
        }
      } else if (OB_UNLIKELY(!entry.id_.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("DAAT source returned an invalid id", K(ret), K(i));
      } else {
        source_state.entry_ = entry;
        source_state.has_entry_ = true;
      }
    }
    if (!source_state.exhausted_) {
      all_exhausted = false;
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::find_min_source(int64_t &min_source_idx)
{
  int ret = OB_SUCCESS;
  min_source_idx = -1;
  for (int64_t i = 0; OB_SUCC(ret) && i < source_states_.count(); ++i) {
    const SourceState &source_state = source_states_.at(i);
    if (!source_state.has_entry_) {
    } else if (min_source_idx < 0) {
      min_source_idx = i;
    } else {
      int cmp_result = 0;
      if (OB_FAIL(id_ops_->compare(
          source_state.entry_.id_,
          source_states_.at(min_source_idx).entry_.id_,
          cmp_result))) {
      } else if (cmp_result < 0) {
        min_source_idx = i;
      }
    }
  }
  if (OB_SUCC(ret) && min_source_idx < 0) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::collect_current_id(
    const int64_t min_source_idx,
    ObSparseRetrievalMatch &match)
{
  int ret = OB_SUCCESS;
  const ObSparseRetrievalIdView min_id = source_states_.at(min_source_idx).entry_.id_;
  match.score_ = 0.0;
  if (OB_FAIL(match.id_.assign(min_id))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < source_states_.count(); ++i) {
    SourceState &source_state = source_states_.at(i);
    if (!source_state.has_entry_) {
    } else {
      int cmp_result = 0;
      if (OB_FAIL(id_ops_->compare(source_state.entry_.id_, min_id, cmp_result))) {
      } else if (0 == cmp_result) {
        const double weight = dimension_weights_.empty()
            ? 1.0 : dimension_weights_.at(i);
        match.score_ += source_state.entry_.score_ * weight;
        source_state.has_entry_ = false;
      }
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::retain_candidate(const ObSparseRetrievalMatch &match)
{
  int ret = OB_SUCCESS;
  bool accepted = true;
  if (nullptr != filter_ && OB_FAIL(filter_->accept(match.id_.view(), accepted))) {
    LOG_WARN("DAAT filter failed", K(ret));
  } else if (!accepted || 0 == candidate_limit_) {
  } else if (results_.count() < candidate_limit_) {
    if (OB_FAIL(results_.push_back(match))) {
    } else if (OB_FAIL(sift_up(results_.count() - 1))) {
    }
  } else {
    // The root is the worst retained match.  Keeping that invariant makes
    // every threshold decision O(log K), while still allowing comparator
    // failures to propagate through the integer-returning heap operations.
    int cmp_result = 0;
    if (OB_FAIL(compare_matches(match, results_.at(0), cmp_result))) {
    } else if (cmp_result < 0) {
      // match is worse than the retained threshold
    } else if (cmp_result > 0) {
      results_.at(0) = match;
      if (OB_FAIL(sift_down(results_.count(), 0))) {
      }
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::sift_up(const int64_t start_idx)
{
  int ret = OB_SUCCESS;
  int64_t child_idx = start_idx;
  while (OB_SUCC(ret) && child_idx > 0) {
    const int64_t parent_idx = (child_idx - 1) / 2;
    int cmp_result = 0;
    if (OB_FAIL(compare_matches(
        results_.at(child_idx), results_.at(parent_idx), cmp_result))) {
    } else if (cmp_result >= 0) {
      break;
    } else {
      const ObSparseRetrievalMatch tmp = results_.at(parent_idx);
      results_.at(parent_idx) = results_.at(child_idx);
      results_.at(child_idx) = tmp;
      child_idx = parent_idx;
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::sift_down(
    const int64_t heap_size,
    const int64_t start_idx)
{
  int ret = OB_SUCCESS;
  int64_t parent_idx = start_idx;
  while (OB_SUCC(ret)) {
    const int64_t left_idx = parent_idx * 2 + 1;
    if (left_idx >= heap_size) {
      break;
    }
    int64_t worst_child_idx = left_idx;
    const int64_t right_idx = left_idx + 1;
    if (right_idx < heap_size) {
      int cmp_result = 0;
      if (OB_FAIL(compare_matches(
          results_.at(right_idx), results_.at(left_idx), cmp_result))) {
      } else if (cmp_result < 0) {
        worst_child_idx = right_idx;
      }
    }
    if (OB_SUCC(ret)) {
      int cmp_result = 0;
      if (OB_FAIL(compare_matches(
          results_.at(worst_child_idx), results_.at(parent_idx), cmp_result))) {
      } else if (cmp_result >= 0) {
        break;
      } else {
        const ObSparseRetrievalMatch tmp = results_.at(parent_idx);
        results_.at(parent_idx) = results_.at(worst_child_idx);
        results_.at(worst_child_idx) = tmp;
        parent_idx = worst_child_idx;
      }
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::compare_matches(
    const ObSparseRetrievalMatch &left,
    const ObSparseRetrievalMatch &right,
    int &cmp_result) const
{
  int ret = OB_SUCCESS;
  if (left.score_ > right.score_) {
    cmp_result = 1;
  } else if (left.score_ < right.score_) {
    cmp_result = -1;
  } else {
    int id_cmp = 0;
    if (OB_FAIL(id_ops_->compare(left.id_.view(), right.id_.view(), id_cmp))) {
    } else {
      // A smaller id wins a score tie.
      cmp_result = -id_cmp;
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::sort_results()
{
  int ret = OB_SUCCESS;
  // Repeatedly move the worst heap root to the tail.  The remaining prefix
  // stays a worst-root heap, so the finished array is best-first with the
  // same score-desc/id-asc policy used for retention.
  for (int64_t heap_size = results_.count(); OB_SUCC(ret) && heap_size > 1; --heap_size) {
    const ObSparseRetrievalMatch tmp = results_.at(0);
    results_.at(0) = results_.at(heap_size - 1);
    results_.at(heap_size - 1) = tmp;
    if (OB_FAIL(sift_down(heap_size - 1, 0))) {
    }
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::materialize()
{
  int ret = OB_SUCCESS;
  bool all_exhausted = false;
  while (OB_SUCC(ret) && !all_exhausted) {
    int64_t min_source_idx = -1;
    ObSparseRetrievalMatch match;
    if (OB_FAIL(load_missing_entries(all_exhausted))) {
    } else if (all_exhausted) {
    } else if (OB_FAIL(find_min_source(min_source_idx))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        all_exhausted = true;
      } else {
        LOG_WARN("failed to find next DAAT id", K(ret));
      }
    } else if (OB_FAIL(collect_current_id(min_source_idx, match))) {
    } else if (OB_FAIL(retain_candidate(match))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(sort_results())) {
    LOG_WARN("failed to sort DAAT results", K(ret));
  }
  if (OB_SUCC(ret)) {
    result_index_ = 0;
    state_ = CursorState::MATERIALIZED;
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::next(const ObSparseRetrievalMatch *&match)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  const ObSparseRetrievalMatch *matches = nullptr;
  match = nullptr;
  if (OB_FAIL(next_batch(1, matches, count))) {
  } else if (OB_UNLIKELY(1 != count || OB_ISNULL(matches))) {
    ret = fail(OB_ERR_UNEXPECTED);
    LOG_WARN("unexpected DAAT single-row result", K(ret), K(count));
  } else {
    match = matches;
  }
  return ret;
}

int ObSparseRetrievalDaaTCursor::next_batch(
    const int64_t capacity,
    const ObSparseRetrievalMatch *&matches,
    int64_t &count)
{
  int ret = OB_SUCCESS;
  matches = nullptr;
  count = 0;
  if (CursorState::FAILED == state_) {
    ret = saved_error_;
  } else if (CursorState::RESET == state_) {
    ret = OB_NOT_INIT;
  } else if (capacity < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 == capacity) {
  } else if (CursorState::READY == state_ && OB_FAIL(materialize())) {
    ret = fail(ret);
  }
  if (OB_FAIL(ret)) {
  } else if (CursorState::EXHAUSTED == state_ || result_index_ >= results_.count()) {
    state_ = CursorState::EXHAUSTED;
    ret = OB_ITER_END;
  } else {
    count = MIN(MIN(capacity, max_batch_size_), results_.count() - result_index_);
    matches = &results_.at(result_index_);
    result_index_ += count;
  }
  return ret;
}

void ObSparseRetrievalDaaTCursor::clear_algorithm_state()
{
  for (int64_t i = 0; i < source_states_.count(); ++i) {
    source_states_.at(i) = SourceState();
  }
  results_.reuse();
  result_index_ = 0;
  saved_error_ = OB_SUCCESS;
}

int ObSparseRetrievalDaaTCursor::reuse(const bool switch_source)
{
  int ret = OB_SUCCESS;
  if (!owns_ports_) {
    ret = OB_NOT_INIT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources_.count(); ++i) {
    if (OB_FAIL(sources_.at(i)->reuse(switch_source))) {
    }
  }
  clear_algorithm_state();
  if (OB_SUCC(ret)) {
    state_ = CursorState::READY;
  } else {
    fail(ret);
  }
  return ret;
}

void ObSparseRetrievalDaaTCursor::reset()
{
  if (owns_ports_) {
    for (int64_t i = 0; i < sources_.count(); ++i) {
      sources_.at(i)->reset();
    }
  }
  clear_algorithm_state();
  state_ = CursorState::RESET;
}

void ObSparseRetrievalDaaTCursor::destroy()
{
  common::ObIAllocator *allocator = allocator_;
  reset();
  if (owns_ports_) {
    for (int64_t i = 0; i < sources_.count(); ++i) {
      sources_.at(i)->destroy();
    }
    if (nullptr != id_ops_) {
      id_ops_->destroy();
    }
    if (nullptr != filter_) {
      filter_->destroy();
    }
    owns_ports_ = false;
  }
  this->~ObSparseRetrievalDaaTCursor();
  if (nullptr != allocator) {
    allocator->free(this);
  }
}

class ObSparseRetrievalBMWCursor final : public ObSparseRetrievalCursor
{
public:
  explicit ObSparseRetrievalBMWCursor(common::ObIAllocator &allocator)
    : allocator_(&allocator),
      sources_(),
      block_sources_(),
      source_states_(),
      id_ops_(nullptr),
      filter_(nullptr),
      dimension_weights_(),
      global_max_scores_(),
      candidate_limit_(0),
      max_batch_size_(1),
      results_(),
      result_index_(0),
      saved_error_(OB_SUCCESS),
      state_(CursorState::RESET),
      blocks_prepared_(false),
      owns_ports_(false)
  {}
  virtual ~ObSparseRetrievalBMWCursor() = default;

  int init(const ObSparseRetrievalBlockMaxWandRequest &request);
  virtual int next(const ObSparseRetrievalMatch *&match) override;
  virtual int next_batch(
      const int64_t capacity,
      const ObSparseRetrievalMatch *&matches,
      int64_t &count) override;
  virtual int reuse(const bool switch_source = false) override;
  virtual void reset() override;
  virtual void destroy() override;

private:
  int materialize();
  int load_missing_entries(bool &all_exhausted);
  int find_min_source(int64_t &min_source_idx);
  int collect_current_id(const int64_t min_source_idx, ObSparseRetrievalMatch &match);
  int prepare_blocks();
  int try_prune_range(
      const ObSparseRetrievalIdView &start,
      bool &pruned,
      bool &finished);
  int advance_sources_to_boundary(
      const ObSparseRetrievalIdView &boundary,
      const bool inclusive);
  int advance_one_source(
      const int64_t source_idx,
      const ObSparseRetrievalIdView &boundary,
      const bool inclusive);
  int retain_candidate(const ObSparseRetrievalMatch &match);
  int sift_up(const int64_t start_idx);
  int sift_down(const int64_t heap_size, const int64_t start_idx);
  int sort_results();
  int compare_matches(
      const ObSparseRetrievalMatch &left,
      const ObSparseRetrievalMatch &right,
      int &cmp_result) const;
  double threshold() const { return results_.empty() ? 0.0 : results_.at(0).score_; }
  double weight_at(const int64_t idx) const
  {
    return dimension_weights_.empty() ? 1.0 : dimension_weights_.at(idx);
  }
  int fail(const int error);
  void clear_algorithm_state();

private:
  common::ObIAllocator *allocator_;
  common::ObSEArray<ObISparseRetrievalSource *, 4> sources_;
  common::ObSEArray<ObISparseRetrievalBlockSource *, 4> block_sources_;
  common::ObSEArray<SourceState, 4> source_states_;
  ObISparseRetrievalIdOps *id_ops_;
  ObISparseRetrievalFilter *filter_;
  common::ObSEArray<double, 4> dimension_weights_;
  common::ObSEArray<double, 4> global_max_scores_;
  int64_t candidate_limit_;
  int64_t max_batch_size_;
  common::ObSEArray<ObSparseRetrievalMatch, 16> results_;
  int64_t result_index_;
  int saved_error_;
  CursorState state_;
  bool blocks_prepared_;
  bool owns_ports_;
};

int ObSparseRetrievalBMWCursor::init(
    const ObSparseRetrievalBlockMaxWandRequest &request)
{
  int ret = OB_SUCCESS;
  if (CursorState::RESET != state_ || owns_ports_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("BMW cursor initialized twice", K(ret));
  } else if (OB_ISNULL(request.allocator_)
      || OB_ISNULL(request.sources_)
      || OB_ISNULL(request.block_sources_)
      || OB_ISNULL(request.id_ops_)
      || request.sources_->count() != request.block_sources_->count()
      || request.candidate_limit_ < 0
      || request.max_batch_size_ <= 0
      || (OB_NOT_NULL(request.dimension_weights_)
          && request.dimension_weights_->count() != request.sources_->count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid BMW request", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < request.sources_->count(); ++i) {
    const double weight = OB_ISNULL(request.dimension_weights_)
        ? 1.0 : request.dimension_weights_->at(i);
    if (OB_ISNULL(request.sources_->at(i))
        || OB_ISNULL(request.block_sources_->at(i))
        || !std::isfinite(weight)
        || weight < 0.0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid BMW dimension", K(ret), K(i), K(weight));
    } else if (OB_FAIL(sources_.push_back(request.sources_->at(i)))) {
    } else if (OB_FAIL(block_sources_.push_back(request.block_sources_->at(i)))) {
    } else if (OB_FAIL(source_states_.push_back(SourceState()))) {
    } else if (OB_FAIL(global_max_scores_.push_back(0.0))) {
    } else if (OB_NOT_NULL(request.dimension_weights_)
        && OB_FAIL(dimension_weights_.push_back(weight))) {
      LOG_WARN("failed to copy BMW dimension weight", K(ret), K(i));
    }
  }
  if (OB_SUCC(ret)) {
    id_ops_ = request.id_ops_;
    filter_ = request.filter_;
    candidate_limit_ = request.candidate_limit_;
    max_batch_size_ = request.max_batch_size_;
    result_index_ = 0;
    saved_error_ = OB_SUCCESS;
    state_ = CursorState::READY;
    blocks_prepared_ = false;
    owns_ports_ = true;
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::fail(const int error)
{
  saved_error_ = error;
  state_ = CursorState::FAILED;
  return error;
}

int ObSparseRetrievalBMWCursor::load_missing_entries(bool &all_exhausted)
{
  int ret = OB_SUCCESS;
  all_exhausted = true;
  for (int64_t i = 0; OB_SUCC(ret) && i < sources_.count(); ++i) {
    SourceState &source_state = source_states_.at(i);
    if (source_state.exhausted_) {
    } else if (!source_state.has_entry_) {
      ObSparseRetrievalEntryView entry;
      if (OB_FAIL(sources_.at(i)->next(entry))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          source_state.exhausted_ = true;
        } else {
          LOG_WARN("BMW exact source failed", K(ret), K(i));
        }
      } else if (!entry.id_.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("BMW exact source returned an invalid id", K(ret), K(i));
      } else {
        source_state.entry_ = entry;
        source_state.has_entry_ = true;
      }
    }
    if (!source_state.exhausted_) {
      all_exhausted = false;
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::find_min_source(int64_t &min_source_idx)
{
  int ret = OB_SUCCESS;
  min_source_idx = -1;
  for (int64_t i = 0; OB_SUCC(ret) && i < source_states_.count(); ++i) {
    const SourceState &source_state = source_states_.at(i);
    if (!source_state.has_entry_) {
    } else if (min_source_idx < 0) {
      min_source_idx = i;
    } else {
      int cmp_result = 0;
      if (OB_FAIL(id_ops_->compare(
          source_state.entry_.id_,
          source_states_.at(min_source_idx).entry_.id_,
          cmp_result))) {
      } else if (cmp_result < 0) {
        min_source_idx = i;
      }
    }
  }
  if (OB_SUCC(ret) && min_source_idx < 0) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::collect_current_id(
    const int64_t min_source_idx,
    ObSparseRetrievalMatch &match)
{
  int ret = OB_SUCCESS;
  const ObSparseRetrievalIdView min_id = source_states_.at(min_source_idx).entry_.id_;
  match.score_ = 0.0;
  if (OB_FAIL(match.id_.assign(min_id))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < source_states_.count(); ++i) {
    SourceState &source_state = source_states_.at(i);
    if (!source_state.has_entry_) {
    } else {
      int cmp_result = 0;
      if (OB_FAIL(id_ops_->compare(source_state.entry_.id_, min_id, cmp_result))) {
      } else if (0 == cmp_result) {
        match.score_ += source_state.entry_.score_ * weight_at(i);
        source_state.has_entry_ = false;
      }
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::prepare_blocks()
{
  int ret = OB_SUCCESS;
  if (blocks_prepared_) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < block_sources_.count(); ++i) {
      double max_score = 0.0;
      if (OB_FAIL(block_sources_.at(i)->max_score(max_score))) {
      } else if (!std::isfinite(max_score)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("BMW global bound is not finite", K(ret), K(i), K(max_score));
      } else {
        // A dimension absent from a document contributes zero, so zero is
        // always a safe upper bound when a physical score bound is negative.
        global_max_scores_.at(i) = OB_MAX(0.0, max_score * weight_at(i));
      }
    }
    if (OB_SUCC(ret)) {
      blocks_prepared_ = true;
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::advance_one_source(
    const int64_t source_idx,
    const ObSparseRetrievalIdView &boundary,
    const bool inclusive)
{
  int ret = OB_SUCCESS;
  SourceState &source_state = source_states_.at(source_idx);
  if (!source_state.has_entry_ || source_state.exhausted_) {
  } else {
    int cmp_result = 0;
    if (OB_FAIL(id_ops_->compare(source_state.entry_.id_, boundary, cmp_result))) {
    } else if (cmp_result > 0 || (0 == cmp_result && !inclusive)) {
      // This source already starts after the pruned interval.
    } else {
      ObSparseRetrievalEntryView entry;
      if (cmp_result < 0 && OB_FAIL(sources_.at(source_idx)->advance_to(boundary, entry))) {
      } else if (0 == cmp_result) {
        entry = source_state.entry_;
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        source_state.has_entry_ = false;
        source_state.exhausted_ = true;
      } else if (OB_FAIL(ret)) {
      } else if (!entry.id_.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("BMW exact advance returned an invalid id", K(ret), K(source_idx));
      } else {
        if (OB_FAIL(id_ops_->compare(entry.id_, boundary, cmp_result))) {
        } else if (cmp_result < 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("BMW exact source did not reach its boundary", K(ret), K(source_idx));
        } else if (0 == cmp_result && inclusive) {
          if (OB_FAIL(sources_.at(source_idx)->next(entry))) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              source_state.has_entry_ = false;
              source_state.exhausted_ = true;
            } else {
              LOG_WARN("failed to pass inclusive BMW boundary", K(ret), K(source_idx));
            }
          } else if (!entry.id_.is_valid()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("BMW exact source returned an invalid post-boundary id", K(ret));
          } else {
            source_state.entry_ = entry;
            source_state.has_entry_ = true;
          }
        } else {
          source_state.entry_ = entry;
          source_state.has_entry_ = true;
        }
      }
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::advance_sources_to_boundary(
    const ObSparseRetrievalIdView &boundary,
    const bool inclusive)
{
  int ret = OB_SUCCESS;
  ObSparseRetrievalId owned_boundary;
  if (OB_FAIL(owned_boundary.assign(boundary))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources_.count(); ++i) {
    if (OB_FAIL(advance_one_source(i, owned_boundary.view(), inclusive))) {
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::try_prune_range(
    const ObSparseRetrievalIdView &start,
    bool &pruned,
    bool &finished)
{
  int ret = OB_SUCCESS;
  double global_upper_bound = 0.0;
  double block_upper_bound = 0.0;
  bool has_boundary = false;
  bool boundary_inclusive = false;
  ObSparseRetrievalId boundary;
  pruned = false;
  finished = false;

  for (int64_t i = 0; i < global_max_scores_.count(); ++i) {
    global_upper_bound += global_max_scores_.at(i);
  }
  if (global_upper_bound <= threshold()) {
    finished = true;
  }

  for (int64_t i = 0; OB_SUCC(ret) && !finished && i < block_sources_.count(); ++i) {
    ObSparseRetrievalBlockView block;
    int block_ret = block_sources_.at(i)->advance_to(start, true, block);
    if (OB_ITER_END == block_ret) {
      if (!source_states_.at(i).exhausted_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("BMW block source ended before its exact source", K(ret), K(i));
      }
    } else if (OB_SUCCESS != block_ret) {
      ret = block_ret;
      LOG_WARN("BMW block source failed", K(ret), K(i));
    } else if (!block.min_id_.is_valid() || !block.max_id_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("BMW block source returned an invalid interval", K(ret), K(i));
    } else if (!std::isfinite(block.score_upper_bound_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("BMW block bound is not finite", K(ret), K(i), K(block.score_upper_bound_));
    } else {
      int range_cmp = 0;
      int min_cmp = 0;
      int max_cmp = 0;
      if (OB_FAIL(id_ops_->compare(block.min_id_, block.max_id_, range_cmp))) {
      } else if (range_cmp > 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("BMW block source returned a reversed interval", K(ret), K(i));
      } else if (OB_FAIL(id_ops_->compare(block.min_id_, start, min_cmp))) {
      } else if (OB_FAIL(id_ops_->compare(block.max_id_, start, max_cmp))) {
      } else if (max_cmp < 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("BMW block source did not reach the requested id", K(ret), K(i));
      } else {
        const bool covers_start = min_cmp <= 0;
        if (!covers_start && source_states_.at(i).has_entry_) {
          int exact_to_block_cmp = 0;
          if (OB_FAIL(id_ops_->compare(
              source_states_.at(i).entry_.id_, block.min_id_, exact_to_block_cmp))) {
          } else if (exact_to_block_cmp < 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("BMW exact posting is not covered by a block", K(ret), K(i));
          }
        }
        const ObSparseRetrievalIdView candidate_boundary = covers_start
            ? block.max_id_ : block.min_id_;
        const bool candidate_inclusive = covers_start;
        if (OB_SUCC(ret) && covers_start) {
          block_upper_bound += OB_MAX(
              0.0, block.score_upper_bound_ * weight_at(i));
        }
        bool select_boundary = !has_boundary;
        if (OB_SUCC(ret) && has_boundary) {
          int boundary_cmp = 0;
          if (OB_FAIL(id_ops_->compare(
              candidate_boundary, boundary.view(), boundary_cmp))) {
          } else {
            select_boundary = boundary_cmp < 0
                || (0 == boundary_cmp && boundary_inclusive && !candidate_inclusive);
          }
        }
        if (OB_SUCC(ret) && select_boundary) {
          if (OB_FAIL(boundary.assign(candidate_boundary))) {
          } else {
            boundary_inclusive = candidate_inclusive;
            has_boundary = true;
          }
        }
      }
    }
  }

  if (OB_SUCC(ret) && !finished && !has_boundary) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("BMW exact sources outlived every block source", K(ret));
  } else if (OB_SUCC(ret) && !finished && block_upper_bound <= threshold()) {
    if (OB_FAIL(advance_sources_to_boundary(boundary.view(), boundary_inclusive))) {
    } else {
      pruned = true;
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::compare_matches(
    const ObSparseRetrievalMatch &left,
    const ObSparseRetrievalMatch &right,
    int &cmp_result) const
{
  int ret = OB_SUCCESS;
  if (left.score_ > right.score_) {
    cmp_result = 1;
  } else if (left.score_ < right.score_) {
    cmp_result = -1;
  } else {
    int id_cmp = 0;
    if (OB_FAIL(id_ops_->compare(left.id_.view(), right.id_.view(), id_cmp))) {
    } else {
      cmp_result = -id_cmp;
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::sift_up(const int64_t start_idx)
{
  int ret = OB_SUCCESS;
  int64_t child_idx = start_idx;
  while (OB_SUCC(ret) && child_idx > 0) {
    const int64_t parent_idx = (child_idx - 1) / 2;
    int cmp_result = 0;
    if (OB_FAIL(compare_matches(
        results_.at(child_idx), results_.at(parent_idx), cmp_result))) {
    } else if (cmp_result >= 0) {
      break;
    } else {
      const ObSparseRetrievalMatch tmp = results_.at(parent_idx);
      results_.at(parent_idx) = results_.at(child_idx);
      results_.at(child_idx) = tmp;
      child_idx = parent_idx;
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::sift_down(
    const int64_t heap_size,
    const int64_t start_idx)
{
  int ret = OB_SUCCESS;
  int64_t parent_idx = start_idx;
  while (OB_SUCC(ret)) {
    const int64_t left_idx = parent_idx * 2 + 1;
    if (left_idx >= heap_size) {
      break;
    }
    int64_t worst_child_idx = left_idx;
    const int64_t right_idx = left_idx + 1;
    if (right_idx < heap_size) {
      int cmp_result = 0;
      if (OB_FAIL(compare_matches(
          results_.at(right_idx), results_.at(left_idx), cmp_result))) {
      } else if (cmp_result < 0) {
        worst_child_idx = right_idx;
      }
    }
    if (OB_SUCC(ret)) {
      int cmp_result = 0;
      if (OB_FAIL(compare_matches(
          results_.at(worst_child_idx), results_.at(parent_idx), cmp_result))) {
      } else if (cmp_result >= 0) {
        break;
      } else {
        const ObSparseRetrievalMatch tmp = results_.at(parent_idx);
        results_.at(parent_idx) = results_.at(worst_child_idx);
        results_.at(worst_child_idx) = tmp;
        parent_idx = worst_child_idx;
      }
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::retain_candidate(
    const ObSparseRetrievalMatch &match)
{
  int ret = OB_SUCCESS;
  bool accepted = true;
  if (OB_NOT_NULL(filter_) && OB_FAIL(filter_->accept(match.id_.view(), accepted))) {
    LOG_WARN("BMW filter failed", K(ret));
  } else if (!accepted || 0 == candidate_limit_) {
  } else if (results_.count() < candidate_limit_) {
    if (OB_FAIL(results_.push_back(match))) {
    } else if (OB_FAIL(sift_up(results_.count() - 1))) {
    }
  } else {
    int cmp_result = 0;
    if (OB_FAIL(compare_matches(match, results_.at(0), cmp_result))) {
    } else if (cmp_result > 0) {
      results_.at(0) = match;
      if (OB_FAIL(sift_down(results_.count(), 0))) {
      }
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::sort_results()
{
  int ret = OB_SUCCESS;
  for (int64_t heap_size = results_.count(); OB_SUCC(ret) && heap_size > 1; --heap_size) {
    const ObSparseRetrievalMatch tmp = results_.at(0);
    results_.at(0) = results_.at(heap_size - 1);
    results_.at(heap_size - 1) = tmp;
    if (OB_FAIL(sift_down(heap_size - 1, 0))) {
    }
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::materialize()
{
  int ret = OB_SUCCESS;
  bool all_exhausted = false;
  bool finished = 0 == candidate_limit_;
  while (OB_SUCC(ret) && !all_exhausted && !finished) {
    int64_t min_source_idx = -1;
    if (OB_FAIL(load_missing_entries(all_exhausted))) {
    } else if (all_exhausted) {
    } else if (OB_FAIL(find_min_source(min_source_idx))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        all_exhausted = true;
      } else {
        LOG_WARN("failed to find next BMW id", K(ret));
      }
    } else {
      bool pruned = false;
      if (results_.count() == candidate_limit_) {
        if (OB_FAIL(prepare_blocks())) {
        } else if (OB_FAIL(try_prune_range(
            source_states_.at(min_source_idx).entry_.id_, pruned, finished))) {
        }
      }
      if (OB_SUCC(ret) && !pruned && !finished) {
        ObSparseRetrievalMatch match;
        if (OB_FAIL(collect_current_id(min_source_idx, match))) {
        } else if (OB_FAIL(retain_candidate(match))) {
        }
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(sort_results())) {
    LOG_WARN("failed to sort BMW results", K(ret));
  }
  if (OB_SUCC(ret)) {
    result_index_ = 0;
    state_ = CursorState::MATERIALIZED;
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::next(const ObSparseRetrievalMatch *&match)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  const ObSparseRetrievalMatch *matches = nullptr;
  match = nullptr;
  if (OB_FAIL(next_batch(1, matches, count))) {
  } else if (1 != count || OB_ISNULL(matches)) {
    ret = fail(OB_ERR_UNEXPECTED);
    LOG_WARN("unexpected BMW single-row result", K(ret), K(count));
  } else {
    match = matches;
  }
  return ret;
}

int ObSparseRetrievalBMWCursor::next_batch(
    const int64_t capacity,
    const ObSparseRetrievalMatch *&matches,
    int64_t &count)
{
  int ret = OB_SUCCESS;
  matches = nullptr;
  count = 0;
  if (CursorState::FAILED == state_) {
    ret = saved_error_;
  } else if (CursorState::RESET == state_) {
    ret = OB_NOT_INIT;
  } else if (capacity < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 == capacity) {
  } else if (CursorState::READY == state_ && OB_FAIL(materialize())) {
    ret = fail(ret);
  }
  if (OB_FAIL(ret)) {
  } else if (CursorState::EXHAUSTED == state_ || result_index_ >= results_.count()) {
    state_ = CursorState::EXHAUSTED;
    ret = OB_ITER_END;
  } else {
    count = MIN(MIN(capacity, max_batch_size_), results_.count() - result_index_);
    matches = &results_.at(result_index_);
    result_index_ += count;
  }
  return ret;
}

void ObSparseRetrievalBMWCursor::clear_algorithm_state()
{
  for (int64_t i = 0; i < source_states_.count(); ++i) {
    source_states_.at(i) = SourceState();
    global_max_scores_.at(i) = 0.0;
  }
  results_.reuse();
  result_index_ = 0;
  saved_error_ = OB_SUCCESS;
  blocks_prepared_ = false;
}

int ObSparseRetrievalBMWCursor::reuse(const bool switch_source)
{
  int ret = OB_SUCCESS;
  if (!owns_ports_) {
    ret = OB_NOT_INIT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sources_.count(); ++i) {
    if (OB_FAIL(sources_.at(i)->reuse(switch_source))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < block_sources_.count(); ++i) {
    if (OB_FAIL(block_sources_.at(i)->reuse(switch_source))) {
    }
  }
  clear_algorithm_state();
  if (OB_SUCC(ret)) {
    state_ = CursorState::READY;
  } else {
    fail(ret);
  }
  return ret;
}

void ObSparseRetrievalBMWCursor::reset()
{
  if (owns_ports_) {
    for (int64_t i = 0; i < sources_.count(); ++i) {
      sources_.at(i)->reset();
    }
    for (int64_t i = 0; i < block_sources_.count(); ++i) {
      block_sources_.at(i)->reset();
    }
  }
  clear_algorithm_state();
  state_ = CursorState::RESET;
}

void ObSparseRetrievalBMWCursor::destroy()
{
  common::ObIAllocator *allocator = allocator_;
  reset();
  if (owns_ports_) {
    for (int64_t i = 0; i < sources_.count(); ++i) {
      sources_.at(i)->destroy();
    }
    for (int64_t i = 0; i < block_sources_.count(); ++i) {
      block_sources_.at(i)->destroy();
    }
    if (OB_NOT_NULL(id_ops_)) {
      id_ops_->destroy();
    }
    if (OB_NOT_NULL(filter_)) {
      filter_->destroy();
    }
    owns_ports_ = false;
  }
  this->~ObSparseRetrievalBMWCursor();
  if (OB_NOT_NULL(allocator)) {
    allocator->free(this);
  }
}

} // namespace

int ObSparseRetrievalFactory::create_daat(
    const ObSparseRetrievalDaaTRequest &request,
    ObSparseRetrievalHandle &handle)
{
  int ret = OB_SUCCESS;
  void *buffer = nullptr;
  ObSparseRetrievalDaaTCursor *cursor = nullptr;
  if (handle.is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("retrieval handle already owns a cursor", K(ret));
  } else if (OB_ISNULL(request.allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("missing DAAT cursor allocator", K(ret));
  } else if (OB_ISNULL(buffer = request.allocator_->alloc(sizeof(ObSparseRetrievalDaaTCursor)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate DAAT cursor", K(ret));
  } else if (FALSE_IT(cursor = new (buffer) ObSparseRetrievalDaaTCursor(*request.allocator_))) {
  } else if (OB_FAIL(cursor->init(request))) {
    LOG_WARN("failed to initialize DAAT cursor", K(ret));
    cursor->~ObSparseRetrievalDaaTCursor();
    request.allocator_->free(buffer);
  } else {
    handle.adopt(cursor);
  }
  return ret;
}

int ObSparseRetrievalFactory::create_block_max_wand(
    const ObSparseRetrievalBlockMaxWandRequest &request,
    ObSparseRetrievalHandle &handle)
{
  int ret = OB_SUCCESS;
  void *buffer = nullptr;
  ObSparseRetrievalBMWCursor *cursor = nullptr;
  if (handle.is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("retrieval handle already owns a cursor", K(ret));
  } else if (OB_ISNULL(request.allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("missing BMW cursor allocator", K(ret));
  } else if (OB_ISNULL(buffer = request.allocator_->alloc(sizeof(ObSparseRetrievalBMWCursor)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate BMW cursor", K(ret));
  } else if (FALSE_IT(cursor = new (buffer) ObSparseRetrievalBMWCursor(*request.allocator_))) {
  } else if (OB_FAIL(cursor->init(request))) {
    LOG_WARN("failed to initialize BMW cursor", K(ret));
    cursor->~ObSparseRetrievalBMWCursor();
    request.allocator_->free(buffer);
  } else {
    handle.adopt(cursor);
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
