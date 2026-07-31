/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#define USING_LOG_PREFIX STORAGE

#include "data_plane/retrieval/ob_retrieval_program_spi.h"

#include <algorithm>
#include <new>

#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

template <typename T, typename... Args>
T *alloc_object(common::ObIAllocator &allocator, Args &&...args)
{
  void *buffer = allocator.alloc(sizeof(T));
  return nullptr == buffer
      ? nullptr
      : new (buffer) T(static_cast<Args &&>(args)...);
}

template <typename T>
void free_object(common::ObIAllocator &allocator, T *&object)
{
  if (nullptr != object) {
    object->~T();
    allocator.free(object);
    object = nullptr;
  }
}

ObRetrievalResultOrder planned_scan_order(const ObRetrievalPlanIntent intent)
{
  ObRetrievalResultOrder order = RETRIEVAL_UNSPECIFIED;
  switch (intent) {
    case RETRIEVAL_ORDERED_SCAN:
      order = RETRIEVAL_DOC_ID_ASC;
      break;
    case RETRIEVAL_BOUNDED_TOP_K:
      // Compatibility is intentional: the current production BMW heap pops
      // its minimum score first and does not define tie order.
      order = RETRIEVAL_SCORE_ASC_TIES_UNSPECIFIED;
      break;
    case RETRIEVAL_ACCUMULATE:
      order = RETRIEVAL_UNSPECIFIED;
      break;
    default:
      order = RETRIEVAL_UNSPECIFIED;
      break;
  }
  return order;
}

bool is_valid_order(const ObRetrievalResultOrder order)
{
  return order >= RETRIEVAL_DOC_ID_ASC
      && order <= RETRIEVAL_LOOKUP_INPUT_ORDER;
}

int normalize_non_data_result(const int result)
{
  // OB_ITER_END is meaningful only for next_batch().  Letting it escape from
  // setup or metadata calls would make a hard provider failure look like EOF.
  return OB_ITER_END == result ? OB_ERR_UNEXPECTED : result;
}

class ObRetrievalRunStorage
{
public:
  ObRetrievalRunStorage()
    : arena_("RetrievalRun"),
      request_(),
      ranges_(nullptr),
      lower_ids_(nullptr),
      upper_ids_(nullptr),
      lookup_keys_(nullptr),
      lookup_ids_(nullptr)
  {}
  ~ObRetrievalRunStorage() = default;

  int assign(const ObRetrievalRunRequest &source)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(!source.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      request_ = source;
      request_.ranges_ = nullptr;
      request_.lookup_keys_ = nullptr;
    }

    if (OB_SUCC(ret) && source.range_count_ > 0) {
      ranges_ = static_cast<ObRetrievalDocRangeView *>(
          arena_.alloc(sizeof(ObRetrievalDocRangeView) * source.range_count_));
      lower_ids_ = static_cast<ObRetrievalDocId *>(
          arena_.alloc(sizeof(ObRetrievalDocId) * source.range_count_));
      upper_ids_ = static_cast<ObRetrievalDocId *>(
          arena_.alloc(sizeof(ObRetrievalDocId) * source.range_count_));
      if (OB_ISNULL(ranges_) || OB_ISNULL(lower_ids_) || OB_ISNULL(upper_ids_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < source.range_count_; ++i) {
        new (&ranges_[i]) ObRetrievalDocRangeView();
        new (&lower_ids_[i]) ObRetrievalDocId();
        new (&upper_ids_[i]) ObRetrievalDocId();
        ranges_[i].has_lower_ = source.ranges_[i].has_lower_;
        ranges_[i].has_upper_ = source.ranges_[i].has_upper_;
        ranges_[i].include_lower_ = source.ranges_[i].include_lower_;
        ranges_[i].include_upper_ = source.ranges_[i].include_upper_;
        if (source.ranges_[i].has_lower_) {
          if (OB_FAIL(lower_ids_[i].assign(source.ranges_[i].lower_))) {
          } else {
            ranges_[i].lower_ = lower_ids_[i].view();
          }
        }
        if (OB_SUCC(ret) && source.ranges_[i].has_upper_) {
          if (OB_FAIL(upper_ids_[i].assign(source.ranges_[i].upper_))) {
          } else {
            ranges_[i].upper_ = upper_ids_[i].view();
          }
        }
      }
      if (OB_SUCC(ret)) {
        request_.ranges_ = ranges_;
      }
    }

    if (OB_SUCC(ret) && source.lookup_key_count_ > 0) {
      lookup_keys_ = static_cast<ObRetrievalLookupKeyView *>(
          arena_.alloc(sizeof(ObRetrievalLookupKeyView) * source.lookup_key_count_));
      lookup_ids_ = static_cast<ObRetrievalDocId *>(
          arena_.alloc(sizeof(ObRetrievalDocId) * source.lookup_key_count_));
      if (OB_ISNULL(lookup_keys_) || OB_ISNULL(lookup_ids_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < source.lookup_key_count_; ++i) {
        new (&lookup_keys_[i]) ObRetrievalLookupKeyView();
        new (&lookup_ids_[i]) ObRetrievalDocId();
        lookup_keys_[i] = source.lookup_keys_[i];
        if (OB_FAIL(lookup_ids_[i].assign(source.lookup_keys_[i].doc_id_))) {
        } else {
          lookup_keys_[i].doc_id_ = lookup_ids_[i].view();
        }
      }
      if (OB_SUCC(ret)) {
        request_.lookup_keys_ = lookup_keys_;
      }
    }
    return ret;
  }

  const ObRetrievalRunRequest &view() const { return request_; }

private:
  common::ObArenaAllocator arena_;
  ObRetrievalRunRequest request_;
  ObRetrievalDocRangeView *ranges_;
  ObRetrievalDocId *lower_ids_;
  ObRetrievalDocId *upper_ids_;
  ObRetrievalLookupKeyView *lookup_keys_;
  ObRetrievalDocId *lookup_ids_;
};

} // namespace

class ObRetrievalProgram::Impl
{
public:
  explicit Impl(common::ObIAllocator &owner_allocator)
    : owner_allocator_(&owner_allocator),
      query_arena_("RetrievalQuery"),
      query_(),
      term_views_(nullptr),
      term_datums_(nullptr),
      binding_(nullptr),
      execution_(nullptr),
      run_(nullptr),
      state_(RETRIEVAL_PROGRAM_EMPTY),
      first_error_(OB_SUCCESS),
      actual_order_(RETRIEVAL_UNSPECIFIED),
      run_generation_(0),
      emitted_count_(0),
      has_query_max_score_(false),
      query_max_score_(0.0)
  {}

  ~Impl()
  {
    close_execution();
    if (nullptr != binding_) {
      binding_->destroy();
      binding_ = nullptr;
    }
    free_object(*owner_allocator_, run_);
    query_arena_.reset();
  }

  void destroy_self()
  {
    common::ObIAllocator *allocator = owner_allocator_;
    this->~Impl();
    allocator->free(this);
  }

  int init(const ObRetrievalCompileRequest &source)
  {
    int ret = OB_SUCCESS;
    if (RETRIEVAL_PROGRAM_EMPTY != state_) {
      ret = OB_INIT_TWICE;
    } else if (OB_UNLIKELY(!source.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      query_ = source;
      query_.terms_ = nullptr;
    }

    if (OB_SUCC(ret) && source.term_count_ > 0) {
      term_views_ = static_cast<ObRetrievalTermView *>(
          query_arena_.alloc(sizeof(ObRetrievalTermView) * source.term_count_));
      term_datums_ = static_cast<common::ObDatum *>(
          query_arena_.alloc(sizeof(common::ObDatum) * source.term_count_));
      if (OB_ISNULL(term_views_) || OB_ISNULL(term_datums_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < source.term_count_; ++i) {
        new (&term_views_[i]) ObRetrievalTermView();
        new (&term_datums_[i]) common::ObDatum();
        term_views_[i] = source.terms_[i];
        if (OB_FAIL(term_datums_[i].deep_copy(
            *source.terms_[i].token_, query_arena_))) {
        } else {
          term_views_[i].token_ = &term_datums_[i];
        }
      }
      if (OB_SUCC(ret)) {
        query_.terms_ = term_views_;
      }
    }

    if (OB_SUCC(ret)) {
      ret = copy_intent(source.boolean_intent_, query_.boolean_intent_);
    }
    if (OB_SUCC(ret)) {
      ret = copy_intent(source.filter_intent_, query_.filter_intent_);
    }
    if (OB_SUCC(ret)) {
      state_ = RETRIEVAL_PROGRAM_COMPILED;
    } else {
      query_arena_.reset();
      query_ = ObRetrievalCompileRequest();
      term_views_ = nullptr;
      term_datums_ = nullptr;
    }
    return ret;
  }

  int start_same(const ObRetrievalRunRequest &source, ObRetrievalRunInfo &info)
  {
    int ret = OB_SUCCESS;
    info = ObRetrievalRunInfo();
    if (OB_ISNULL(binding_)) {
      ret = OB_NOT_INIT;
    } else {
      ret = stage_start(source, binding_, nullptr, info);
    }
    return ret;
  }

  int start_candidate(
      const ObRetrievalRunRequest &source,
      ObRetrievalCorpus &candidate,
      ObRetrievalRunInfo &info)
  {
    int ret = OB_SUCCESS;
    info = ObRetrievalRunInfo();
    if (OB_UNLIKELY(!candidate.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = stage_start(source, candidate.binding_, &candidate, info);
    }
    return ret;
  }

  int pull(const int64_t max_rows, ObRetrievalBatchView &batch)
  {
    int ret = OB_SUCCESS;
    batch = ObRetrievalBatchView();
    if (RETRIEVAL_PROGRAM_FAILED == state_) {
      ret = first_error_;
    } else if (RETRIEVAL_PROGRAM_END == state_) {
      ret = finish_empty_batch(batch);
    } else if (OB_ISNULL(execution_)) {
      ret = OB_NOT_INIT;
    } else if (OB_UNLIKELY(max_rows <= 0)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const int64_t effective_rows = std::min(max_rows, query_.max_batch_rows_);
      if (OB_FAIL(ensure_query_max_score())) {
        ret = fail(ret);
        batch = ObRetrievalBatchView();
      } else {
        ret = execution_->next_batch(effective_rows, batch);
        if (OB_ITER_END == ret) {
          if (OB_UNLIKELY(0 != batch.count_ || nullptr != batch.matches_)) {
            ret = fail(OB_ERR_UNEXPECTED);
            batch = ObRetrievalBatchView();
          } else {
            ret = finish_empty_batch(batch);
          }
        } else if (OB_FAIL(ret)) {
          ret = fail(ret);
          batch = ObRetrievalBatchView();
        } else if (OB_FAIL(validate_batch(effective_rows, batch))) {
          ret = fail(ret);
          batch = ObRetrievalBatchView();
        }
      }

      if (OB_SUCC(ret)) {
        emitted_count_ += batch.count_;
        if (batch.end_ && OB_FAIL(validate_end_count())) {
          ret = fail(ret);
          batch = ObRetrievalBatchView();
        } else {
          batch.actual_order_ = actual_order_;
          batch.run_generation_ = run_generation_;
          batch.has_query_max_score_ = has_query_max_score_;
          batch.query_max_score_ = query_max_score_;
          state_ = batch.end_ ? RETRIEVAL_PROGRAM_END : RETRIEVAL_PROGRAM_RUNNING;
          if (0 == batch.count_ && batch.end_) {
            ret = OB_ITER_END;
          }
        }
      }
    }
    return ret;
  }

  ObRetrievalProgramState state() const { return state_; }
  int first_error() const { return first_error_; }
  const ObRetrievalCompileRequest &query() const { return query_; }

private:
  int copy_intent(
      const ObRetrievalIntentView &source, ObRetrievalIntentView &destination)
  {
    int ret = OB_SUCCESS;
    destination = source;
    if (!source.is_empty()) {
      uint8_t *data = static_cast<uint8_t *>(query_arena_.alloc(source.size_));
      if (OB_ISNULL(data)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(data, source.data_, source.size_);
        destination.data_ = data;
      }
    }
    return ret;
  }

  int stage_start(
      const ObRetrievalRunRequest &source,
      detail::ObIRetrievalCorpusBinding *candidate_binding,
      ObRetrievalCorpus *candidate_owner,
      ObRetrievalRunInfo &info)
  {
    int ret = OB_SUCCESS;
    ObRetrievalRunStorage *staged_run = nullptr;
    detail::ObIRetrievalExecution *staged_execution = nullptr;
    info = ObRetrievalRunInfo();
    if (OB_UNLIKELY(!source.is_valid()) || OB_ISNULL(candidate_binding)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_ISNULL(staged_run = alloc_object<ObRetrievalRunStorage>(
        *owner_allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(staged_run->assign(source))) {
    } else if (OB_FAIL(candidate_binding->open(
        query_, staged_run->view(), staged_execution))) {
      ret = normalize_non_data_result(ret);
    } else if (OB_ISNULL(staged_execution)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObRetrievalResultOrder staged_order = staged_execution->result_order();
      const ObRetrievalResultOrder expected_order =
          RETRIEVAL_LOOKUP_RUN == source.kind_
              ? RETRIEVAL_LOOKUP_INPUT_ORDER
              : planned_scan_order(query_.plan_intent_);
      if (OB_UNLIKELY(!is_valid_order(staged_order)
          || staged_order != expected_order)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }

    if (OB_FAIL(ret)) {
      if (nullptr != staged_execution) {
        staged_execution->destroy();
        staged_execution = nullptr;
      }
      free_object(*owner_allocator_, staged_run);
    } else {
      close_execution();
      free_object(*owner_allocator_, run_);
      if (nullptr != candidate_owner) {
        if (nullptr != binding_) {
          binding_->destroy();
        }
        binding_ = candidate_owner->binding_;
        candidate_owner->binding_ = nullptr;
      }
      execution_ = staged_execution;
      run_ = staged_run;
      actual_order_ = execution_->result_order();
      ++run_generation_;
      emitted_count_ = 0;
      has_query_max_score_ = false;
      query_max_score_ = 0.0;
      first_error_ = OB_SUCCESS;
      state_ = RETRIEVAL_PROGRAM_READY;
      info.actual_order_ = actual_order_;
      info.run_generation_ = run_generation_;
    }
    return ret;
  }

  int validate_batch(
      const int64_t effective_rows, const ObRetrievalBatchView &batch) const
  {
    int ret = OB_SUCCESS;
    if (batch.count_ < 0 || batch.count_ > effective_rows
        || (batch.count_ > 0 && nullptr == batch.matches_)
        || (0 == batch.count_ && nullptr != batch.matches_)
        || (0 == batch.count_ && !batch.end_)) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < batch.count_; ++i) {
      const ObRetrievalMatch &match = batch.matches_[i];
      if (!match.doc_id_.is_valid() || !std::isfinite(match.score_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (RETRIEVAL_LOOKUP_RUN == run_->view().kind_) {
        const int64_t expected_index = emitted_count_ + i;
        if (expected_index >= run_->view().lookup_key_count_
            || match.input_ordinal_
                != run_->view().lookup_keys_[expected_index].input_ordinal_
            || !common::ObDatum::binary_equal(
                match.doc_id_.datum(),
                *run_->view().lookup_keys_[expected_index].doc_id_.datum_)
            || (!match.matched_ && 0.0 != match.score_)) {
          ret = OB_ERR_UNEXPECTED;
        }
      } else if (-1 != match.input_ordinal_ || !match.matched_) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    return ret;
  }

  int ensure_query_max_score()
  {
    int ret = OB_SUCCESS;
    if (query_.need_query_max_score_ && !has_query_max_score_) {
      double score = 0.0;
      if (OB_FAIL(execution_->query_max_score(score))) {
        ret = normalize_non_data_result(ret);
      } else if (OB_UNLIKELY(!std::isfinite(score))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        query_max_score_ = score;
        has_query_max_score_ = true;
      }
    }
    return ret;
  }

  int validate_end_count() const
  {
    int ret = OB_SUCCESS;
    if (RETRIEVAL_LOOKUP_RUN == run_->view().kind_
        && emitted_count_ != run_->view().lookup_key_count_) {
      ret = OB_ERR_UNEXPECTED;
    }
    return ret;
  }

  int finish_empty_batch(ObRetrievalBatchView &batch)
  {
    int ret = OB_SUCCESS;
    batch = ObRetrievalBatchView();
    batch.end_ = true;
    if (OB_FAIL(validate_end_count())) {
      ret = fail(ret);
      batch = ObRetrievalBatchView();
    } else {
      batch.actual_order_ = actual_order_;
      batch.run_generation_ = run_generation_;
      batch.has_query_max_score_ = has_query_max_score_;
      batch.query_max_score_ = query_max_score_;
      state_ = RETRIEVAL_PROGRAM_END;
      ret = OB_ITER_END;
    }
    return ret;
  }

  int fail(const int error)
  {
    if (RETRIEVAL_PROGRAM_FAILED != state_) {
      first_error_ = error;
      state_ = RETRIEVAL_PROGRAM_FAILED;
    }
    return first_error_;
  }

  void close_execution()
  {
    if (nullptr != execution_) {
      execution_->destroy();
      execution_ = nullptr;
    }
  }

private:
  common::ObIAllocator *owner_allocator_;
  common::ObArenaAllocator query_arena_;
  ObRetrievalCompileRequest query_;
  ObRetrievalTermView *term_views_;
  common::ObDatum *term_datums_;
  detail::ObIRetrievalCorpusBinding *binding_;
  detail::ObIRetrievalExecution *execution_;
  ObRetrievalRunStorage *run_;
  ObRetrievalProgramState state_;
  int first_error_;
  ObRetrievalResultOrder actual_order_;
  uint64_t run_generation_;
  int64_t emitted_count_;
  bool has_query_max_score_;
  double query_max_score_;
};

ObRetrievalCorpus::ObRetrievalCorpus() : binding_(nullptr) {}

ObRetrievalCorpus::~ObRetrievalCorpus()
{
  reset();
}

ObRetrievalCorpus::ObRetrievalCorpus(ObRetrievalCorpus &&other)
  : binding_(other.binding_)
{
  other.binding_ = nullptr;
}

ObRetrievalCorpus &ObRetrievalCorpus::operator=(ObRetrievalCorpus &&other)
{
  if (this != &other) {
    reset();
    binding_ = other.binding_;
    other.binding_ = nullptr;
  }
  return *this;
}

bool ObRetrievalCorpus::is_valid() const
{
  return nullptr != binding_;
}

void ObRetrievalCorpus::reset()
{
  if (nullptr != binding_) {
    binding_->destroy();
    binding_ = nullptr;
  }
}

void ObRetrievalCorpus::adopt(detail::ObIRetrievalCorpusBinding *binding)
{
  reset();
  binding_ = binding;
}

int ObRetrievalCorpusFactory::create(ObRetrievalCorpus &corpus)
{
  int ret = OB_SUCCESS;
  detail::ObIRetrievalCorpusBinding *binding = nullptr;
  if (OB_UNLIKELY(corpus.is_valid())) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(create_binding(binding))) {
    ret = normalize_non_data_result(ret);
    if (nullptr != binding) {
      binding->destroy();
      binding = nullptr;
    }
  } else if (OB_ISNULL(binding)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    corpus.adopt(binding);
  }
  return ret;
}

ObRetrievalProgram::ObRetrievalProgram() : impl_(nullptr) {}

ObRetrievalProgram::~ObRetrievalProgram()
{
  reset();
}

ObRetrievalProgram::ObRetrievalProgram(ObRetrievalProgram &&other)
  : impl_(other.impl_)
{
  other.impl_ = nullptr;
}

ObRetrievalProgram &ObRetrievalProgram::operator=(ObRetrievalProgram &&other)
{
  if (this != &other) {
    reset();
    impl_ = other.impl_;
    other.impl_ = nullptr;
  }
  return *this;
}

int ObRetrievalProgram::compile(
    common::ObIAllocator &allocator,
    const ObRetrievalCompileRequest &request,
    ObRetrievalProgram &program,
    ObRetrievalCompileInfo *info)
{
  int ret = OB_SUCCESS;
  Impl *impl = nullptr;
  if (nullptr != info) {
    *info = ObRetrievalCompileInfo();
  }
  if (OB_UNLIKELY(program.is_valid())) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(!request.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(impl = alloc_object<Impl>(allocator, allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(impl->init(request))) {
    free_object(allocator, impl);
  } else {
    program.impl_ = impl;
    if (nullptr != info) {
      info->scan_order_ = planned_scan_order(request.plan_intent_);
      info->max_batch_rows_ = request.max_batch_rows_;
    }
  }
  return ret;
}

int ObRetrievalProgram::start(
    const ObRetrievalRunRequest &run, ObRetrievalRunInfo &info)
{
  info = ObRetrievalRunInfo();
  return nullptr == impl_ ? OB_NOT_INIT : impl_->start_same(run, info);
}

int ObRetrievalProgram::start(
    const ObRetrievalRunRequest &run,
    ObRetrievalCorpus &candidate,
    ObRetrievalRunInfo &info)
{
  info = ObRetrievalRunInfo();
  return nullptr == impl_
      ? OB_NOT_INIT
      : impl_->start_candidate(run, candidate, info);
}

int ObRetrievalProgram::pull(
    const int64_t max_rows, ObRetrievalBatchView &batch)
{
  batch = ObRetrievalBatchView();
  return nullptr == impl_ ? OB_NOT_INIT : impl_->pull(max_rows, batch);
}

bool ObRetrievalProgram::is_valid() const
{
  return nullptr != impl_;
}

ObRetrievalProgramState ObRetrievalProgram::state() const
{
  return nullptr == impl_ ? RETRIEVAL_PROGRAM_EMPTY : impl_->state();
}

int ObRetrievalProgram::first_error() const
{
  return nullptr == impl_ ? OB_NOT_INIT : impl_->first_error();
}

void ObRetrievalProgram::reset()
{
  if (nullptr != impl_) {
    impl_->destroy_self();
    impl_ = nullptr;
  }
}

} // namespace data_plane
} // namespace oceanbase
