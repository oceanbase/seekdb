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

#define USING_LOG_PREFIX SQL_DAS

#include "sql/das/iter/sparse_retrieval/ob_das_tr_merge_iter.h"

#include <mutex>

#include "sql/das/iter/sparse_retrieval/ob_das_text_retrieval_engine.h"

namespace oceanbase
{
using namespace share;
namespace sql
{
namespace
{

struct ObDASTextRetrievalProviderSlot
{
  ObDASTextRetrievalProviderSlot()
    : lock_(), factory_(nullptr), query_builder_(nullptr)
  {}

  std::mutex lock_;
  ObDASTextRetrievalEngineFactory factory_;
  ObDASTextRetrievalQueryBuilder query_builder_;
};

ObDASTextRetrievalProviderSlot &provider_slot()
{
  static ObDASTextRetrievalProviderSlot slot;
  return slot;
}

} // namespace

int install_das_text_retrieval_engine_factory(
    ObDASTextRetrievalEngineFactory factory,
    ObDASTextRetrievalQueryBuilder query_builder)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(factory) || OB_ISNULL(query_builder)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObDASTextRetrievalProviderSlot &slot = provider_slot();
    std::lock_guard<std::mutex> guard(slot.lock_);
    if (nullptr == slot.factory_ && nullptr == slot.query_builder_) {
      slot.factory_ = factory;
      slot.query_builder_ = query_builder;
    } else if (slot.factory_ != factory || slot.query_builder_ != query_builder) {
      ret = OB_INIT_TWICE;
    }
  }
  return ret;
}

int create_das_text_retrieval_engine(
    common::ObIAllocator &allocator,
    ObIDASTextRetrievalEngine *&engine)
{
  int ret = OB_SUCCESS;
  ObDASTextRetrievalEngineFactory factory = nullptr;
  engine = nullptr;
  {
    ObDASTextRetrievalProviderSlot &slot = provider_slot();
    std::lock_guard<std::mutex> guard(slot.lock_);
    factory = slot.factory_;
  }
  if (OB_ISNULL(factory)) {
    ret = OB_NOT_INIT;
    LOG_ERROR("DAS text retrieval composition provider is not installed", K(ret));
  } else if (OB_FAIL(factory(allocator, engine))) {
  } else if (OB_ISNULL(engine)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("text retrieval engine factory returned null", K(ret));
  }
  return ret;
}

int build_das_text_retrieval_query(
    const ObDASIRScanCtDef *ir_ctdef,
    ObDASIRScanRtDef *ir_rtdef,
    common::ObIAllocator &allocator,
    ObArray<ObString> &query_tokens,
    ObArray<double> &boost_values,
    ObFtsEvalNode *&root_node,
    bool &has_duplicate_boolean_tokens)
{
  int ret = OB_SUCCESS;
  ObDASTextRetrievalQueryBuilder query_builder = nullptr;
  {
    ObDASTextRetrievalProviderSlot &slot = provider_slot();
    std::lock_guard<std::mutex> guard(slot.lock_);
    query_builder = slot.query_builder_;
  }
  if (OB_ISNULL(query_builder)) {
    ret = OB_NOT_INIT;
    LOG_ERROR("DAS text retrieval query provider is not installed", K(ret));
  } else if (OB_FAIL(query_builder(
      ir_ctdef, ir_rtdef, allocator, query_tokens, boost_values, root_node,
      has_duplicate_boolean_tokens))) {
  }
  return ret;
}

ObDASTRMergeIter::ObDASTRMergeIter()
  : ObDASIter(ObDASIterType::DAS_ITER_TEXT_RETRIEVAL_MERGE),
    engine_allocator_(lib::ObMemAttr("DASRetrieval"), OB_MALLOC_NORMAL_BLOCK_SIZE),
    engine_(nullptr),
    is_inited_(false)
{}

int ObDASTRMergeIter::inner_init(ObDASIterParam &param)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("double initialization", K(ret));
  } else if (OB_UNLIKELY(
      ObDASIterType::DAS_ITER_TEXT_RETRIEVAL_MERGE != param.type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid DAS iter param type for text retrieval", K(ret), K(param));
  } else if (OB_FAIL(create_das_text_retrieval_engine(
      engine_allocator_, engine_))) {
  } else if (OB_FAIL(engine_->init(
      static_cast<ObDASTRMergeIterParam &>(param)))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObDASTRMergeIter::set_related_tablet_ids(
    const ObDASFTSTabletID &related_tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(engine_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(engine_->set_related_tablet_ids(related_tablet_ids))) {
  }
  return ret;
}

int ObDASTRMergeIter::do_table_scan()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT || OB_ISNULL(engine_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("text retrieval facade is not initialized", K(ret));
  } else if (OB_FAIL(engine_->bind_source_tree(children_, children_cnt_))) {
  } else if (OB_FAIL(engine_->do_table_scan())) {
  }
  return ret;
}

int ObDASTRMergeIter::inner_reuse()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT || OB_ISNULL(engine_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(engine_->bind_source_tree(children_, children_cnt_))) {
  } else if (OB_FAIL(engine_->reuse())) {
  }
  return ret;
}

int ObDASTRMergeIter::rescan()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT || OB_ISNULL(engine_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(engine_->bind_source_tree(children_, children_cnt_))) {
  } else if (OB_FAIL(engine_->rescan())) {
  }
  return ret;
}

int ObDASTRMergeIter::inner_release()
{
  if (OB_NOT_NULL(engine_)) {
    engine_->destroy();
    engine_ = nullptr;
  }
  engine_allocator_.reset();
  is_inited_ = false;
  return OB_SUCCESS;
}

int ObDASTRMergeIter::inner_get_next_row()
{
  return IS_NOT_INIT || OB_ISNULL(engine_)
      ? OB_NOT_INIT : engine_->get_next_row();
}

int ObDASTRMergeIter::inner_get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  count = 0;
  if (IS_NOT_INIT || OB_ISNULL(engine_)) {
    ret = OB_NOT_INIT;
  } else {
    ret = engine_->get_next_rows(count, capacity);
  }
  return ret;
}

int ObDASTRMergeIter::set_children_iter_rangekey(
    const common::ObIArray<std::pair<ObDocIdExt, int>> &virtual_rangekeys,
    const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT || OB_ISNULL(engine_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(engine_->bind_source_tree(children_, children_cnt_))) {
  } else if (OB_FAIL(engine_->set_lookup_keys(
      virtual_rangekeys, batch_size))) {
  }
  return ret;
}

bool ObDASTRMergeIter::is_taat_mode()
{
  return OB_NOT_NULL(engine_) && engine_->is_taat_mode();
}

int ObDASTRMergeIter::get_query_max_score(double &score)
{
  score = 0.0;
  return IS_NOT_INIT || OB_ISNULL(engine_)
      ? OB_NOT_INIT : engine_->get_query_max_score(score);
}

int ObDASTRMergeIter::build_query_tokens(
    const ObDASIRScanCtDef *ir_ctdef,
    ObDASIRScanRtDef *ir_rtdef,
    common::ObIAllocator &allocator,
    ObArray<ObString> &query_tokens,
    ObArray<double> &boost_values,
    ObFtsEvalNode *&root_node,
    bool &has_duplicate_boolean_tokens)
{
  return build_das_text_retrieval_query(
      ir_ctdef, ir_rtdef, allocator, query_tokens, boost_values, root_node,
      has_duplicate_boolean_tokens);
}

} // namespace sql
} // namespace oceanbase
