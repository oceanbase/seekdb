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

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#include "data_plane/retrieval/ob_retrieval_program_spi.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

static_assert(std::is_move_constructible<ObRetrievalCorpus>::value,
    "corpus must be movable");
static_assert(!std::is_copy_constructible<ObRetrievalCorpus>::value,
    "corpus ownership must not be copied");
static_assert(std::is_move_constructible<ObRetrievalProgram>::value,
    "program must be movable");
static_assert(!std::is_copy_constructible<ObRetrievalProgram>::value,
    "program ownership must not be copied");

common::ObDatum make_int_datum(const int64_t &value)
{
  return common::ObDatum(
      reinterpret_cast<const char *>(&value), sizeof(value), false);
}

struct FakeProbe
{
  FakeProbe()
    : open_count_(0), execution_destroy_count_(0), binding_destroy_count_(0),
      max_score_count_(0), next_count_(0), last_capacity_(0), close_clock_(0),
      execution_close_order_(0), binding_close_order_(0),
      fail_open_(false), fail_next_(false), invalid_batch_(false),
      dirty_iter_end_(false), empty_scan_(false), wrong_lookup_doc_(false),
      max_score_result_(OB_SUCCESS), score_(9.0),
      scan_order_(RETRIEVAL_DOC_ID_ASC),
      expected_term_(11), expected_intent_(0x42)
  {}

  int open_count_;
  int execution_destroy_count_;
  int binding_destroy_count_;
  int max_score_count_;
  int next_count_;
  int64_t last_capacity_;
  int close_clock_;
  int execution_close_order_;
  int binding_close_order_;
  bool fail_open_;
  bool fail_next_;
  bool invalid_batch_;
  bool dirty_iter_end_;
  bool empty_scan_;
  bool wrong_lookup_doc_;
  int max_score_result_;
  double score_;
  ObRetrievalResultOrder scan_order_;
  int64_t expected_term_;
  uint8_t expected_intent_;
};

class FakeExecution final : public detail::ObIRetrievalExecution
{
public:
  FakeExecution(
      FakeProbe &probe,
      const ObRetrievalCompileRequest &query,
      const ObRetrievalRunRequest &run,
      const ObRetrievalResultOrder order)
    : probe_(probe), query_(query), run_(run), order_(order),
      next_index_(0), prepared_(false), matches_{}
  {}

  int next_batch(
      const int64_t max_rows, ObRetrievalBatchView &batch) override
  {
    int ret = OB_SUCCESS;
    ++probe_.next_count_;
    probe_.last_capacity_ = max_rows;
    batch = ObRetrievalBatchView();
    if (probe_.fail_next_) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(prepare())) {
    } else if (probe_.invalid_batch_) {
      batch.matches_ = matches_;
      batch.count_ = max_rows + 1;
    } else if (next_index_ >= result_count()) {
      if (probe_.dirty_iter_end_) {
        batch.matches_ = matches_;
        batch.count_ = 1;
      }
      ret = OB_ITER_END;
    } else {
      const int64_t count = std::min(max_rows, result_count() - next_index_);
      batch.matches_ = &matches_[next_index_];
      batch.count_ = count;
      next_index_ += count;
      batch.end_ = next_index_ == result_count();
    }
    return ret;
  }

  int query_max_score(double &score) override
  {
    ++probe_.max_score_count_;
    score = probe_.score_;
    return probe_.max_score_result_;
  }

  ObRetrievalResultOrder result_order() const override { return order_; }

  void destroy() override
  {
    ++probe_.execution_destroy_count_;
    probe_.execution_close_order_ = ++probe_.close_clock_;
    delete this;
  }

private:
  int prepare()
  {
    int ret = OB_SUCCESS;
    if (prepared_) {
    } else if (query_.term_count_ < 1 || nullptr == query_.terms_
        || nullptr == query_.terms_[0].token_
        || query_.terms_[0].token_->get_int() != probe_.expected_term_
        || query_.boolean_intent_.size_ != 1
        || query_.boolean_intent_.data_[0] != probe_.expected_intent_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (RETRIEVAL_LOOKUP_RUN == run_.kind_) {
      if (run_.lookup_key_count_ > MAX_MATCHES) {
        ret = OB_SIZE_OVERFLOW;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < run_.lookup_key_count_; ++i) {
        if (OB_FAIL(matches_[i].doc_id_.assign(run_.lookup_keys_[i].doc_id_))) {
        } else {
          matches_[i].input_ordinal_ = run_.lookup_keys_[i].input_ordinal_;
          matches_[i].matched_ = 1 != i;
          matches_[i].score_ = matches_[i].matched_ ? i + 1.0 : 0.0;
        }
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < run_.range_count_; ++i) {
        if ((!run_.ranges_[i].has_lower_
                && nullptr != run_.ranges_[i].lower_.datum_)
            || (!run_.ranges_[i].has_upper_
                && nullptr != run_.ranges_[i].upper_.datum_)) {
          ret = OB_ERR_UNEXPECTED;
        }
      }
      int64_t ids[3] = {10, 20, 30};
      for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
        common::ObDatum datum = make_int_datum(ids[i]);
        if (OB_FAIL(matches_[i].doc_id_.assign(ObRetrievalDocIdView(datum)))) {
        } else {
          matches_[i].score_ = i + 1.0;
        }
      }
    }
    if (OB_SUCC(ret) && probe_.wrong_lookup_doc_
        && RETRIEVAL_LOOKUP_RUN == run_.kind_
        && run_.lookup_key_count_ > 0) {
      int64_t wrong_id = -1;
      common::ObDatum wrong_datum = make_int_datum(wrong_id);
      ret = matches_[0].doc_id_.assign(ObRetrievalDocIdView(wrong_datum));
    }
    if (OB_SUCC(ret)) {
      prepared_ = true;
    }
    return ret;
  }

  int64_t result_count() const
  {
    return RETRIEVAL_LOOKUP_RUN == run_.kind_
        ? run_.lookup_key_count_ : (probe_.empty_scan_ ? 0 : 3);
  }

private:
  static const int64_t MAX_MATCHES = 16;
  FakeProbe &probe_;
  const ObRetrievalCompileRequest &query_;
  const ObRetrievalRunRequest &run_;
  ObRetrievalResultOrder order_;
  int64_t next_index_;
  bool prepared_;
  ObRetrievalMatch matches_[MAX_MATCHES];
};

class FakeBinding final : public detail::ObIRetrievalCorpusBinding
{
public:
  explicit FakeBinding(FakeProbe &probe) : probe_(probe) {}

  int open(
      const ObRetrievalCompileRequest &query,
      const ObRetrievalRunRequest &run,
      detail::ObIRetrievalExecution *&execution) override
  {
    int ret = OB_SUCCESS;
    execution = nullptr;
    ++probe_.open_count_;
    if (probe_.fail_open_) {
      ret = OB_EAGAIN;
    } else {
      const ObRetrievalResultOrder order = RETRIEVAL_LOOKUP_RUN == run.kind_
          ? RETRIEVAL_LOOKUP_INPUT_ORDER : probe_.scan_order_;
      execution = new (std::nothrow) FakeExecution(probe_, query, run, order);
      if (nullptr == execution) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    return ret;
  }

  void destroy() override
  {
    ++probe_.binding_destroy_count_;
    probe_.binding_close_order_ = ++probe_.close_clock_;
    delete this;
  }

private:
  FakeProbe &probe_;
};

class FakeCorpusFactory final : public ObRetrievalCorpusFactory
{
public:
  explicit FakeCorpusFactory(FakeProbe &probe) : probe_(probe) {}

protected:
  int create_binding(detail::ObIRetrievalCorpusBinding *&binding) override
  {
    int ret = OB_SUCCESS;
    binding = new (std::nothrow) FakeBinding(probe_);
    if (nullptr == binding) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    return ret;
  }

private:
  FakeProbe &probe_;
};

struct QueryFixture
{
  QueryFixture()
    : term_value_(11), term_datum_(make_int_datum(term_value_)), term_(),
      intent_byte_(0x42), request_()
  {
    term_.token_ = &term_datum_;
    term_.weight_ = 2.0;
    request_.terms_ = &term_;
    request_.term_count_ = 1;
    request_.query_mode_ = RETRIEVAL_QUERY_BOOLEAN;
    request_.plan_intent_ = RETRIEVAL_ORDERED_SCAN;
    request_.max_batch_rows_ = 2;
    request_.need_query_max_score_ = true;
    request_.boolean_intent_.dialect_ = 1;
    request_.boolean_intent_.version_ = 1;
    request_.boolean_intent_.data_ = &intent_byte_;
    request_.boolean_intent_.size_ = 1;
  }

  int64_t term_value_;
  common::ObDatum term_datum_;
  ObRetrievalTermView term_;
  uint8_t intent_byte_;
  ObRetrievalCompileRequest request_;
};

TEST(RetrievalDocIdContract, OwnsPayloadAndRejectsInvalidValues)
{
  int64_t value = 42;
  common::ObDatum datum = make_int_datum(value);
  ObRetrievalDocId id;
  ASSERT_EQ(OB_SUCCESS, id.assign(ObRetrievalDocIdView(datum)));
  value = 7;
  EXPECT_EQ(42, id.datum().get_int());

  ObRetrievalDocId copy(id);
  id.reset();
  EXPECT_EQ(42, copy.datum().get_int());

  char oversized[ObRetrievalDocId::MAX_DOC_ID_BYTES + 1] = {};
  common::ObDatum oversized_datum(
      oversized, static_cast<uint32_t>(sizeof(oversized)), false);
  EXPECT_EQ(OB_BUF_NOT_ENOUGH,
      id.assign(ObRetrievalDocIdView(oversized_datum)));
  EXPECT_FALSE(id.is_valid());

  common::ObDatum null_datum;
  null_datum.set_null();
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      id.assign(ObRetrievalDocIdView(null_datum)));
}

TEST(RetrievalCompileContract, ValidatesIntentAndDeepCopiesQuery)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  ObRetrievalProgram program;
  ObRetrievalCompileInfo compile_info;
  ObRetrievalCompileRequest invalid_request = fixture.request_;
  invalid_request.term_count_ = -1;
  compile_info.scan_order_ = RETRIEVAL_LOOKUP_INPUT_ORDER;
  compile_info.max_batch_rows_ = 99;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      ObRetrievalProgram::compile(
          allocator, invalid_request, program, &compile_info));
  EXPECT_EQ(RETRIEVAL_UNSPECIFIED, compile_info.scan_order_);
  EXPECT_EQ(0, compile_info.max_batch_rows_);

  ObRetrievalCompileRequest oversized_request = fixture.request_;
  oversized_request.term_count_ = RETRIEVAL_MAX_REQUEST_ITEMS + 1;
  EXPECT_FALSE(oversized_request.is_valid());
  ASSERT_TRUE(fixture.request_.is_valid());
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(
          allocator, fixture.request_, program, &compile_info));
  EXPECT_EQ(RETRIEVAL_DOC_ID_ASC, compile_info.scan_order_);
  EXPECT_EQ(2, compile_info.max_batch_rows_);

  // The fake execution reads query views lazily on first pull.  Mutating the
  // original now proves that compile() retained its own bytes and datums.
  fixture.term_value_ = 99;
  fixture.intent_byte_ = 0x7f;

  FakeProbe probe;
  FakeCorpusFactory factory(probe);
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  int64_t ignored_endpoint = 77;
  common::ObDatum ignored_datum = make_int_datum(ignored_endpoint);
  ObRetrievalDocRangeView range;
  range.lower_ = ObRetrievalDocIdView(ignored_datum);
  ObRetrievalRunRequest run;
  run.ranges_ = &range;
  run.range_count_ = 1;
  ObRetrievalRunInfo run_info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, run_info));
  EXPECT_FALSE(corpus.is_valid());

  ObRetrievalBatchView batch;
  ASSERT_EQ(OB_SUCCESS, program.pull(1, batch));
  ASSERT_EQ(1, batch.count_);
  EXPECT_EQ(10, batch.matches_[0].doc_id_.datum().get_int());
}

TEST(RetrievalProgramContract, CapsBatchesAndCachesRunMetadata)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));
  EXPECT_EQ(RETRIEVAL_DOC_ID_ASC, info.actual_order_);
  EXPECT_EQ(1U, info.run_generation_);

  ObRetrievalBatchView first;
  ASSERT_EQ(OB_SUCCESS, program.pull(100, first));
  EXPECT_EQ(2, probe.last_capacity_);
  EXPECT_EQ(2, first.count_);
  EXPECT_TRUE(first.has_query_max_score_);
  EXPECT_DOUBLE_EQ(9.0, first.query_max_score_);

  ObRetrievalBatchView second;
  ASSERT_EQ(OB_SUCCESS, program.pull(100, second));
  EXPECT_EQ(1, second.count_);
  EXPECT_TRUE(second.end_);
  EXPECT_EQ(1, probe.max_score_count_);
  EXPECT_EQ(first.run_generation_, second.run_generation_);
}

TEST(RetrievalProgramContract, EmptyRunStillReportsFiniteNegativeMaxScore)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  fixture.term_.weight_ = -2.0;
  FakeProbe probe;
  probe.empty_scan_ = true;
  probe.score_ = -9.0;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_TRUE(fixture.request_.is_valid());
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));

  ObRetrievalBatchView batch;
  EXPECT_EQ(OB_ITER_END, program.pull(2, batch));
  EXPECT_TRUE(batch.end_);
  EXPECT_TRUE(batch.has_query_max_score_);
  EXPECT_DOUBLE_EQ(-9.0, batch.query_max_score_);
  EXPECT_EQ(1, probe.max_score_count_);

  ObRetrievalBatchView repeated_end;
  EXPECT_EQ(OB_ITER_END, program.pull(2, repeated_end));
  EXPECT_TRUE(repeated_end.end_);
  EXPECT_TRUE(repeated_end.has_query_max_score_);
  EXPECT_DOUBLE_EQ(-9.0, repeated_end.query_max_score_);
  EXPECT_EQ(1, probe.max_score_count_);
}

TEST(RetrievalProgramContract, LookupPreservesDuplicatesMissesAndOwnedRun)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));

  int64_t values[] = {7, 7, 9};
  common::ObDatum datums[] = {
      make_int_datum(values[0]), make_int_datum(values[1]), make_int_datum(values[2])};
  ObRetrievalLookupKeyView keys[3];
  for (int64_t i = 0; i < 3; ++i) {
    keys[i].doc_id_ = ObRetrievalDocIdView(datums[i]);
    keys[i].input_ordinal_ = 10 + i;
  }
  ObRetrievalRunRequest run;
  run.kind_ = RETRIEVAL_LOOKUP_RUN;
  run.lookup_keys_ = keys;
  run.lookup_key_count_ = 3;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));
  EXPECT_EQ(RETRIEVAL_LOOKUP_INPUT_ORDER, info.actual_order_);

  // FakeExecution intentionally keeps the run view and reads it lazily.
  // Mutating caller-owned inputs proves start() staged an owned run.
  values[0] = 100;
  values[1] = 101;
  values[2] = 102;
  keys[0].input_ordinal_ = 99;

  ObRetrievalBatchView first;
  ASSERT_EQ(OB_SUCCESS, program.pull(3, first));
  ASSERT_EQ(2, first.count_);
  EXPECT_EQ(7, first.matches_[0].doc_id_.datum().get_int());
  EXPECT_EQ(7, first.matches_[1].doc_id_.datum().get_int());
  EXPECT_EQ(10, first.matches_[0].input_ordinal_);
  EXPECT_EQ(11, first.matches_[1].input_ordinal_);
  EXPECT_FALSE(first.matches_[1].matched_);
  EXPECT_DOUBLE_EQ(0.0, first.matches_[1].score_);
  EXPECT_FALSE(first.end_);

  ObRetrievalBatchView second;
  ASSERT_EQ(OB_SUCCESS, program.pull(3, second));
  ASSERT_EQ(1, second.count_);
  EXPECT_EQ(9, second.matches_[0].doc_id_.datum().get_int());
  EXPECT_EQ(12, second.matches_[0].input_ordinal_);
  EXPECT_TRUE(second.end_);
}

TEST(RetrievalProgramContract, RebindFailurePreservesCorpusAndCursor)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe old_probe;
  FakeCorpusFactory old_factory(old_probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus old_corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, old_factory.create(old_corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, old_corpus, info));

  ObRetrievalBatchView first;
  ASSERT_EQ(OB_SUCCESS, program.pull(1, first));
  EXPECT_EQ(10, first.matches_[0].doc_id_.datum().get_int());

  FakeProbe candidate_probe;
  candidate_probe.fail_open_ = true;
  FakeCorpusFactory candidate_factory(candidate_probe);
  ObRetrievalCorpus candidate;
  ASSERT_EQ(OB_SUCCESS, candidate_factory.create(candidate));
  EXPECT_EQ(OB_EAGAIN, program.start(run, candidate, info));
  EXPECT_TRUE(candidate.is_valid());
  EXPECT_EQ(RETRIEVAL_UNSPECIFIED, info.actual_order_);
  EXPECT_EQ(0U, info.run_generation_);
  EXPECT_EQ(RETRIEVAL_PROGRAM_RUNNING, program.state());

  ObRetrievalBatchView after_failure;
  ASSERT_EQ(OB_SUCCESS, program.pull(1, after_failure));
  EXPECT_EQ(20, after_failure.matches_[0].doc_id_.datum().get_int());

  old_probe.fail_open_ = true;
  info.actual_order_ = RETRIEVAL_DOC_ID_ASC;
  info.run_generation_ = 99;
  EXPECT_EQ(OB_EAGAIN, program.start(run, info));
  EXPECT_EQ(RETRIEVAL_UNSPECIFIED, info.actual_order_);
  EXPECT_EQ(0U, info.run_generation_);
  EXPECT_EQ(RETRIEVAL_PROGRAM_RUNNING, program.state());
  old_probe.fail_open_ = false;

  ObRetrievalBatchView after_restart_failure;
  ASSERT_EQ(OB_SUCCESS, program.pull(1, after_restart_failure));
  EXPECT_EQ(30, after_restart_failure.matches_[0].doc_id_.datum().get_int());
}

TEST(RetrievalProgramContract, WrongOrderRollbackPreservesCurrentCursor)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));

  ObRetrievalBatchView first;
  ASSERT_EQ(OB_SUCCESS, program.pull(1, first));
  EXPECT_EQ(10, first.matches_[0].doc_id_.datum().get_int());

  probe.scan_order_ = RETRIEVAL_UNSPECIFIED;
  EXPECT_EQ(OB_ERR_UNEXPECTED, program.start(run, info));
  EXPECT_EQ(RETRIEVAL_PROGRAM_RUNNING, program.state());
  EXPECT_EQ(1, probe.execution_destroy_count_);
  probe.scan_order_ = RETRIEVAL_DOC_ID_ASC;

  ObRetrievalBatchView after_failure;
  ASSERT_EQ(OB_SUCCESS, program.pull(1, after_failure));
  EXPECT_EQ(20, after_failure.matches_[0].doc_id_.datum().get_int());
}

TEST(RetrievalProgramContract, StickyErrorWinsValidationAndStartRecovers)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));

  probe.fail_next_ = true;
  ObRetrievalBatchView batch;
  EXPECT_EQ(OB_TIMEOUT, program.pull(1, batch));
  EXPECT_EQ(RETRIEVAL_PROGRAM_FAILED, program.state());
  EXPECT_EQ(OB_TIMEOUT, program.first_error());
  EXPECT_EQ(OB_TIMEOUT, program.pull(0, batch));

  probe.fail_next_ = false;
  ASSERT_EQ(OB_SUCCESS, program.start(run, info));
  EXPECT_EQ(RETRIEVAL_PROGRAM_READY, program.state());
  EXPECT_EQ(OB_SUCCESS, program.first_error());
  EXPECT_EQ(2U, info.run_generation_);
  EXPECT_EQ(OB_SUCCESS, program.pull(1, batch));
}

TEST(RetrievalProgramContract, MetadataControlCodeBecomesStickyHardError)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  probe.max_score_result_ = OB_ITER_END;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));

  ObRetrievalBatchView batch;
  EXPECT_EQ(OB_ERR_UNEXPECTED, program.pull(1, batch));
  EXPECT_EQ(RETRIEVAL_PROGRAM_FAILED, program.state());
  EXPECT_EQ(OB_ERR_UNEXPECTED, program.first_error());
  EXPECT_EQ(0, probe.next_count_);
  EXPECT_EQ(nullptr, batch.matches_);
  EXPECT_EQ(OB_ERR_UNEXPECTED, program.pull(0, batch));
}

TEST(RetrievalProgramContract, FailedStartClearsOutputWithoutProgram)
{
  ObRetrievalProgram program;
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  info.actual_order_ = RETRIEVAL_DOC_ID_ASC;
  info.run_generation_ = 99;
  EXPECT_EQ(OB_NOT_INIT, program.start(run, info));
  EXPECT_EQ(RETRIEVAL_UNSPECIFIED, info.actual_order_);
  EXPECT_EQ(0U, info.run_generation_);
}

TEST(RetrievalProgramContract, RejectsDirtyIteratorEnd)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  probe.empty_scan_ = true;
  probe.dirty_iter_end_ = true;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
  ObRetrievalRunRequest run;
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));

  ObRetrievalBatchView batch;
  EXPECT_EQ(OB_ERR_UNEXPECTED, program.pull(1, batch));
  EXPECT_EQ(RETRIEVAL_PROGRAM_FAILED, program.state());
  EXPECT_EQ(nullptr, batch.matches_);
  EXPECT_EQ(0, batch.count_);
}

TEST(RetrievalProgramContract, RejectsLookupDocumentMismatch)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  probe.wrong_lookup_doc_ = true;
  FakeCorpusFactory factory(probe);
  ObRetrievalProgram program;
  ObRetrievalCorpus corpus;
  ASSERT_EQ(OB_SUCCESS,
      ObRetrievalProgram::compile(allocator, fixture.request_, program));
  ASSERT_EQ(OB_SUCCESS, factory.create(corpus));

  int64_t value = 7;
  common::ObDatum datum = make_int_datum(value);
  ObRetrievalLookupKeyView key;
  key.doc_id_ = ObRetrievalDocIdView(datum);
  key.input_ordinal_ = 3;
  ObRetrievalRunRequest run;
  run.kind_ = RETRIEVAL_LOOKUP_RUN;
  run.lookup_keys_ = &key;
  run.lookup_key_count_ = 1;
  ObRetrievalRunRequest paged_lookup = run;
  paged_lookup.offset_ = 1;
  EXPECT_FALSE(paged_lookup.is_valid());
  ObRetrievalRunRequest oversized_lookup = run;
  oversized_lookup.lookup_key_count_ = RETRIEVAL_MAX_REQUEST_ITEMS + 1;
  EXPECT_FALSE(oversized_lookup.is_valid());
  ObRetrievalRunInfo info;
  ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));

  ObRetrievalBatchView batch;
  EXPECT_EQ(OB_ERR_UNEXPECTED, program.pull(1, batch));
  EXPECT_EQ(RETRIEVAL_PROGRAM_FAILED, program.state());
}

TEST(RetrievalProgramContract, FacadeOwnsInvariantsAndDestructionOrder)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  QueryFixture fixture;
  FakeProbe probe;
  FakeCorpusFactory factory(probe);
  {
    ObRetrievalProgram program;
    ObRetrievalCorpus corpus;
    ASSERT_EQ(OB_SUCCESS,
        ObRetrievalProgram::compile(allocator, fixture.request_, program));
    ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
    ObRetrievalRunRequest run;
    ObRetrievalRunInfo info;
    ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, info));
    probe.invalid_batch_ = true;
    ObRetrievalBatchView batch;
    EXPECT_EQ(OB_ERR_UNEXPECTED, program.pull(1, batch));
    EXPECT_EQ(RETRIEVAL_PROGRAM_FAILED, program.state());
  }
  EXPECT_EQ(1, probe.execution_destroy_count_);
  EXPECT_EQ(1, probe.binding_destroy_count_);
  EXPECT_LT(probe.execution_close_order_, probe.binding_close_order_);
}

TEST(RetrievalProgramContract, ReportsAllProductionOrderProfilesExactly)
{
  common::ObArenaAllocator allocator("RetrievalTest");
  const ObRetrievalPlanIntent intents[] = {
      RETRIEVAL_ORDERED_SCAN, RETRIEVAL_BOUNDED_TOP_K, RETRIEVAL_ACCUMULATE};
  const ObRetrievalResultOrder orders[] = {
      RETRIEVAL_DOC_ID_ASC,
      RETRIEVAL_SCORE_ASC_TIES_UNSPECIFIED,
      RETRIEVAL_UNSPECIFIED};
  for (int64_t i = 0; i < 3; ++i) {
    QueryFixture fixture;
    fixture.request_.plan_intent_ = intents[i];
    FakeProbe probe;
    probe.scan_order_ = orders[i];
    FakeCorpusFactory factory(probe);
    ObRetrievalProgram program;
    ObRetrievalCorpus corpus;
    ObRetrievalCompileInfo compile_info;
    ASSERT_EQ(OB_SUCCESS,
        ObRetrievalProgram::compile(
            allocator, fixture.request_, program, &compile_info));
    EXPECT_EQ(orders[i], compile_info.scan_order_);
    ASSERT_EQ(OB_SUCCESS, factory.create(corpus));
    ObRetrievalRunRequest run;
    ObRetrievalRunInfo run_info;
    ASSERT_EQ(OB_SUCCESS, program.start(run, corpus, run_info));
    EXPECT_EQ(orders[i], run_info.actual_order_);
    program.reset();
    allocator.reuse();
  }
}

} // namespace
} // namespace data_plane
} // namespace oceanbase
