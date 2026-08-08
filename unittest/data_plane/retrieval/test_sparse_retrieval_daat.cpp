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

#define USING_LOG_PREFIX STORAGE

#include <gtest/gtest.h>
#include <limits>

#include "data_plane/retrieval/ob_sparse_retrieval.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

class MockSource final : public ObISparseRetrievalSource
{
public:
  static const int64_t MAX_ENTRY_COUNT = 512;

  MockSource(common::ObIAllocator &allocator, int *destroy_count, int *read_count)
    : allocator_(&allocator),
      destroy_count_(destroy_count),
      read_count_(read_count),
      ids_(),
      scores_{},
      count_(0),
      index_(-1),
      exhausted_(false),
      saved_error_(OB_SUCCESS)
  {}
  virtual ~MockSource() = default;

  int init(const int64_t *ids, const double *scores, const int64_t count)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(ids) || OB_ISNULL(scores) || count < 0 || count > MAX_ENTRY_COUNT) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      char datum_buffer[sizeof(int64_t)] = {};
      common::ObDatum datum(datum_buffer, 0, false);
      datum.set_int(ids[i]);
      if (OB_FAIL(ids_[i].assign(ObSparseRetrievalIdView(datum)))) {
      } else {
        scores_[i] = scores[i];
      }
    }
    if (OB_SUCC(ret)) {
      count_ = count;
    }
    return ret;
  }

  virtual int next(ObSparseRetrievalEntryView &entry) override
  {
    int ret = OB_SUCCESS;
    entry = ObSparseRetrievalEntryView();
    if (OB_NOT_NULL(read_count_)) {
      ++*read_count_;
    }
    if (OB_SUCCESS != saved_error_) {
      ret = saved_error_;
    } else if (exhausted_ || ++index_ >= count_) {
      exhausted_ = true;
      ret = OB_ITER_END;
    } else {
      entry.id_ = ids_[index_].view();
      entry.score_ = scores_[index_];
    }
    return ret;
  }

  virtual int advance_to(
      const ObSparseRetrievalIdView &target,
      ObSparseRetrievalEntryView &entry) override
  {
    int ret = OB_SUCCESS;
    bool found = false;
    if (!target.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    }
    while (OB_SUCC(ret) && !found) {
      if (OB_FAIL(next(entry))) {
      } else {
        found = entry.id_.datum_->get_int() >= target.datum_->get_int();
      }
    }
    if (OB_FAIL(ret) && OB_ITER_END != ret) {
      saved_error_ = ret;
    }
    return ret;
  }

  virtual int reuse(const bool switch_source) override
  {
    UNUSED(switch_source);
    index_ = -1;
    exhausted_ = false;
    saved_error_ = OB_SUCCESS;
    return OB_SUCCESS;
  }

  virtual void reset() override
  {
    index_ = -1;
    exhausted_ = true;
    saved_error_ = OB_SUCCESS;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    if (OB_NOT_NULL(destroy_count_)) {
      ++*destroy_count_;
    }
    this->~MockSource();
    allocator->free(this);
  }

private:
  common::ObIAllocator *allocator_;
  int *destroy_count_;
  int *read_count_;
  ObSparseRetrievalId ids_[MAX_ENTRY_COUNT];
  double scores_[MAX_ENTRY_COUNT];
  int64_t count_;
  int64_t index_;
  bool exhausted_;
  int saved_error_;
  DISALLOW_COPY_AND_ASSIGN(MockSource);
};

class IntIdOps final : public ObISparseRetrievalIdOps
{
public:
  IntIdOps(common::ObIAllocator &allocator, int *destroy_count, int *compare_count)
    : allocator_(&allocator),
      destroy_count_(destroy_count),
      compare_count_(compare_count),
      injected_error_(OB_SUCCESS)
  {}
  virtual ~IntIdOps() = default;

  void inject_error(const int error) { injected_error_ = error; }

  virtual int compare(
      const ObSparseRetrievalIdView &left,
      const ObSparseRetrievalIdView &right,
      int &cmp_result) const override
  {
    int ret = OB_SUCCESS;
    if (OB_NOT_NULL(compare_count_)) {
      ++*compare_count_;
    }
    if (OB_SUCCESS != injected_error_) {
      ret = injected_error_;
    } else if (!left.is_valid() || !right.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const int64_t left_id = left.datum_->get_int();
      const int64_t right_id = right.datum_->get_int();
      cmp_result = left_id < right_id ? -1 : (left_id > right_id ? 1 : 0);
    }
    return ret;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    if (OB_NOT_NULL(destroy_count_)) {
      ++*destroy_count_;
    }
    this->~IntIdOps();
    allocator->free(this);
  }

private:
  common::ObIAllocator *allocator_;
  int *destroy_count_;
  int *compare_count_;
  int injected_error_;
  DISALLOW_COPY_AND_ASSIGN(IntIdOps);
};

class MockBlockSource final : public ObISparseRetrievalBlockSource
{
public:
  static const int64_t MAX_BLOCK_COUNT = 32;

  MockBlockSource(
      common::ObIAllocator &allocator,
      int *destroy_count,
      int *advance_count)
    : allocator_(&allocator),
      destroy_count_(destroy_count),
      advance_count_(advance_count),
      min_ids_(),
      max_ids_(),
      bounds_{},
      count_(0),
      index_(-1),
      max_score_override_(0.0),
      has_max_score_override_(false),
      injected_advance_error_(OB_SUCCESS),
      saved_error_(OB_SUCCESS),
      exhausted_(false)
  {}
  virtual ~MockBlockSource() = default;

  int init(
      const int64_t *min_ids,
      const int64_t *max_ids,
      const double *bounds,
      const int64_t count)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(min_ids) || OB_ISNULL(max_ids) || OB_ISNULL(bounds)
        || count <= 0 || count > MAX_BLOCK_COUNT) {
      ret = OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      char min_buffer[sizeof(int64_t)] = {};
      char max_buffer[sizeof(int64_t)] = {};
      common::ObDatum min_datum(min_buffer, 0, false);
      common::ObDatum max_datum(max_buffer, 0, false);
      min_datum.set_int(min_ids[i]);
      max_datum.set_int(max_ids[i]);
      if (min_ids[i] > max_ids[i]) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(min_ids_[i].assign(ObSparseRetrievalIdView(min_datum)))) {
      } else if (OB_FAIL(max_ids_[i].assign(ObSparseRetrievalIdView(max_datum)))) {
      } else {
        bounds_[i] = bounds[i];
      }
    }
    if (OB_SUCC(ret)) {
      count_ = count;
    }
    return ret;
  }

  void inject_advance_error(const int error) { injected_advance_error_ = error; }
  void override_max_score(const double score)
  {
    max_score_override_ = score;
    has_max_score_override_ = true;
  }

  virtual int max_score(double &score) override
  {
    int ret = saved_error_;
    score = 0.0;
    if (OB_SUCCESS == ret && has_max_score_override_) {
      score = max_score_override_;
    } else if (OB_SUCCESS == ret) {
      for (int64_t i = 0; i < count_; ++i) {
        score = OB_MAX(score, bounds_[i]);
      }
    }
    return ret;
  }

  virtual int advance_to(
      const ObSparseRetrievalIdView &target,
      const bool inclusive,
      ObSparseRetrievalBlockView &block) override
  {
    int ret = OB_SUCCESS;
    block = ObSparseRetrievalBlockView();
    if (OB_NOT_NULL(advance_count_)) {
      ++*advance_count_;
    }
    if (OB_SUCCESS != saved_error_) {
      ret = saved_error_;
    } else if (OB_SUCCESS != injected_advance_error_) {
      ret = injected_advance_error_;
    } else if (!target.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (exhausted_) {
      ret = OB_ITER_END;
    } else {
      const int64_t target_id = target.datum_->get_int();
      if (index_ < 0) {
        index_ = 0;
      }
      while (index_ < count_
          && (max_ids_[index_].datum().get_int() < target_id
              || (!inclusive && max_ids_[index_].datum().get_int() == target_id))) {
        ++index_;
      }
      if (index_ >= count_) {
        exhausted_ = true;
        ret = OB_ITER_END;
      } else {
        block.min_id_ = min_ids_[index_].view();
        block.max_id_ = max_ids_[index_].view();
        block.score_upper_bound_ = bounds_[index_];
      }
    }
    if (OB_SUCCESS != ret && OB_ITER_END != ret) {
      saved_error_ = ret;
    }
    return ret;
  }

  virtual int reuse(const bool switch_source) override
  {
    UNUSED(switch_source);
    index_ = -1;
    saved_error_ = OB_SUCCESS;
    exhausted_ = false;
    return OB_SUCCESS;
  }

  virtual void reset() override
  {
    index_ = -1;
    saved_error_ = OB_SUCCESS;
    exhausted_ = true;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    if (OB_NOT_NULL(destroy_count_)) {
      ++*destroy_count_;
    }
    this->~MockBlockSource();
    allocator->free(this);
  }

private:
  common::ObIAllocator *allocator_;
  int *destroy_count_;
  int *advance_count_;
  ObSparseRetrievalId min_ids_[MAX_BLOCK_COUNT];
  ObSparseRetrievalId max_ids_[MAX_BLOCK_COUNT];
  double bounds_[MAX_BLOCK_COUNT];
  int64_t count_;
  int64_t index_;
  double max_score_override_;
  bool has_max_score_override_;
  int injected_advance_error_;
  int saved_error_;
  bool exhausted_;
  DISALLOW_COPY_AND_ASSIGN(MockBlockSource);
};

MockSource *new_source(
    common::ObIAllocator &allocator,
    const int64_t *ids,
    const double *scores,
    const int64_t count,
    int &destroy_count,
    int &read_count)
{
  void *buffer = allocator.alloc(sizeof(MockSource));
  MockSource *source = OB_ISNULL(buffer)
      ? nullptr : new (buffer) MockSource(allocator, &destroy_count, &read_count);
  if (OB_NOT_NULL(source) && OB_SUCCESS != source->init(ids, scores, count)) {
    source->destroy();
    source = nullptr;
  }
  return source;
}

IntIdOps *new_id_ops(
    common::ObIAllocator &allocator,
    int &destroy_count,
    int &compare_count)
{
  void *buffer = allocator.alloc(sizeof(IntIdOps));
  return OB_ISNULL(buffer)
      ? nullptr : new (buffer) IntIdOps(allocator, &destroy_count, &compare_count);
}

MockBlockSource *new_block_source(
    common::ObIAllocator &allocator,
    const int64_t *min_ids,
    const int64_t *max_ids,
    const double *bounds,
    const int64_t count,
    int &destroy_count,
    int &advance_count)
{
  void *buffer = allocator.alloc(sizeof(MockBlockSource));
  MockBlockSource *source = OB_ISNULL(buffer)
      ? nullptr : new (buffer) MockBlockSource(allocator, &destroy_count, &advance_count);
  if (OB_NOT_NULL(source)
      && OB_SUCCESS != source->init(min_ids, max_ids, bounds, count)) {
    source->destroy();
    source = nullptr;
  }
  return source;
}

TEST(SparseRetrievalDaaT, MergesWeightsAndBoundsTopK)
{
  common::ObArenaAllocator allocator;
  const int64_t ids_1[] = {1, 3};
  const double scores_1[] = {1.0, 3.0};
  const int64_t ids_2[] = {1, 2};
  const double scores_2[] = {2.0, 4.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source_1 = new_source(
      allocator, ids_1, scores_1, ARRAYSIZEOF(ids_1), source_destroy_count, source_read_count);
  MockSource *source_2 = new_source(
      allocator, ids_2, scores_2, ARRAYSIZEOF(ids_2), source_destroy_count, source_read_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source_1);
  ASSERT_NE(nullptr, source_2);
  ASSERT_NE(nullptr, id_ops);

  common::ObSEArray<ObISparseRetrievalSource *, 2> sources;
  common::ObSEArray<double, 2> weights;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source_1));
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source_2));
  ASSERT_EQ(OB_SUCCESS, weights.push_back(1.0));
  ASSERT_EQ(OB_SUCCESS, weights.push_back(2.0));
  ObSparseRetrievalDaaTRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.id_ops_ = id_ops;
  request.dimension_weights_ = &weights;
  request.candidate_limit_ = 2;
  request.max_batch_size_ = 8;

  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_daat(request, handle));
  // Request containers are create-call inputs, not cursor-lifetime dependencies.
  weights.reset();
  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS, handle.next_batch(8, matches, count));
  ASSERT_EQ(2, count);
  ASSERT_NE(nullptr, matches);
  EXPECT_EQ(2, matches[0].id_.datum().get_int());
  EXPECT_DOUBLE_EQ(8.0, matches[0].score_);
  EXPECT_EQ(1, matches[1].id_.datum().get_int());
  EXPECT_DOUBLE_EQ(5.0, matches[1].score_);
  EXPECT_EQ(OB_ITER_END, handle.next_batch(8, matches, count));

  handle.reset();
  EXPECT_EQ(2, source_destroy_count);
  EXPECT_EQ(1, id_ops_destroy_count);
}

TEST(SparseRetrievalDaaT, CompareFailureIsSticky)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1};
  const double scores[] = {1.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source_1 = new_source(
      allocator, ids, scores, ARRAYSIZEOF(ids), source_destroy_count, source_read_count);
  MockSource *source_2 = new_source(
      allocator, ids, scores, ARRAYSIZEOF(ids), source_destroy_count, source_read_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source_1);
  ASSERT_NE(nullptr, source_2);
  ASSERT_NE(nullptr, id_ops);
  id_ops->inject_error(OB_ERR_UNEXPECTED);

  common::ObSEArray<ObISparseRetrievalSource *, 2> sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source_1));
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source_2));
  ObSparseRetrievalDaaTRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = 2;
  request.max_batch_size_ = 2;
  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_daat(request, handle));

  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(2, matches, count));
  const int reads_after_failure = source_read_count;
  const int compares_after_failure = compare_count;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(2, matches, count));
  EXPECT_EQ(reads_after_failure, source_read_count);
  EXPECT_EQ(compares_after_failure, compare_count);
  handle.reset();
}

TEST(SparseRetrievalDaaT, BoundedHeapHasLogarithmicTopKComparisons)
{
  common::ObArenaAllocator allocator;
  static const int64_t ENTRY_COUNT = 256;
  static const int64_t TOP_K = 32;
  int64_t ids[ENTRY_COUNT] = {};
  double scores[ENTRY_COUNT] = {};
  for (int64_t i = 0; i < ENTRY_COUNT; ++i) {
    ids[i] = i + 1;
    scores[i] = 1.0;
  }
  int source_destroy_count = 0;
  int source_read_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, ENTRY_COUNT, source_destroy_count, source_read_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, id_ops);

  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ObSparseRetrievalDaaTRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = TOP_K;
  request.max_batch_size_ = TOP_K;
  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_daat(request, handle));

  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS, handle.next_batch(TOP_K, matches, count));
  ASSERT_EQ(TOP_K, count);
  for (int64_t i = 0; i < count; ++i) {
    EXPECT_EQ(i + 1, matches[i].id_.datum().get_int());
  }
  // Equal scores force every heap comparison through id_ops.  A linear
  // worst-element scan takes more than 7,000 comparisons for this N/K;
  // the bounded heap, including merge and final heap-sort, stays well below.
  EXPECT_LT(compare_count, 2000);

  handle.reset();
  EXPECT_EQ(1, source_destroy_count);
  EXPECT_EQ(1, id_ops_destroy_count);
}

TEST(SparseRetrievalDaaT, FactoryCommitsPortOwnershipAtomically)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1};
  const double scores[] = {1.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, ARRAYSIZEOF(ids), source_destroy_count, source_read_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, id_ops);

  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  common::ObSEArray<double, 1> mismatched_weights;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ObSparseRetrievalDaaTRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.id_ops_ = id_ops;
  request.dimension_weights_ = &mismatched_weights;
  request.candidate_limit_ = 1;
  request.max_batch_size_ = 1;
  ObSparseRetrievalHandle handle;

  EXPECT_EQ(OB_INVALID_ARGUMENT, ObSparseRetrievalFactory::create_daat(request, handle));
  EXPECT_EQ(0, source_destroy_count);
  EXPECT_EQ(0, id_ops_destroy_count);
  source->destroy();
  id_ops->destroy();
  EXPECT_EQ(1, source_destroy_count);
  EXPECT_EQ(1, id_ops_destroy_count);

  source_destroy_count = 0;
  id_ops_destroy_count = 0;
  source = new_source(
      allocator, ids, scores, ARRAYSIZEOF(ids), source_destroy_count, source_read_count);
  id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, id_ops);
  sources.reset();
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  request.id_ops_ = id_ops;
  request.dimension_weights_ = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_daat(request, handle));
  handle.reset();
  EXPECT_EQ(1, source_destroy_count);
  EXPECT_EQ(1, id_ops_destroy_count);
}

TEST(SparseRetrievalBMW, PrunesRangesAndKeepsStableTopK)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1, 2, 3, 4, 5, 6, 7, 8};
  const double scores[] = {5.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 10.0};
  const int64_t block_mins[] = {1, 2, 8};
  const int64_t block_maxs[] = {1, 7, 8};
  const double block_bounds[] = {5.0, 1.0, 10.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int block_destroy_count = 0;
  int block_advance_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, ARRAYSIZEOF(ids), source_destroy_count, source_read_count);
  MockBlockSource *block_source = new_block_source(
      allocator,
      block_mins,
      block_maxs,
      block_bounds,
      ARRAYSIZEOF(block_mins),
      block_destroy_count,
      block_advance_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, block_source);
  ASSERT_NE(nullptr, id_ops);

  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  common::ObSEArray<ObISparseRetrievalBlockSource *, 1> block_sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ASSERT_EQ(OB_SUCCESS, block_sources.push_back(block_source));
  ObSparseRetrievalBlockMaxWandRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.block_sources_ = &block_sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = 1;
  request.max_batch_size_ = 4;
  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_block_max_wand(request, handle));

  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  ASSERT_EQ(OB_SUCCESS, handle.next_batch(4, matches, count));
  ASSERT_EQ(1, count);
  EXPECT_EQ(8, matches[0].id_.datum().get_int());
  EXPECT_DOUBLE_EQ(10.0, matches[0].score_);
  EXPECT_GT(block_advance_count, 0);
  EXPECT_EQ(OB_ITER_END, handle.next_batch(4, matches, count));

  ASSERT_EQ(OB_SUCCESS, handle.reuse());
  ASSERT_EQ(OB_SUCCESS, handle.next_batch(4, matches, count));
  ASSERT_EQ(1, count);
  EXPECT_EQ(8, matches[0].id_.datum().get_int());
  handle.reset();
  EXPECT_EQ(1, source_destroy_count);
  EXPECT_EQ(1, block_destroy_count);
  EXPECT_EQ(1, id_ops_destroy_count);
}

TEST(SparseRetrievalBMW, BlockFailureIsSticky)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1, 2};
  const double scores[] = {1.0, 2.0};
  const int64_t block_mins[] = {1};
  const int64_t block_maxs[] = {2};
  const double block_bounds[] = {2.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int block_destroy_count = 0;
  int block_advance_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, ARRAYSIZEOF(ids), source_destroy_count, source_read_count);
  MockBlockSource *block_source = new_block_source(
      allocator,
      block_mins,
      block_maxs,
      block_bounds,
      ARRAYSIZEOF(block_mins),
      block_destroy_count,
      block_advance_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, block_source);
  ASSERT_NE(nullptr, id_ops);
  block_source->inject_advance_error(OB_ERR_UNEXPECTED);

  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  common::ObSEArray<ObISparseRetrievalBlockSource *, 1> block_sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ASSERT_EQ(OB_SUCCESS, block_sources.push_back(block_source));
  ObSparseRetrievalBlockMaxWandRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.block_sources_ = &block_sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = 1;
  request.max_batch_size_ = 1;
  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_block_max_wand(request, handle));

  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
  const int reads_after_failure = source_read_count;
  const int advances_after_failure = block_advance_count;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
  EXPECT_EQ(reads_after_failure, source_read_count);
  EXPECT_EQ(advances_after_failure, block_advance_count);
  handle.reset();
}

TEST(SparseRetrievalBMW, RejectsBlockEndBeforeExactAndSticks)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1, 2};
  const double scores[] = {1.0, 2.0};
  const int64_t block_mins[] = {1};
  const int64_t block_maxs[] = {1};
  const double block_bounds[] = {2.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int block_destroy_count = 0;
  int block_advance_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, 2, source_destroy_count, source_read_count);
  MockBlockSource *block_source = new_block_source(
      allocator, block_mins, block_maxs, block_bounds, 1,
      block_destroy_count, block_advance_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, block_source);
  ASSERT_NE(nullptr, id_ops);
  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  common::ObSEArray<ObISparseRetrievalBlockSource *, 1> block_sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ASSERT_EQ(OB_SUCCESS, block_sources.push_back(block_source));
  ObSparseRetrievalBlockMaxWandRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.block_sources_ = &block_sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = 1;
  request.max_batch_size_ = 1;
  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_block_max_wand(request, handle));

  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
  const int reads_after_failure = source_read_count;
  const int advances_after_failure = block_advance_count;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
  EXPECT_EQ(reads_after_failure, source_read_count);
  EXPECT_EQ(advances_after_failure, block_advance_count);
  handle.reset();
}

TEST(SparseRetrievalBMW, RejectsUncoveredExactPostingAndSticks)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1, 2, 3};
  const double scores[] = {1.0, 9.0, 10.0};
  const int64_t block_mins[] = {1, 3};
  const int64_t block_maxs[] = {1, 3};
  const double block_bounds[] = {1.0, 10.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int block_destroy_count = 0;
  int block_advance_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, 3, source_destroy_count, source_read_count);
  MockBlockSource *block_source = new_block_source(
      allocator, block_mins, block_maxs, block_bounds, 2,
      block_destroy_count, block_advance_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, block_source);
  ASSERT_NE(nullptr, id_ops);
  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  common::ObSEArray<ObISparseRetrievalBlockSource *, 1> block_sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ASSERT_EQ(OB_SUCCESS, block_sources.push_back(block_source));
  ObSparseRetrievalBlockMaxWandRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.block_sources_ = &block_sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = 1;
  request.max_batch_size_ = 1;
  ObSparseRetrievalHandle handle;
  ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_block_max_wand(request, handle));

  const ObSparseRetrievalMatch *matches = nullptr;
  int64_t count = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
  const int compares_after_failure = compare_count;
  EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
  EXPECT_EQ(compares_after_failure, compare_count);
  handle.reset();
}

TEST(SparseRetrievalBMW, RejectsNonFiniteGlobalAndBlockBounds)
{
  {
    common::ObArenaAllocator allocator;
    const int64_t ids[] = {1, 2};
    const double scores[] = {1.0, 2.0};
    const int64_t block_mins[] = {1};
    const int64_t block_maxs[] = {2};
    const double block_bounds[] = {2.0};
    int source_destroy_count = 0;
    int source_read_count = 0;
    int block_destroy_count = 0;
    int block_advance_count = 0;
    int id_ops_destroy_count = 0;
    int compare_count = 0;
    MockSource *source = new_source(
        allocator, ids, scores, 2, source_destroy_count, source_read_count);
    MockBlockSource *block_source = new_block_source(
        allocator, block_mins, block_maxs, block_bounds, 1,
        block_destroy_count, block_advance_count);
    IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
    ASSERT_NE(nullptr, source);
    ASSERT_NE(nullptr, block_source);
    ASSERT_NE(nullptr, id_ops);
    block_source->override_max_score(std::numeric_limits<double>::quiet_NaN());
    common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
    common::ObSEArray<ObISparseRetrievalBlockSource *, 1> block_sources;
    ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
    ASSERT_EQ(OB_SUCCESS, block_sources.push_back(block_source));
    ObSparseRetrievalBlockMaxWandRequest request;
    request.allocator_ = &allocator;
    request.sources_ = &sources;
    request.block_sources_ = &block_sources;
    request.id_ops_ = id_ops;
    request.candidate_limit_ = 1;
    request.max_batch_size_ = 1;
    ObSparseRetrievalHandle handle;
    ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_block_max_wand(request, handle));
    const ObSparseRetrievalMatch *matches = nullptr;
    int64_t count = 0;
    EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
    EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
    handle.reset();
  }

  {
    common::ObArenaAllocator allocator;
    const int64_t ids[] = {1, 2};
    const double scores[] = {1.0, 2.0};
    const int64_t block_mins[] = {1};
    const int64_t block_maxs[] = {2};
    const double block_bounds[] = {std::numeric_limits<double>::quiet_NaN()};
    int source_destroy_count = 0;
    int source_read_count = 0;
    int block_destroy_count = 0;
    int block_advance_count = 0;
    int id_ops_destroy_count = 0;
    int compare_count = 0;
    MockSource *source = new_source(
        allocator, ids, scores, 2, source_destroy_count, source_read_count);
    MockBlockSource *block_source = new_block_source(
        allocator, block_mins, block_maxs, block_bounds, 1,
        block_destroy_count, block_advance_count);
    IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
    ASSERT_NE(nullptr, source);
    ASSERT_NE(nullptr, block_source);
    ASSERT_NE(nullptr, id_ops);
    block_source->override_max_score(2.0);
    common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
    common::ObSEArray<ObISparseRetrievalBlockSource *, 1> block_sources;
    ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
    ASSERT_EQ(OB_SUCCESS, block_sources.push_back(block_source));
    ObSparseRetrievalBlockMaxWandRequest request;
    request.allocator_ = &allocator;
    request.sources_ = &sources;
    request.block_sources_ = &block_sources;
    request.id_ops_ = id_ops;
    request.candidate_limit_ = 1;
    request.max_batch_size_ = 1;
    ObSparseRetrievalHandle handle;
    ASSERT_EQ(OB_SUCCESS, ObSparseRetrievalFactory::create_block_max_wand(request, handle));
    const ObSparseRetrievalMatch *matches = nullptr;
    int64_t count = 0;
    EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
    EXPECT_EQ(OB_ERR_UNEXPECTED, handle.next_batch(1, matches, count));
    handle.reset();
  }
}

TEST(SparseRetrievalBMW, FactoryCommitsAllPortsAtomically)
{
  common::ObArenaAllocator allocator;
  const int64_t ids[] = {1};
  const double scores[] = {1.0};
  const int64_t block_mins[] = {1};
  const int64_t block_maxs[] = {1};
  const double block_bounds[] = {1.0};
  int source_destroy_count = 0;
  int source_read_count = 0;
  int block_destroy_count = 0;
  int block_advance_count = 0;
  int id_ops_destroy_count = 0;
  int compare_count = 0;
  MockSource *source = new_source(
      allocator, ids, scores, 1, source_destroy_count, source_read_count);
  MockBlockSource *block_source = new_block_source(
      allocator, block_mins, block_maxs, block_bounds, 1,
      block_destroy_count, block_advance_count);
  IntIdOps *id_ops = new_id_ops(allocator, id_ops_destroy_count, compare_count);
  ASSERT_NE(nullptr, source);
  ASSERT_NE(nullptr, block_source);
  ASSERT_NE(nullptr, id_ops);

  common::ObSEArray<ObISparseRetrievalSource *, 1> sources;
  common::ObSEArray<ObISparseRetrievalBlockSource *, 1> empty_block_sources;
  ASSERT_EQ(OB_SUCCESS, sources.push_back(source));
  ObSparseRetrievalBlockMaxWandRequest request;
  request.allocator_ = &allocator;
  request.sources_ = &sources;
  request.block_sources_ = &empty_block_sources;
  request.id_ops_ = id_ops;
  request.candidate_limit_ = 1;
  request.max_batch_size_ = 1;
  ObSparseRetrievalHandle handle;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      ObSparseRetrievalFactory::create_block_max_wand(request, handle));
  EXPECT_EQ(0, source_destroy_count);
  EXPECT_EQ(0, block_destroy_count);
  EXPECT_EQ(0, id_ops_destroy_count);
  source->destroy();
  block_source->destroy();
  id_ops->destroy();
  EXPECT_EQ(1, source_destroy_count);
  EXPECT_EQ(1, block_destroy_count);
  EXPECT_EQ(1, id_ops_destroy_count);
}

} // namespace
} // namespace data_plane
} // namespace oceanbase
