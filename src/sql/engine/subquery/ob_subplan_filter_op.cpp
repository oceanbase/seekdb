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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_subplan_filter_op.h"
#include "lib/ob_abort.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

bool DatumRow::operator==(const DatumRow &other) const
{
  bool equal = cnt_ == other.cnt_ && types_ != nullptr && other.types_ != nullptr;
  for (int64_t i = 0; equal && i < cnt_; ++i) {
    bool cell_equal = false;
    const SqlType *type = types_[i];
    const SqlType *other_type = other.types_[i];
    equal = type != nullptr && other_type != nullptr && type->equals(*other_type)
        && OB_SUCCESS == DatumCopyPlan(*type).binary_equal(
            elems_[i], other.elems_[i], cell_equal) && cell_equal;
  }
  return equal;
}

int DatumRow::hash(uint64_t &hash_val, uint64_t seed) const
{
  int ret = OB_SUCCESS;
  uint64_t candidate = seed;
  if (types_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  }
  // Preserve the cache's historical binary key semantics, independently of
  // SQL collation equality. The typed view owns no data beyond this call.
  for (int64_t i = 0; OB_SUCC(ret) && i < cnt_; ++i) {
    alignas(8) char scratch[64];
    ObDatum legacy;
    if (OB_ISNULL(types_[i])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(DatumCopyPlan(*types_[i]).legacy_view(
        elems_[i], legacy, scratch, sizeof(scratch)))) {
    } else {
      candidate = murmurhash(legacy.ptr_, legacy.len_, candidate);
    }
  }
  if (OB_SUCC(ret)) { hash_val = candidate; }
  return ret;
}

ObSubQueryIterator::ObSubQueryIterator(ObOperator &op)
    : op_(op),
    onetime_plan_(false),
    init_plan_(false),
    inited_(false),
    store_("SpfIter"),
    mem_entity_(nullptr),
    cache_value_owner_(nullptr),
    id_(-1),
    parent_(nullptr),
    memory_used_(0),
    eval_ctx_(op.get_eval_ctx()),
    iter_brs_(NULL),
    batch_size_(0),
    batch_row_pos_(0),
    iter_end_(false)
{
}

ObSubQueryIterator::~ObSubQueryIterator()
{
  const int ret = close_cache_values();
  if (ret != OB_SUCCESS) {
    LOG_ERROR("SPF iterator resource cleanup failed before arena destroy", K(ret));
    ob_abort();
  }
  if (cache_value_owner_ != nullptr) {
    cache_value_owner_->~NativeSqlValueOwner();
    op_.get_exec_ctx().get_allocator().free(cache_value_owner_);
    cache_value_owner_ = nullptr;
  }
  if (hashmap_.created()) { hashmap_.destroy(); }
  if (mem_entity_ != nullptr) { DESTROY_CONTEXT(mem_entity_); mem_entity_ = nullptr; }
}

int ObSubQueryIterator::retain_scalar_result(const Datum &source, const SqlType &type, Datum &output)
{
  return parent_ == nullptr ? OB_NOT_INIT : parent_->retain_scalar_result(source, type, output);
}

int ObSubQueryIterator::cache_key_eligible(bool &eligible) const
{
  int ret = OB_SUCCESS;
  bool candidate = true;
  if (probe_row_.cnt_ < 0 || probe_row_.types_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
  for (int64_t i = 0; ret == OB_SUCCESS && i < probe_row_.cnt_; ++i) {
    if (probe_row_.types_[i] == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else if (probe_row_.types_[i]->requires_resource_clone()) { candidate = false; }
  }
  if (ret == OB_SUCCESS) { eligible = candidate; }
  return ret;
}

int ObSubQueryIterator::get_cache_value_owner(NativeSqlValueOwner *&owner)
{
  int ret = OB_SUCCESS;
  if (mem_entity_ == nullptr) { ret = OB_NOT_INIT; }
  else if (cache_value_owner_ == nullptr) {
    void *memory = op_.get_exec_ctx().get_allocator().alloc(sizeof(NativeSqlValueOwner));
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      cache_value_owner_ = new (memory) NativeSqlValueOwner(
          mem_entity_->get_arena_allocator(), op_.get_exec_ctx().get_my_session());
    }
  }
  if (ret == OB_SUCCESS) { owner = cache_value_owner_; }
  return ret;
}

int ObSubQueryIterator::close_cache_values()
{
  int ret = OB_SUCCESS;
  // The map must stop borrowing resource values before any pointee is released.
  if (hashmap_.created()) { ret = hashmap_.reuse(); }
  if (ret == OB_SUCCESS && cache_value_owner_ != nullptr) {
    ret = cache_value_owner_->enforce_empty_before_destroy();
  }
  return ret;
}

int ObSubQueryIterator::store_retained_resource_row()
{
  struct LegacyScratch { alignas(8) char bytes_[64]; };
  ObSEArray<ObDatum, 8> views;
  ObSEArray<LegacyScratch, 8> scratches;
  const auto &expressions = get_output();
  int ret = OB_SUCCESS;
  if (stored_output_types_.empty()) {
    for (int64_t i = 0; ret == OB_SUCCESS && i < expressions.count(); ++i) {
      const SqlType *type = nullptr;
      if (expressions.at(i) == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(expressions.at(i)->get_sql_type().clone_to(
          op_.get_exec_ctx().get_allocator(), type))) {}
      else { ret = stored_output_types_.push_back(type); }
    }
  }
  if (ret == OB_SUCCESS) { ret = validate_stored_output_types(); }
  if (ret == OB_SUCCESS) { ret = views.prepare_allocate(expressions.count()); }
  if (ret == OB_SUCCESS) { ret = scratches.prepare_allocate(expressions.count()); }
  for (int64_t i = 0; ret == OB_SUCCESS && i < expressions.count(); ++i) {
    const ObExpr *expr = expressions.at(i);
    Datum *source = nullptr;
    Datum retained;
    if (expr == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else if (OB_FAIL(expr->eval(op_.get_eval_ctx(), source))) {}
    else if (expr->get_sql_type().requires_resource_clone()
        && OB_FAIL(retain_scalar_result(*source, expr->get_sql_type(), retained))) {}
    else {
      const Datum &value = expr->get_sql_type().requires_resource_clone() ? retained : *source;
      ret = expr->get_datum_copy_plan().legacy_view(value, views.at(i), scratches.at(i).bytes_, 64);
    }
  }
  // The writer owns descriptor bytes; the execution owner retains pointees.
  if (ret == OB_SUCCESS) { ret = store_.add_row(views.get_data(), views.count(), 0, nullptr); }
  return ret;
}

int ObSubQueryIterator::validate_stored_output_types() const
{
  int ret = OB_SUCCESS;
  if (!stored_output_types_.empty()) {
    if (stored_output_types_.count() != get_output().count()) { ret = OB_ERR_UNEXPECTED; }
    for (int64_t i = 0; ret == OB_SUCCESS && i < stored_output_types_.count(); ++i) {
      if (get_output().at(i) == nullptr || stored_output_types_.at(i) == nullptr
          || !stored_output_types_.at(i)->equals(get_output().at(i)->get_sql_type())) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  return ret;
}

int ObSubQueryIterator::get_next_row()
{
  int ret = OB_SUCCESS;
  bool is_from_store = init_plan_ && inited_;
  if (is_from_store) {
    ret = validate_stored_output_types();
    if (ret == OB_SUCCESS) { ret = store_it_.get_next_row(get_output(), op_.get_eval_ctx()); }
  } else if (parent_->enable_left_das_batch()) {
    uint64_t parent_spf_group = 0;
    int64_t parent_group_rescan_cnt = 0;
    const GroupParamArray *group_params = nullptr;
    parent_->get_current_group(parent_spf_group);
    parent_->get_current_batch_cnt(parent_group_rescan_cnt);
    group_params = parent_->get_rescan_params_info();
    GroupParamBackupGuard guard(op_.get_exec_ctx().get_das_ctx());
    guard.bind_batch_rescan_params(parent_spf_group, parent_group_rescan_cnt, group_params);
    ret = get_next_row_from_child();
  } else {
    ret = get_next_row_from_child();
  }
  return ret;
}

int ObSubQueryIterator::init_batch_rows_holder(const common::ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  ret = brs_holder_.init(exprs, eval_ctx);
  return ret;
}
int ObSubQueryIterator::get_next_row_from_child()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(op_.get_spec().is_vectorized())) {
    ret = get_next_row_vecrorizely();
  } else {
    ret = op_.get_next_row();
  }
  return ret;
}

int ObSubQueryIterator::get_next_row_vecrorizely()
{
  int ret = OB_SUCCESS;
  const int64_t max_row_cnt = INT64_MAX;
  if (NULL == iter_brs_) {
    if (OB_FAIL(op_.get_next_batch(max_row_cnt, iter_brs_))) {
    } else if (OB_FAIL(brs_holder_.save(1))) {
    }
    // backup datums[0]
  }

  while(OB_SUCC(ret)) {
    if (batch_row_pos_ >= iter_brs_->size_ && iter_brs_->end_) {
      ret = OB_ITER_END;
      break;
    }

    while (batch_row_pos_ < iter_brs_->size_ && iter_brs_->skip_->at(batch_row_pos_)) {
      batch_row_pos_++;
    }

    if (batch_row_pos_ >= iter_brs_->size_) {
      if (!iter_brs_->end_) {
        brs_holder_.restore();
        if (OB_FAIL(op_.get_next_batch(max_row_cnt, iter_brs_))) {
        } else {
          batch_row_pos_ = 0;
          if (0 == iter_brs_->size_ && iter_brs_->end_) {
            ret = OB_ITER_END;
            break;
          } else if (OB_FAIL(brs_holder_.save(1))) {
          }
        }
      }
      continue;
    } else {
      // got row, increase the index to next row
      batch_row_pos_ += 1;
      break;
    }
  }

  // overwrite datums[0]: shallow copy
  for (int i = 0; OB_SUCC(ret) && i < op_.get_spec().output_.count(); i++) {
    ObExpr *expr = op_.get_spec().output_.at(i);
    if (expr->is_batch_result() && 0 != cur_idx()) {
      Datum *datums = expr->locate_batch_datums(eval_ctx_);
      datums[0] = datums[cur_idx()];
      LOG_DEBUG("copy datum to datum[0], details: ", K(cur_idx()), K(datums[0]),
               K(expr->locate_batch_datums(eval_ctx_)),
               KPC(expr->locate_batch_datums(eval_ctx_)), K(expr), KPC(expr));
    } // non batch expr always point to offset 0, do nothing for else brach
  }
  eval_ctx_.set_batch_size(1);
  eval_ctx_.set_batch_idx(0);
  return ret;
}


void ObSubQueryIterator::drain_exch()
{
  op_.drain_exch();
}

int ObSubQueryIterator::rewind(const bool reset_onetime_plan /* = false */)
{
  // According to the semantics of subplan filter, reset row iterator, other members remain unchanged
  int ret = OB_SUCCESS;
  if (onetime_plan_ && !reset_onetime_plan) {
    // for onetime expr
  } else if (init_plan_) {
    // for init plan
    if (OB_FAIL(store_.begin(store_it_))) {
    }
  } else {
    if (parent_->enable_left_das_batch()) {
      {
        uint64_t parent_spf_group = 0;
        int64_t parent_group_rescan_cnt = 0;
        const GroupParamArray *group_params = nullptr;
        if(OB_SUCC(ret)) {
          parent_->get_current_group(parent_spf_group);
          parent_->get_current_batch_cnt(parent_group_rescan_cnt);
          group_params = parent_->get_rescan_params_info();
          GroupParamBackupGuard guard(op_.get_exec_ctx().get_das_ctx());
          guard.bind_batch_rescan_params(parent_spf_group, parent_group_rescan_cnt, group_params);
          if (OB_FAIL(op_.rescan())) {
          }
        }
      }
    } else {
      //No batch branch
      if (OB_SUCC(ret) && OB_FAIL(op_.rescan())) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    iter_end_ = false;
    //for vectorize mode, SPF iter may have a stored batch to process
    //should reset them in rewind()
    iter_brs_ = NULL;
    batch_size_ = 0;
    batch_row_pos_ = 0;
  }
  return ret;
}

void ObSubQueryIterator::reuse()
{
  inited_ = false;
  store_it_.reset();
  store_.reset();
  stored_output_types_.reuse();
  iter_brs_ = NULL;
  batch_size_ = 0;
  batch_row_pos_ = 0;
  iter_end_ = false;
}
//TODO move to the corresponding expr, set a flag to ensure it is calculated only once
int ObSubQueryIterator::prepare_init_plan()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    bool resource_output = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < get_output().count(); ++i) {
      if (get_output().at(i) == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else { resource_output |= get_output().at(i)->get_sql_type().requires_resource_clone(); }
    }
    if (OB_SUCC(ret) && !store_.is_inited()) {
      // TODO bin.lb: use auto memory management
      OZ(store_.init(1L << 20));
      OZ(store_.alloc_dir_id());
    }
    if (OB_FAIL(ret)) {
    } else if (op_.is_vectorized()) {
      const ObBatchRows *iter_brs = NULL;
      do {
        int64_t stored_rows_count = 0;
        if (OB_FAIL(op_.get_next_batch(op_.get_spec().max_batch_size_, iter_brs))) {
        } else if (resource_output) {
          ObEvalCtx::BatchInfoScopeGuard guard(op_.get_eval_ctx());
          guard.set_batch_size(iter_brs->size_);
          for (int64_t i = 0; OB_SUCC(ret) && i < iter_brs->size_; ++i) {
            if (!iter_brs->skip_->at(i)) {
              guard.set_batch_idx(i);
              ret = store_retained_resource_row();
            }
          }
        } else if (OB_FAIL(store_.add_batch(get_output(), op_.get_eval_ctx(), *iter_brs->skip_,
                                            iter_brs->size_, stored_rows_count))) {
        }
      } while (OB_SUCC(ret) && !iter_brs->end_);
      if (OB_SUCC(ret)) {
        inited_ = true;
        OZ(store_.finish_add_row());
        OZ(store_it_.init(&store_));
      }
    } else {
      while (OB_SUCC(ret) && OB_SUCC(get_next_row())) {
        if (resource_output) { OZ(store_retained_resource_row()); }
        else { OZ(store_.add_row(get_output(), &op_.get_eval_ctx())); }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        inited_ = true;
        OZ(store_.finish_add_row());
        OZ(store_it_.init(&store_));
      }
    }
  }
  return ret;
}

int ObSubQueryIterator::init_mem_entity()
{
  int ret = OB_SUCCESS;
  lib::ContextParam param;
  param.set_mem_attr(ObMemAttr("SqlSQIterator",
        ObCtxIds::DEFAULT_CTX_ID));
  param.set_properties(lib::USE_TL_PAGE_OPTIONAL);
  if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_entity_, param))) {
  } else if (OB_ISNULL(mem_entity_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObSubQueryIterator::init_probe_row(const int64_t cnt)
{
  int ret = OB_SUCCESS;
  probe_row_.cnt_ = cnt;
  if (OB_ISNULL(probe_row_.elems_ =
      static_cast<Datum *>(op_.get_exec_ctx().get_allocator().alloc(cnt * sizeof(Datum))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    new (probe_row_.elems_) Datum[cnt];
    probe_row_.types_ = static_cast<const SqlType **>(
        op_.get_exec_ctx().get_allocator().alloc(sizeof(SqlType *) * cnt));
    if (probe_row_.types_ == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  }
  return ret;
}

int ObSubQueryIterator::get_arena_allocator(common::ObIAllocator *&alloc)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mem_entity_)) {
    ret = OB_NOT_INIT;
  } else {
    alloc = &mem_entity_->get_arena_allocator();
  }
  return ret;
}

int ObSubQueryIterator::get_curr_probe_row()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *pctx = nullptr;
  const ObSubPlanFilterSpec &spec = static_cast<const ObSubPlanFilterSpec &> (parent_->get_spec());
  if (OB_ISNULL(probe_row_.elems_) || probe_row_.cnt_ != spec.exec_param_array_[get_iter_id() - 1/*ignore child 0*/].count()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(pctx = op_.get_exec_ctx().get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // put exec param into probe_row
    for (int64_t i = 0; i < probe_row_.cnt_; ++i) {
      const ObExpr *expr = spec.exec_param_array_[get_iter_id() - 1][i];
      probe_row_.elems_[i] = expr->locate_expr_datum(op_.get_eval_ctx());
      probe_row_.types_[i] = &expr->get_sql_type();
    }
  }
  return ret;
}

int ObSubQueryIterator::get_refactored(Datum &out, const ObExpr &result_expr)
{
  CachedResult cached;
  bool eligible = false;
  int ret = cache_key_eligible(eligible);
  if (ret == OB_SUCCESS && !eligible) { ret = OB_HASH_NOT_EXIST; }
  if (ret == OB_SUCCESS) { ret = hashmap_.get_refactored(probe_row_, cached); }
  if (OB_SUCC(ret)) {
    if (cached.type_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else if (!cached.type_->equals(result_expr.get_sql_type())) { ret = OB_HASH_NOT_EXIST; }
    else if (cached.type_->requires_resource_clone()) {
      ret = retain_scalar_result(cached.value_, *cached.type_, out);
    } else { out = cached.value_; }
  }
  return ret;
}

int ObSubQueryIterator::prepare_cache_result(const Datum &result, const ObExpr &result_expr,
                                             PendingCacheEntry &pending)
{
  bool eligible = false;
  int ret = cache_key_eligible(eligible);
  if (ret != OB_SUCCESS) { return ret; }
  if (!eligible) { pending = PendingCacheEntry(); return OB_SUCCESS; }
  int64_t bytes = sizeof(Datum) * (probe_row_.cnt_ + 1)
      + sizeof(SqlType *) * probe_row_.cnt_;
  int64_t span = 0;
  ObIAllocator *allocator = nullptr;
  if (probe_row_.types_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(result_expr.get_datum_copy_plan().carrier_span_size(result, span))) {
  } else if (span > INT64_MAX - bytes) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    bytes += span;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < probe_row_.cnt_; ++i) {
    if (OB_FAIL(DatumCopyPlan(*probe_row_.types_[i]).carrier_span_size(
        probe_row_.elems_[i], span))) {
    } else if (span > INT64_MAX - bytes) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      bytes += span;
    }
  }
  if (OB_FAIL(ret) || !check_can_insert(bytes)) {
  } else if (OB_FAIL(get_arena_allocator(allocator)) || OB_ISNULL(allocator)) {
    if (OB_SUCC(ret)) { ret = OB_ERR_UNEXPECTED; }
  } else {
    DatumRow key;
    Datum value;
    const SqlType *result_type = nullptr;
    key.cnt_ = probe_row_.cnt_;
    if (OB_ISNULL(key.elems_ = static_cast<Datum *>(allocator->alloc(sizeof(Datum) * key.cnt_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_ISNULL(key.types_ = static_cast<const SqlType **>(
        allocator->alloc(sizeof(SqlType *) * key.cnt_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new (key.elems_) Datum[key.cnt_];
      // A cache entry outlives the input frame. Resources must use the real
      // domain cloner; copying only their wrapper would retain stale pointees.
      if (OB_FAIL(result_expr.get_sql_type().clone_to(*allocator, result_type))) {
      } else if (result_type->requires_resource_clone()) {
        NativeSqlValueOwner *owner = nullptr;
        if (OB_FAIL(get_cache_value_owner(owner))) {
        } else {
          // Complete native Type is authoritative; unknown resource leaves
          // have no implicit runtime namespace and fail before publication.
          ret = clone_native_owned_value(*result_type, result, *owner, value, nullptr, false);
        }
      } else if (OB_FAIL(ObExpr::clone_native_value(*result_type, result, *allocator, value))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < key.cnt_; ++i) {
        // Retain type metadata in the same cache owner as each cloned value;
        // a runtime Expr may later bind a different effective parameter type.
        if (OB_FAIL(probe_row_.types_[i]->clone_to(*allocator, key.types_[i]))) {
        } else {
          ret = ObExpr::clone_native_value(*key.types_[i], probe_row_.elems_[i],
                                           *allocator, key.elems_[i]);
        }
      }
      if (OB_SUCC(ret)) {
        pending.key_ = key;
        pending.value_ = value;
        pending.result_type_ = result_type;
        pending.ready_ = true;
      }
    }
    // Include allocations made by domain cloners and failed insertions. A
    // single resource clone may exceed the historical cache soft limit.
    memory_used_ = mem_entity_->get_arena_allocator().used();
  }
  return ret;
}

int ObSubQueryIterator::publish_cache_result(PendingCacheEntry &pending)
{
  int ret = OB_SUCCESS;
  if (pending.ready_) {
    CachedResult result;
    result.value_ = pending.value_;
    result.type_ = pending.result_type_;
    ret = hashmap_.set_refactored(pending.key_, result, 1 /* overwrite a changed result type */);
    if (OB_SUCC(ret)) { pending.ready_ = false; }
  }
  return ret;
}

int ObSubQueryIterator::cache_result(const Datum &result, const ObExpr &result_expr)
{
  PendingCacheEntry pending;
  int ret = prepare_cache_result(result, result_expr, pending);
  if (OB_SUCC(ret)) { ret = publish_cache_result(pending); }
  return ret;
}

int ObSubQueryIterator::reset_hash_map()
{
  const int ret = close_cache_values();
  if (ret == OB_SUCCESS) {
    if (mem_entity_ != nullptr) { mem_entity_->reuse(); }
    memory_used_ = 0;
  }
  return ret;
}



ObSubPlanFilterSpec::ObSubPlanFilterSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
  : ObOpSpec(alloc, type),
    rescan_params_(alloc),
    onetime_exprs_(alloc),
    init_plan_idxs_(ModulePageAllocator(alloc)),
    one_time_idxs_(ModulePageAllocator(alloc)),
    update_set_(alloc),
    exec_param_array_(alloc),
    enable_subquery_result_cache_(false),
    enable_px_batch_rescans_(alloc),
    enable_das_group_rescan_(false),
    filter_exprs_(alloc),
    output_exprs_(alloc),
    left_rescan_params_(alloc),
    right_rescan_params_(alloc)
{
}

OB_SERIALIZE_MEMBER((ObSubPlanFilterSpec, ObOpSpec),
                    rescan_params_,
                    onetime_exprs_,
                    init_plan_idxs_,
                    one_time_idxs_,
                    update_set_,
                    exec_param_array_,
                    enable_subquery_result_cache_,
                    enable_px_batch_rescans_,
                    enable_das_group_rescan_,
                    filter_exprs_,
                    output_exprs_,
                    left_rescan_params_,
                    right_rescan_params_);

DEF_TO_STRING(ObSubPlanFilterSpec)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME("op_spec");
  J_COLON();
  pos += ObOpSpec::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(rescan_params),
       K_(onetime_exprs),
       K_(init_plan_idxs),
       K_(one_time_idxs),
       K_(update_set),
       K_(enable_subquery_result_cache));
  J_OBJ_END();
  return pos;
}

ObSubPlanFilterOp::ObSubPlanFilterOp(
    ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
  : ObOperator(exec_ctx, spec, input),
    iter_end_(false),
    max_group_size_(0),
    execution_value_owner_(nullptr),
    update_set_mem_(NULL),
    enable_left_px_batch_(false),
    current_group_(0),
    das_batch_params_(),
    left_rows_("SpfOp"),
    left_rows_iter_(),
    last_store_row_(),
    save_last_row_(false),
    is_left_end_(false),
    left_row_idx_(0),
    batch_rescan_ctl_(),
    last_store_row_mem_(NULL),
    group_rescan_cnt_(0)
{
}

int ObSubPlanFilterOp::retain_scalar_result(const Datum &source, const SqlType &type, Datum &output)
{
  int ret = OB_SUCCESS;
  if (!type.requires_resource_clone()) {
    ret = ObExpr::clone_native_value(type, source, ctx_.get_allocator(), output);
  } else {
    if (execution_value_owner_ == nullptr) {
      void *memory = ctx_.get_allocator().alloc(sizeof(NativeSqlValueOwner));
      if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { execution_value_owner_ = new (memory) NativeSqlValueOwner(ctx_.get_allocator(), ctx_.get_my_session()); }
    }
    if (ret == OB_SUCCESS) {
      ret = clone_native_owned_value(type, source, *execution_value_owner_, output, nullptr, true);
    }
  }
  return ret;
}

int ObSubPlanFilterOp::close_owned_values()
{
  int first_error = OB_SUCCESS;
  FOREACH_CNT(it, subplan_iters_) {
    if (*it != nullptr) {
      const int ret = (*it)->close_cache_values();
      if (first_error == OB_SUCCESS && ret != OB_SUCCESS) { first_error = ret; }
    }
  }
  if (execution_value_owner_ != nullptr) {
    const int ret = execution_value_owner_->enforce_empty_before_destroy();
    if (first_error == OB_SUCCESS && ret != OB_SUCCESS) { first_error = ret; }
  }
  return first_error;
}

void ObSubPlanFilterOp::enforce_empty_before_destroy()
{
  const int ret = close_owned_values();
  if (ret != OB_SUCCESS) {
    LOG_ERROR("SPF resource cleanup failed before arena destroy", K(ret));
    ob_abort();
  }
}

ObSubPlanFilterOp::~ObSubPlanFilterOp()
{
  destroy_subplan_iters();
  destroy_update_set_mem();
  destroy_px_batch_rescan_status();
}

void ObSubPlanFilterOp::destroy_subplan_iters()
{
  enforce_empty_before_destroy();
  FOREACH_CNT(it, subplan_iters_) {
    if (NULL != *it) {
      (*it)->~Iterator();
      *it = NULL;
    }
  }
  subplan_iters_.reset();
  if (execution_value_owner_ != nullptr) {
    execution_value_owner_->~NativeSqlValueOwner();
    ctx_.get_allocator().free(execution_value_owner_);
    execution_value_owner_ = nullptr;
  }
}

void ObSubPlanFilterOp::destroy_px_batch_rescan_status()
{
  left_rows_iter_.reset();
  last_store_row_.reset();
  left_rows_.reset();
  batch_rescan_ctl_.reset();
}

void ObSubPlanFilterOp::destroy()
{
  destroy_subplan_iters();
  destroy_update_set_mem();
  destroy_px_batch_rescan_status();
  ObOperator::destroy();
}



//SPF has its own rescan
int ObSubPlanFilterOp::rescan()
{
  int ret = OB_SUCCESS;
  brs_.end_ = false;
  iter_end_ = false;
  clear_evaluated_flag();
  set_param_null();
  if (OB_FAIL(ObOperator::inner_rescan())) {
  }

  if (OB_SUCC(ret) &&
      (MY_SPEC.enable_das_group_rescan_ || enable_left_px_batch_)) {
    left_rows_.reset();
    left_rows_iter_.reset();
    is_left_end_ = false;
    save_last_row_ = false;
    last_store_row_.reset();
  }

  if (OB_SUCC(ret) && MY_SPEC.enable_das_group_rescan_) {
    //We do not need alloc memory again in rescan.
    //das_batch_params_.reset();
    current_group_ = 0;
    brs_holder_.reset();
  }

  if (OB_SUCC(ret) && enable_left_px_batch_) {
    batch_rescan_ctl_.reuse();
    brs_holder_.reset();
  }

  if (!MY_SPEC.enable_das_group_rescan_) {
    // call each child's rescan when not batch rescan
    for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
      if (OB_FAIL(children_[i]->rescan())) {
      }
    }
  } else {
    for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
      if (MY_SPEC.init_plan_idxs_.has_member(i) || MY_SPEC.one_time_idxs_.has_member(i)) {
        // rescan for init plan and onetime expr when batch rescan
        if (OB_FAIL(children_[i]->rescan())) {
        }
      }
    }
  }
  for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
    Iterator *iter = subplan_iters_.at(i - 1);
    if (OB_ISNULL(iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (MY_SPEC.init_plan_idxs_.has_member(i)) {
      iter->reuse();
      if (OB_FAIL(iter->prepare_init_plan())) {
      }
    } else if (OB_FAIL(iter->reset_hash_map())) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(prepare_onetime_exprs())) {
    } else if (OB_FAIL(child_->rescan())) {
    } else {
      startup_passed_ = spec_.startup_filters_.empty();
    }
  }
  need_init_before_get_row_ = false;
#ifndef NDEBUG
  OX(OB_ASSERT(false == brs_.end_));
#endif

  return ret;
}
//SPF has its own switch iterator

int ObSubPlanFilterOp::fill_cur_row_rescan_param()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = ctx_.get_physical_plan_ctx();
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_BAD_NULL_ERROR;
  } else if (batch_rescan_ctl_.cur_idx_ >= batch_rescan_ctl_.params_.get_count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObBatchRescanParams::Row &params =
        batch_rescan_ctl_.params_.get_one_batch_values(batch_rescan_ctl_.cur_idx_);
    const auto &attributes = batch_rescan_ctl_.params_.get_one_batch_metadata(batch_rescan_ctl_.cur_idx_);
    int64_t param_cnt = params.count();
    int64_t idx = OB_INVALID_ID;
    for (int64_t i = 0; OB_SUCC(ret) && i < param_cnt; ++i) {
      idx = batch_rescan_ctl_.params_.get_param_idx(i);
      if (OB_UNLIKELY(i >= MY_SPEC.rescan_params_.count() || MY_SPEC.rescan_params_.at(i).param_idx_ != idx)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = MY_SPEC.rescan_params_.at(i).restore_dynamic_param(eval_ctx_, params.at(i), attributes.at(i));
      }
    }
  }
  return ret;
}

int ObSubPlanFilterOp::switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_switch_iterator())) {
  } else if (OB_FAIL(child_->switch_iterator())) {
    //TODO: Currently only supports multi-group iterator switch for non-correlated subqueries, only switches the main table
    if (OB_ITER_END != ret) {
    }
  }

#ifndef NDEBUG
  OX(OB_ASSERT(false == brs_.end_));
#endif

  return ret;
}

int ObSubPlanFilterOp::inner_open()
{
  int ret = OB_SUCCESS;
  CK(child_cnt_ >= 2);
  CK(child_cnt_ == MY_SPEC.enable_px_batch_rescans_.count() ||
     0 == MY_SPEC.enable_px_batch_rescans_.count());
  if (OB_SUCC(ret)) {
    // In subplan filter, the first child is the main table for external output, the following children are all subqueries,
    // The result of subquery needs to participate in expression calculation, so generate a row_iterator for each subquery
    OZ(subplan_iters_.prepare_allocate(child_cnt_ - 1));
    //TODO move to the back
    if (child_cnt_ - 1 != MY_SPEC.exec_param_array_.count()) {
      ret = OB_ERR_UNEXPECTED;
    }
    for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
      void *ptr = ctx_.get_allocator().alloc(sizeof(Iterator));
      Iterator *&iter = subplan_iters_.at(i - 1);
      if (OB_ISNULL(ptr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc subplan iterator failed", K(ret), "size", sizeof(Iterator));
      } else {
        iter = new(ptr) Iterator(*children_[i]);
        iter->set_iter_id(i);
        iter->set_parent(this);
        if (MY_SPEC.init_plan_idxs_.has_member(i)) {
          iter->set_init_plan();
          //init plan move to get_next_rowafter
        } else if (MY_SPEC.one_time_idxs_.has_member(i)) {
          iter->set_onetime_plan();
        } else if (!MY_SPEC.enable_px_batch_rescans_.empty() &&
            MY_SPEC.enable_px_batch_rescans_.at(i)) {
          enable_left_px_batch_ = true;
        }
        if (!MY_SPEC.enable_subquery_result_cache_) {
          // Non-deterministic subqueries bypass the parameter-result cache.
        } else if (OB_FAIL(iter->init_mem_entity())) {
        } else if (MY_SPEC.exec_param_array_[i - 1].count() > 0) {
          //min of buckets is 16,
          //max will not exceed card of left_child and HASH_MAP_MEMORY_LIMIT/sizeof(Datum)
          if (OB_FAIL(iter->init_hashmap(max(
                  16/*hard code*/, min(get_child(0)->get_spec().get_rows(),
                      iter->HASH_MAP_MEMORY_LIMIT / static_cast<int64_t>(sizeof(Datum))))))) {
          } else if (OB_FAIL(iter->init_probe_row(MY_SPEC.exec_param_array_[i - 1].count()))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (children_[i]->is_vectorized() && OB_FAIL(iter->init_batch_rows_holder(children_[i]->get_spec().output_, children_[i]->get_eval_ctx()))) {
          }
        }
      }
    }
  }

  //BATCH SUBPLAN FILTER {
  if (OB_SUCC(ret) && MY_SPEC.enable_das_group_rescan_) {
    int64_t simulate_group_size = - EVENT_CALL(EventTable::EN_DAS_SIMULATE_GROUP_SIZE);
    max_group_size_ = simulate_group_size > 0 ? simulate_group_size: OB_MAX_BULK_JOIN_ROWS;
    if(OB_FAIL(alloc_das_batch_params(max_group_size_+MY_SPEC.max_batch_size_))) {
    }
  }
  //} BATCH SUBPLAN FILTER END
  //left_rows used by px_batch and das batch.
  if (OB_SUCC(ret) &&
      (enable_left_px_batch_ || MY_SPEC.enable_das_group_rescan_) &&
      OB_ISNULL(last_store_row_mem_)) {
    ObSQLSessionInfo *session = ctx_.get_my_session();
    
    lib::ContextParam param;
    param.set_mem_attr("ObSBFCache",
                       ObCtxIds::WORK_AREA)
      .set_properties(lib::USE_TL_PAGE_OPTIONAL);
    if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(last_store_row_mem_, param))) {
    } else if (OB_ISNULL(last_store_row_mem_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(left_rows_.init(MAX_DUMP_SIZE, ObCtxIds::WORK_AREA))) {
    } else if (OB_FAIL(left_rows_.alloc_dir_id())) {
    } else {
      left_rows_.set_allocator(last_store_row_mem_->get_malloc_allocator());
    }
  }
  if (OB_SUCC(ret) && is_vectorized()) {
    if (OB_FAIL(brs_holder_.init(child_->get_spec().output_, eval_ctx_))) {
    }
  }
  return ret;
}

int ObSubPlanFilterOp::inner_close()
{
  const int ret = close_owned_values();
  if (ret != OB_SUCCESS) { return ret; }
  destroy_subplan_iters();
  destroy_update_set_mem();
  if (MY_SPEC.enable_das_group_rescan_) {
    das_batch_params_.reset();
  }
  return OB_SUCCESS;
}

int ObSubPlanFilterOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;

  if (OB_SUCC(ret)) {
    if (OB_FAIL(handle_next_row())) {
    }
  }

  if (OB_SUCC(ret)) {
    if (!MY_SPEC.update_set_.empty()) {
      OZ(handle_update_set());
    }
  }
  if (OB_ITER_END == ret) {
    set_param_null();
  }
  return ret;
}
int ObSubPlanFilterOp::handle_next_row()
{
  int ret = OB_SUCCESS;
  int64_t params_size = 0;
  if (need_init_before_get_row_) {
    OZ(prepare_onetime_exprs());
  }
  if (OB_FAIL(ret)) {
  } else if (enable_left_px_batch_ || MY_SPEC.enable_das_group_rescan_) {
    //DAS batch spf is conflict with PX batch spf
    OB_ASSERT(!(enable_left_px_batch_ && MY_SPEC.enable_das_group_rescan_));
    bool has_row = false;
    int batch_count = 0;
    batch_count = MY_SPEC.enable_das_group_rescan_ ? max_group_size_ : PX_RESCAN_BATCH_ROW_COUNT;
    if (left_rows_iter_.is_valid() && left_rows_iter_.has_next()) {
      if(MY_SPEC.enable_das_group_rescan_) {
        //das batch branch
        //Consume the remaining batch data in left store.
        current_group_++;
      } else {
        OB_ASSERT(enable_left_px_batch_);
        //px batch branch
        batch_rescan_ctl_.cur_idx_++;
      }
    } else if (is_left_end_) {
      ret = OB_ITER_END;
    } else {
      //Accumulate a new batch into left store.
      if(enable_left_px_batch_) {
        batch_rescan_ctl_.reuse();
      }
      if (MY_SPEC.enable_das_group_rescan_) {
        current_group_ = 0;
        //Always OB_SUCCESS in current implement.
        if(OB_FAIL(init_das_batch_params())) {
        }
      }
      left_rows_iter_.reset();
      left_rows_.reset();
      last_store_row_mem_->get_arena_allocator().reset();
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(last_store_row_.get_store_row())) {
        if (save_last_row_) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(last_store_row_.init(
            last_store_row_mem_->get_malloc_allocator(), child_->get_spec().output_.count()))) {
        }
      } else if (save_last_row_) {
          // restore expr datum to original value
          if (OB_FAIL(last_store_row_.restore(child_->get_spec().output_, eval_ctx_))) {
        }
      }
      save_last_row_ = false;
      set_param_null();
      while (OB_SUCC(ret) && batch_count--) {
        clear_evaluated_flag();
        set_param_null();
        if (OB_FAIL(child_->get_next_row())) {
          if (OB_ITER_END != ret) {
          } else {
            is_left_end_ = true;
          }
        } else if (OB_FAIL(left_rows_.add_row(child_->get_spec().output_, &eval_ctx_))) {
        } else if (enable_left_px_batch_ && OB_FAIL(prepare_rescan_params(true, params_size))) {
        } else if (MY_SPEC.enable_das_group_rescan_ && OB_FAIL(deep_copy_dynamic_obj())) {
        } else {
          has_row = true;
        }

        if (enable_left_px_batch_ && params_size >= MAX_PX_RESCAN_PARAMS_SIZE) {
          break;
        }
      }
      if (OB_SUCC(ret)) {
        // back expr datum to last_store_row
        if (OB_FAIL(last_store_row_.shadow_copy(child_->get_spec().output_, eval_ctx_))) {
        } else {
          save_last_row_ = true;
        }
      }

      if (OB_SUCC(ret) || (has_row && OB_ITER_END == ret)) {
        ret = OB_SUCCESS;
        OZ(left_rows_.finish_add_row(false));
        OZ(left_rows_.begin(left_rows_iter_));
        if (MY_SPEC.enable_das_group_rescan_) {
          group_rescan_cnt_++;
        }
      }
    }
    //After accumulate a new batch or previous have remaining row.
    if (OB_SUCC(ret)) {
      clear_evaluated_flag();
      // fetch datum from left_row_iter_ instead of child operator
      if (OB_FAIL(left_rows_iter_.get_next_row(child_->get_spec().output_, eval_ctx_))) {
      } else if (enable_left_px_batch_) {
        //px batch spf branch
        OZ(fill_cur_row_rescan_param());
      } else {
        //das batch spf branch
        OB_ASSERT(MY_SPEC.enable_das_group_rescan_);
        if (OB_FAIL(fill_cur_row_das_batch_param(eval_ctx_, current_group_))) {
        }
      }
    }
  } else if (FALSE_IT(clear_evaluated_flag())) {
  } else if (FALSE_IT(set_param_null())) {
  } else if (OB_FAIL(child_->get_next_row())) {
    if (OB_ITER_END != ret) {
    }
  } else if (OB_FAIL(prepare_rescan_params(false, params_size))) {
  }

  if (OB_SUCC(ret) && need_init_before_get_row_) {
    for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
      Iterator *&iter = subplan_iters_.at(i - 1);
      if (MY_SPEC.init_plan_idxs_.has_member(i)) {
        OZ(iter->prepare_init_plan());
      }
    }
    need_init_before_get_row_ = false;
  }
  return ret;
}

int ObSubPlanFilterOp::handle_next_batch_with_px_rescan(const int64_t op_max_batch_size)
{
  int ret = OB_SUCCESS;
  const ObBatchRows *child_brs = NULL;
  bool stop_fetch = false;
  ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
  uint64_t left_rows_total_cnt = 0;
  int64_t params_size = 0;
  if (left_rows_iter_.is_valid() && left_rows_iter_.has_next()) {
    // fetch data from left store
  } else {
    // 1. material data from child into left_rows_
    // 2. prepare batch rescan params
    batch_rescan_ctl_.reuse();
    left_rows_.reset();
    left_rows_iter_.reset();
    (void) brs_holder_.restore();
    while (OB_SUCC(ret) && continue_fetching(left_rows_total_cnt, stop_fetch)) {
      set_param_null();
      clear_evaluated_flag();
      int64_t store_row_cnt = -1;
      if (OB_FAIL(child_->get_next_batch(op_max_batch_size, child_brs))) {
      } else if (OB_FAIL(left_rows_.add_batch(child_->get_spec().output_, eval_ctx_,
                          *child_brs->skip_, child_brs->size_, store_row_cnt))) {
      } else {
        stop_fetch = child_brs->end_;
        left_rows_total_cnt += store_row_cnt;
        guard.set_batch_size(child_brs->size_);
        clear_evaluated_flag();
        // prepare px batch rescan parameter
        for (int64_t l_idx = 0; OB_SUCC(ret) && l_idx < child_brs->size_; l_idx++) {
          if (child_brs->skip_->exist(l_idx)) { continue; }
          guard.set_batch_idx(l_idx);
          if (OB_FAIL(prepare_rescan_params(true, params_size))) {
          }
        }
      }

      if (params_size >= MAX_PX_RESCAN_PARAMS_SIZE) {
        break;
      }
    }
    if (OB_SUCC(ret)) {
      if (!child_brs->end_) {
        // backup child datums into brs_holder_
        OZ(brs_holder_.save(MY_SPEC.max_batch_size_));
      }

      if (OB_FAIL(left_rows_.finish_add_row(false))) {
      } else if (OB_FAIL(left_rows_.begin(left_rows_iter_))) {
      }
      if (left_rows_total_cnt != left_rows_.get_row_cnt()) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }

  // fetch data from masterized left_rows(ChunkDatumStore) and do filtering
  if (OB_SUCC(ret)) {
    int64_t rows_fetched = 0;
    clear_evaluated_flag();
    if (OB_FAIL(left_rows_iter_.get_next_batch(child_->get_spec().output_,
               eval_ctx_, op_max_batch_size, rows_fetched))) {
      if (OB_ITER_END == ret) {
        brs_.size_ = rows_fetched;
        brs_.end_ = true;
        iter_end_ = true;
        OB_ASSERT(0 == brs_.size_);
        OB_ASSERT(0 == left_rows_total_cnt);
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      // Note:  rows are fetched from left_rows(ChunkDatumStore), so there is no
      //        no skip row.
      //        Do not change brs_.skip_
      brs_.size_ = rows_fetched;
      left_rows_total_cnt -= rows_fetched; // debug only
      guard.set_batch_size(brs_.size_);
      for (int64_t l_idx = 0; OB_SUCC(ret) && l_idx < brs_.size_; l_idx++) {
        guard.set_batch_idx(l_idx);
        if (OB_FAIL(fill_cur_row_rescan_param())) {
        } else {
          if (need_init_before_get_row_) {
            for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
              Iterator *&iter = subplan_iters_.at(i - 1);
              if (MY_SPEC.init_plan_idxs_.has_member(i)) {
                OZ(iter->prepare_init_plan());
              }
            }
            need_init_before_get_row_ = false;
          }
        }
        if (OB_SUCC(ret))  {
          bool filtered = false;
          if (OB_FAIL(filter_row(eval_ctx_, MY_SPEC.filter_exprs_, filtered))) {
          } else if (filtered) {
            brs_.skip_->set(l_idx);
          } else {
            Datum *datum = NULL;
            FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
              if (OB_FAIL((*e)->eval(eval_ctx_, datum))) {
              }
            }
          }
          batch_rescan_ctl_.cur_idx_++;
        }
      } // for end
    }
  }
  FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
    (*e)->get_eval_info(eval_ctx_).projected_ = true;
  }

  return ret;
}

int ObSubPlanFilterOp::handle_next_batch_with_group_rescan(const int64_t op_max_batch_size)
{
  int ret = OB_SUCCESS;
  const ObBatchRows *child_brs = NULL;
  bool stop_fetch = false;
  ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
  uint64_t left_rows_total_cnt = 0;
  DASGroupScanMarkGuard mark_guard(ctx_.get_das_ctx(), true);
  if (left_rows_iter_.is_valid() && left_rows_iter_.has_next()) {
    // fetch data from left store
  } else {
    // 1. material data from child into left_rows_
    // 2. prepare batch rescan params
    left_rows_.reset();
    left_rows_iter_.reset();
    (void) brs_holder_.restore();
    current_group_ = 0;
    if (OB_FAIL(init_das_batch_params())) {
    } else { last_store_row_mem_->get_arena_allocator().reset(); }
    while (OB_SUCC(ret) && continue_fetching(left_rows_total_cnt, stop_fetch, true)) {
      set_param_null();
      clear_evaluated_flag();
      int64_t store_row_cnt = -1;
      if (OB_FAIL(child_->get_next_batch(op_max_batch_size, child_brs))) {
      } else if (OB_FAIL(left_rows_.add_batch(child_->get_spec().output_, eval_ctx_,
                          *child_brs->skip_, child_brs->size_, store_row_cnt))) {
      } else {
        stop_fetch = child_brs->end_;
        left_rows_total_cnt += store_row_cnt;
        guard.set_batch_size(child_brs->size_);
        clear_evaluated_flag();
        // prepare group batch rescan parameter
        for (int64_t l_idx = 0; OB_SUCC(ret) && l_idx < child_brs->size_; l_idx++) {
          if (child_brs->skip_->exist(l_idx)) { continue; }
          guard.set_batch_idx(l_idx);
          if (OB_FAIL(deep_copy_dynamic_obj())) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!child_brs->end_) {
        // backup child datums into brs_holder_
        OZ(brs_holder_.save(MY_SPEC.max_batch_size_));
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(left_rows_.finish_add_row(false))) {
        } else if (OB_FAIL(left_rows_.begin(left_rows_iter_))) {
        } else if (left_rows_total_cnt != left_rows_.get_row_cnt()) {
          ret = OB_ERR_UNEXPECTED;
        }
      }

      if (OB_SUCC(ret)) {
        group_rescan_cnt_++;
      }
    }
  }

  // fetch data from masterized left_rows(ChunkDatumStore) and do filtering
  if (OB_SUCC(ret)) {
    int64_t rows_fetched = 0;
    clear_evaluated_flag();
    if (OB_FAIL(left_rows_iter_.get_next_batch(child_->get_spec().output_,
               eval_ctx_, op_max_batch_size, rows_fetched))) {
      if (OB_ITER_END == ret) {
        brs_.size_ = rows_fetched;
        brs_.end_ = true;
        iter_end_ = true;
        OB_ASSERT(0 == brs_.size_);
        OB_ASSERT(0 == left_rows_total_cnt);
        ret = OB_SUCCESS;
      } else {
      }
    } else {
      // Note:  rows are fetched from left_rows(ChunkDatumStore), so there is no
      //        skip row.
      //        Do not change brs_.skip_
      brs_.size_ = rows_fetched;
      left_rows_total_cnt -= rows_fetched; // debug only
      guard.set_batch_size(brs_.size_);
      for (int64_t l_idx = 0; OB_SUCC(ret) && l_idx < brs_.size_; l_idx++) {
        guard.set_batch_idx(l_idx);
        if (OB_FAIL(fill_cur_row_das_batch_param(eval_ctx_, current_group_))) {
        } else {
          if (need_init_before_get_row_) {
            for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
              Iterator *&iter = subplan_iters_.at(i - 1);
              if (MY_SPEC.init_plan_idxs_.has_member(i)) {
                OZ(iter->prepare_init_plan());
              }
            }
            need_init_before_get_row_ = false;
          }
        }
        if (OB_SUCC(ret))  {
          bool filtered = false;
          if (OB_FAIL(filter_row(eval_ctx_, MY_SPEC.filter_exprs_, filtered))) {
          } else if (filtered) {
            brs_.skip_->set(l_idx);
          } else {
            Datum *datum = NULL;
            FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
              if (OB_FAIL((*e)->eval(eval_ctx_, datum))) {
              }
            }
          }
        }
        current_group_++;
      } // for end
    }
  }
  FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
    (*e)->get_eval_info(eval_ctx_).projected_ = true;
  }

  return ret;
}


int ObSubPlanFilterOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  int64_t op_max_batch_size = min(max_row_cnt, MY_SPEC.max_batch_size_);
  int64_t params_size = 0;
  if (iter_end_) {
    brs_.size_ = 0;
    brs_.end_ = true;
  } else if (need_init_before_get_row_) {
    OZ(prepare_onetime_exprs());
  }
  // Get a row of data from the main table
  clear_evaluated_flag();
  if(OB_FAIL(ret)) {
  } else if (MY_SPEC.enable_das_group_rescan_) {
    if (OB_FAIL(handle_next_batch_with_group_rescan(op_max_batch_size))) {
    }
  } else if (enable_left_px_batch_) {
    if (OB_FAIL(handle_next_batch_with_px_rescan(op_max_batch_size))) {
    }
  } else {
    if (iter_end_) {
      brs_.size_ = 0;
      brs_.end_ = true;
    }
    while (OB_SUCC(ret) && !iter_end_) {
      const ObBatchRows *child_brs = NULL;
      set_param_null();
      if (OB_FAIL(child_->get_next_batch(op_max_batch_size, child_brs))) {
      } else if (child_brs->end_) {
        iter_end_ = true;
      }

      if (OB_SUCC(ret)) {
        ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
        guard.set_batch_size(child_brs->size_);
        brs_.size_ = child_brs->size_;
        bool all_filtered = true;
        brs_.skip_->deep_copy(*child_brs->skip_, child_brs->size_);
        clear_evaluated_flag();

        for (int64_t l_idx = 0; OB_SUCC(ret) && l_idx < child_brs->size_; l_idx++) {
          if (child_brs->skip_->exist(l_idx)) { continue; }
          guard.set_batch_idx(l_idx);
          if (OB_FAIL(prepare_rescan_params(false, params_size))) {
          } else {
            if (need_init_before_get_row_) {
              for (int32_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
                Iterator *&iter = subplan_iters_.at(i - 1);
                if (MY_SPEC.init_plan_idxs_.has_member(i)) {
                  OZ(iter->prepare_init_plan());
                }
              }
              need_init_before_get_row_ = false;
            }
          }
          if (OB_SUCC(ret))  {
            bool filtered = false;
            if (OB_FAIL(filter_row(eval_ctx_, MY_SPEC.filter_exprs_, filtered))) {
            } else if (filtered) {
              brs_.skip_->set(l_idx);
            } else {
              all_filtered = false;
              Datum *datum = NULL;
              FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
                if (OB_FAIL((*e)->eval(eval_ctx_, datum))) {
                }
              }
            }
          }
        } // for end
        if (OB_SUCC(ret) && all_filtered) {
          reset_batchrows();
          continue;
        }
        FOREACH_CNT_X(e, spec_.output_, OB_SUCC(ret)) {
          (*e)->get_eval_info(eval_ctx_).projected_ = true;
        }
        break;
      }
    }
  }

  if (OB_SUCC(ret) && iter_end_) {
    set_param_null();
  }
  return ret;
}

int ObSubPlanFilterOp::prepare_rescan_params(bool need_save, int64_t& params_size)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  CK(OB_NOT_NULL(plan_ctx));
  if (!need_save) {
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.rescan_params_.count(); ++i) {
      ret = MY_SPEC.rescan_params_.at(i).set_dynamic_param(eval_ctx_);
    }
  } else {
    ObBatchRescanParams::RowBuilder builder(batch_rescan_ctl_.params_);
    common::ObSEArray<int64_t, 8> param_idxs, param_expr_idxs;
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.rescan_params_.count(); ++i) {
      const ObDynamicParamSetter &setter = MY_SPEC.rescan_params_.at(i);
      NativeTypedDatum value;
      DatumParameterMetadata attributes;
      int64_t expr_idx = 0;
      OZ(setter.set_dynamic_param(eval_ctx_, value, attributes));
      OZ(builder.append(value, attributes));
      if (OB_SUCC(ret)) {
        int64_t bytes = 0;
        OZ(DatumCopyPlan(*value.type_).carrier_span_size(value.datum_, bytes));
        if (OB_SUCC(ret)) { params_size += bytes; }
      }
      OZ(param_idxs.push_back(setter.param_idx_));
      CK(OB_NOT_NULL(plan_ctx->get_phy_plan()));
      OZ(plan_ctx->get_phy_plan()->get_expr_frame_info().get_expr_idx_in_frame(setter.dst_, expr_idx));
      OZ(param_expr_idxs.push_back(expr_idx));
    }
    if (OB_SUCC(ret)) {
      ret = builder.finish(param_idxs, param_expr_idxs);
      if (OB_SUCC(ret)) { batch_rescan_ctl_.param_version_ += 1; }
    }
  }
  return ret;
}

int ObSubPlanFilterOp::prepare_onetime_exprs()
{
  int ret = OB_SUCCESS;
  if (is_vectorized()) {
    ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
    guard.set_batch_size(MY_SPEC.max_batch_size_);
    ret = prepare_onetime_exprs_inner();
  } else {
    // TODO qubin.qb: make eval_ctx_ NOT shared among operators
    // Child operator would set global eval_ctx.batch_size_ to 1 if its
    // parent is non-vectorized, however, onetime expr is calculated ahead of
    // fetching child rows. which leaves its child no chance to to explicitly
    // set eval_ctx.batch_size_ Therefore, set batchsize_ 1 explicitly no matter
    // is vectorization is enabled.
    eval_ctx_.set_batch_size(1);
    eval_ctx_.set_batch_idx(0);
    ret = prepare_onetime_exprs_inner();
  }
  /*
     SPF
      TSC
      PX COORD
        EXCHANGE (thread +1)
    
    if PX COORD is onetime expr, SPF use extra thread calc it and release it until ITER_END，
    A large number of threads are wasted when multiple onetime expr coexist
    So we drain every onetime expr after calc
  */
  for (int64_t i = 1; OB_SUCC(ret) && i < child_cnt_; ++i) {
    Iterator *iter = subplan_iters_.at(i - 1);
    if (OB_ISNULL(iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (MY_SPEC.one_time_idxs_.has_member(i)) {
      iter->drain_exch();
    }
  }
  return ret;
}

int ObSubPlanFilterOp::prepare_onetime_exprs_inner()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.onetime_exprs_.count(); ++i) {
    const ObDynamicParamSetter &setter = MY_SPEC.onetime_exprs_.at(i);
    Datum *datum = NULL;
    if (OB_FAIL(setter.src_->eval(eval_ctx_, datum))) {
    } else if (OB_FAIL(setter.update_dynamic_param(eval_ctx_, *datum))) {
    }
  }

  return ret;
}

int ObSubPlanFilterOp::handle_update_set()
{
  int ret = OB_SUCCESS;
  const int64_t extra_size = 0;
  if (NULL == update_set_mem_) {
    lib::ContextParam param;
    param.set_mem_attr("SubplanFilterOp", ObCtxIds::WORK_AREA)
      .set_properties(lib::USE_TL_PAGE_OPTIONAL);
    if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(update_set_mem_, param))) {
    }
  } else {
    update_set_mem_->get_arena_allocator().reuse();
  }

  if (OB_SUCC(ret)) {
    ObChunkDatumStore::LastStoredRow row_val(update_set_mem_->get_arena_allocator());
    Iterator *iter = NULL;
    subplan_iters_to_check_.reset();
    int64_t update_set_pos = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < subplan_iters_.count(); ++i) {
      if (OB_ISNULL(iter = subplan_iters_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(iter->rewind())) {
      } else if (OB_SUCC(iter->get_next_row())) {
        if (OB_FAIL(subplan_iters_to_check_.push_back(iter))) {
        } else {
          update_set_pos += iter->get_output().count();
        }
      } else if (OB_UNLIKELY(OB_ITER_END != ret)) {
      } else {  // set null for no row subplan iterator
        ret = OB_SUCCESS;
        int64_t j = update_set_pos;
        ObExpr *expr = NULL;
        update_set_pos += iter->get_output().count();
        if (OB_UNLIKELY(update_set_pos > MY_SPEC.update_set_.count())) {
          ret = OB_ERR_UNEXPECTED;
        }
        for (; OB_SUCC(ret) && j < update_set_pos; ++j) {
          if (OB_ISNULL(expr = MY_SPEC.update_set_.at(j))) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            expr->locate_expr_datum(eval_ctx_).set_null();
            expr->set_evaluated_projected(eval_ctx_);
          }
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(MY_SPEC.update_set_.count() != update_set_pos)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(row_val.save_store_row(MY_SPEC.update_set_, eval_ctx_, extra_size))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < subplan_iters_to_check_.count(); ++i) {
        if (OB_UNLIKELY(OB_SUCCESS == (ret = subplan_iters_to_check_.at(i)->get_next_row()))) {
          ret = OB_ERR_MORE_THAN_ONE_ROW;
        } else if (OB_UNLIKELY(OB_ITER_END != ret)) {
        } else {
          ret = OB_SUCCESS;
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(row_val.store_row_->to_expr(MY_SPEC.update_set_, eval_ctx_))) {
      }
    }
  }

  return ret;
}

int ObSubPlanFilterOp::alloc_das_batch_params(uint64_t group_size)
{
  int ret = OB_SUCCESS;
  if (das_batch_params_.empty()) {
    GroupParamArray prepared;
    if (OB_FAIL(prepared.allocate_array(ctx_.get_allocator(), MY_SPEC.rescan_params_.count()))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < prepared.count(); ++i) {
        ret = prepared.at(i).init(MY_SPEC.rescan_params_.at(i).param_idx_, group_size, ctx_.get_allocator());
      }
    }
    if (OB_SUCC(ret)) { das_batch_params_ = prepared; }
  }
  return ret;
}

int ObSubPlanFilterOp::init_das_batch_params()
{
  OB_ASSERT(!das_batch_params_.empty());
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < das_batch_params_.count(); ++i) {
    auto &column = das_batch_params_.at(i);
    for (int64_t j = 0; j < column.values_.count(); ++j) {
      column.values_.at(j) = NativeTypedDatum(); column.metadata_.at(j) = DatumParameterMetadata();
    }
    column.count_ = 0;
  }
  return ret;
}

int ObSubPlanFilterOp::deep_copy_dynamic_obj()
{
  int ret = OB_SUCCESS;
  const int64_t count = MY_SPEC.rescan_params_.count();
  if (OB_ISNULL(last_store_row_mem_) || count != das_batch_params_.count()) { ret = OB_INVALID_ARGUMENT; }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    NativeTypedDatum value;
    DatumParameterMetadata attributes;
    if (das_batch_params_.at(i).count_ != das_batch_params_.at(0).count_) { ret = OB_ERR_UNEXPECTED; }
    else if (OB_FAIL(MY_SPEC.rescan_params_.at(i).set_dynamic_param(eval_ctx_, value, attributes))) {
    } else { ret = das_batch_params_.at(i).stage(value, attributes, last_store_row_mem_->get_arena_allocator()); }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < count; ++i) { ++das_batch_params_.at(i).count_; }
  }
  return ret;
}

int ObSubPlanFilterOp::fill_cur_row_das_batch_param(ObEvalCtx &eval_ctx, uint64_t current_group) const
{
  int ret = OB_SUCCESS;
  if (das_batch_params_.empty() || MY_SPEC.rescan_params_.count() != das_batch_params_.count()) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < das_batch_params_.count(); ++i) {
    const GroupRescanParam &column = das_batch_params_.at(i);
    if (current_group >= column.count_) { ret = OB_ERR_UNEXPECTED; }
    else { ret = MY_SPEC.rescan_params_.at(i).restore_dynamic_param(
        eval_ctx, column.values_.at(current_group), column.metadata_.at(current_group)); }
  }
  return ret;
}

void ObSubPlanFilterOp::get_current_group(uint64_t& current_group) const
{
  current_group = current_group_;
}

} // end namespace sql
} // end namespace oceanbase
