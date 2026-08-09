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
#include "data_plane/lob/ob_lob_access_context.h"
#include "query/engine/ob_exec_context_access.h"
#include "query/session/ob_session_access.h"
#include "ob_exec_context.h"
#include "share/datum/ob_datum_funcs.h"
#include "share/ob_lob_access_utils.h"
#include "share/ob_server_struct.h"
#include "sql/engine/px/ob_px_util.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/table/ob_i_virtual_table_iterator_factory.h"
#include "query/virtual_table/ob_virtual_table_factory_provider.h"
#include "sql/executor/ob_memory_tracker.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{

OB_SERIALIZE_MEMBER(GroupPWJTabletIdInfo, group_id_, tablet_id_array_);

query::ObIRootCommandService *ObExecContext::get_root_command_service() const
{
  return root_command_service_;
}

query::ObILocalCommandService *ObExecContext::get_local_command_service() const
{
  return local_command_service_;
}

query::ObIRootCommandService &ObExecContext::root_command_service() const
{
  query::ObIRootCommandService *service = get_root_command_service();
  OB_ASSERT_MSG(nullptr != service, "root command service is not bound to SQL session");
  return *service;
}

query::ObILocalCommandService &ObExecContext::local_command_service() const
{
  query::ObILocalCommandService *service = get_local_command_service();
  OB_ASSERT_MSG(nullptr != service, "local command service is not bound to SQL session");
  return *service;
}

const ObPartIdRowMapManager::ObRowIdList *ObPartIdRowMapManager::get_row_id_list(int64_t part_index)
{
  const ObRowIdList *ret = NULL;
  // Linear search is sufficient for the current small partition list.
  if (part_index >= 0 && part_index < manager_.count()) {
    ret = &(manager_.at(part_index).list_);
  }
  return ret;
}

int ObPartIdRowMapManager::MapEntry::assign(const MapEntry &other)
{
  int ret = OB_SUCCESS;
  if (this != &other && OB_FAIL(list_.assign(other.list_))) {
    LOG_WARN("copy list failed", K(ret));
  }
  return ret;
}

int ObOpKitStore::init(ObIAllocator &alloc, const int64_t size)
{
  int ret = OB_SUCCESS;
  if (size < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(size));
  } else if (NULL == (kits_ = static_cast<ObOperatorKit *>(
              alloc.alloc(size * sizeof(kits_[0]))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  } else {
    memset(kits_, 0, size * sizeof(kits_[0]));
    size_ = size;
  }
  return ret;
}

void ObOpKitStore::destroy()
{
  if (NULL != kits_) {
    for (int64_t i = 0; i < size_; i++) {
      ObOperatorKit &kit = kits_[i];
      if (NULL != kit.op_) {
        kit.op_->destroy();
      }
      if (NULL != kit.input_) {
        kit.input_->~ObOpInput();
      }
    }
  }
}

int ObDiagnosisManager::add_warning_info(int err_ret, int line_idx) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(rets_.push_back(err_ret))) {
  } else if (OB_FAIL(idxs_.push_back(line_idx))) {
  }
  return ret;
}

int ObDiagnosisManager::do_diagnosis(ObBitVector &skip, int64_t limit_num) {
  int ret = OB_SUCCESS;

  if (idxs_.count() != rets_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("idxs_ and rets_ count mismatch", K(ret), K(idxs_.count()), K(rets_.count()));
  } else if (idxs_.count() > 0) {
    if (cur_file_url_.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("missing cur_file_url", K(ret));
    } else {
      ObWarningBuffer *buffer = ob_get_tsi_warning_buffer();

      bool has_col_info = idxs_.count() == col_names_.count();

      for (int i = 0; OB_SUCC(ret) && i < idxs_.count(); i++) {
        int64_t idx = idxs_.at(i);
        int64_t err_ret = rets_.at(i);
        ObSqlString err_msg;

        if (skip.at(idx)) {
          continue;
        }
        
        if (has_col_info) {
          ObString cur_col_name = col_names_.at(i);
          if (OB_FAIL(err_msg.append_fmt("fail to scan file %.*s at line %ld for column %.*s, error: %s",
                                                        cur_file_url_.length(), cur_file_url_.ptr(),
                                                        idx + cur_line_number_,
                                                        cur_col_name.length(), cur_col_name.ptr(),
                                                        common::ob_strerror(err_ret)))) {
          }
        } else {
          if (OB_FAIL(err_msg.append_fmt("fail to scan file %.*s at line %ld, error: %s",
                                        cur_file_url_.length(), cur_file_url_.ptr(),
                                        idx + cur_line_number_,
                                        common::ob_strerror(err_ret)))) {
          }
        }

        if (OB_SUCC(ret)) {
          skip.set(idx);
          buffer->append_warning(err_msg.ptr(), err_ret);

          if (limit_num >= 0 && buffer->get_total_warning_count() > limit_num) {
            ret = OB_REACH_DIAGNOSIS_ERROR_LIMIT;
          }
        }
      }

      idxs_.reuse();
      rets_.reuse();
      col_names_.reuse();
      allocator_.reuse();
    }
  } else {
    // do nothing
  }

  return ret;
}

ObExecContext::ObExecContext(ObIAllocator &allocator,
                             ObSQLSessionMgr *session_mgr)
  : allocator_(allocator),
    phy_op_size_(0),
    phy_op_ctx_store_(NULL),
    phy_op_input_store_(NULL),
    phy_plan_ctx_(NULL),
    expr_op_size_(0),
    expr_op_ctx_store_(NULL),
    sql_executor_ctx_(),
    my_session_(NULL),
    session_mgr_(session_mgr),
    lob_read_service_(nullptr),
    plan_cache_(nullptr),
    ps_cache_(nullptr),
    plan_cache_access_service_(nullptr),
    pl_sql_runtime_(nullptr),
    pl_engine_(nullptr),
    prepared_statement_runtime_(nullptr),
    sql_execution_id_provider_(nullptr),
    query_runtime_environment_(nullptr),
    root_command_service_(nullptr),
    local_command_service_(nullptr),
    change_stream_service_(nullptr),
    ddl_execution_limiter_(nullptr),
    srs_provider_(nullptr),
    exec_stat_collector_(NULL),
    stmt_factory_(NULL),
    expr_factory_(NULL),
    execution_id_(OB_INVALID_ID),
    has_non_trivial_expr_op_ctx_(false),
    sql_ctx_(NULL),
    pl_stack_ctx_(nullptr),
    procedural_context_(nullptr),
    need_disconnect_(true),
    pl_ctx_(NULL),
    package_guard_(NULL),
    pl_expr_allocator_(NULL),
    row_id_list_(nullptr),
    row_id_list_array_(),
    total_row_count_(0),
    reusable_interm_result_(false),
    is_async_end_trans_(false),
    gi_task_map_(nullptr),
    output_row_(NULL),
    field_columns_(NULL),
    is_direct_local_plan_(false),
    sqc_handler_(nullptr),
    px_task_id_(-1),
    px_sqc_id_(-1),
    frames_(NULL),
    frame_cnt_(0),
    ori_frames_(nullptr),
    ori_frame_cnt_(0),
    ori_expr_op_size_(0),
    op_kit_store_(),
    convert_allocator_(nullptr),
    mem_context_(nullptr),
    group_pwj_map_(nullptr),
    check_status_times_(0),
    vt_ift_(nullptr),
    vt_factory_provider_(nullptr),
    px_batch_id_(0),
    admission_acquired_(false),
    use_temp_expr_ctx_cache_(false),
    temp_expr_ctx_map_(),
    dml_event_(ObDmlEventType::DE_INVALID),
    update_columns_(nullptr),
    expect_range_count_(0),
    das_ctx_(allocator),
    parent_ctx_(nullptr),
    nested_level_(0),
    is_ps_prepare_stage_(false),
    tmp_alloc_used_(false),
    errcode_(OB_SUCCESS),
    user_logging_ctx_(),
    is_online_stats_gathering_(false),
    is_ddl_idempotent_auto_inc_(false),
    slice_count_(0),
    slice_idx_(0),
    slice_row_idx_(0),
    autoinc_range_interval_(0),
    lob_access_ctx_(nullptr),
    lob_read_options_(nullptr),
    datum_access_ctx_(nullptr),
    resource_limit_calculator_(nullptr),
    auto_dop_map_(),
    force_local_plan_(false),
    diagnosis_manager_(),
    current_granule_type_(OB_GRANULE_UNINITIALIZED)
{
}

ObExecContext::RuntimeServices ObExecContext::get_runtime_services() const
{
  RuntimeServices services;
  services.lob_read_service_ = lob_read_service_;
  services.plan_cache_ = plan_cache_;
  services.ps_cache_ = ps_cache_;
  services.plan_cache_access_service_ = plan_cache_access_service_;
  services.pl_sql_runtime_ = pl_sql_runtime_;
  services.pl_engine_ = pl_engine_;
  services.prepared_statement_runtime_ = prepared_statement_runtime_;
  services.sql_execution_id_provider_ = sql_execution_id_provider_;
  services.query_runtime_environment_ = query_runtime_environment_;
  services.root_command_service_ = root_command_service_;
  services.local_command_service_ = local_command_service_;
  services.change_stream_service_ = change_stream_service_;
  services.ddl_execution_limiter_ = ddl_execution_limiter_;
  services.virtual_table_factory_provider_ = vt_factory_provider_;
  services.srs_provider_ = srs_provider_;
  services.resource_limit_calculator_ = resource_limit_calculator_;
  return services;
}

void ObExecContext::set_runtime_services(const RuntimeServices &services)
{
  lob_read_service_ = services.lob_read_service_;
  plan_cache_ = services.plan_cache_;
  ps_cache_ = services.ps_cache_;
  plan_cache_access_service_ = services.plan_cache_access_service_;
  pl_sql_runtime_ = services.pl_sql_runtime_;
  pl_engine_ = services.pl_engine_;
  prepared_statement_runtime_ = services.prepared_statement_runtime_;
  sql_execution_id_provider_ = services.sql_execution_id_provider_;
  query_runtime_environment_ = services.query_runtime_environment_;
  root_command_service_ = services.root_command_service_;
  local_command_service_ = services.local_command_service_;
  change_stream_service_ = services.change_stream_service_;
  ddl_execution_limiter_ = services.ddl_execution_limiter_;
  vt_factory_provider_ = services.virtual_table_factory_provider_;
  srs_provider_ = services.srs_provider_;
  resource_limit_calculator_ = services.resource_limit_calculator_;
}

ObExecContext::~ObExecContext()
{
  row_id_list_array_.reset();
  destroy_eval_allocator();
  reset_op_ctx();
  if (OB_NOT_NULL(exec_stat_collector_)) {
    exec_stat_collector_->~ObExecStatCollector();
    exec_stat_collector_ = NULL;
  }
  
  if (NULL != phy_plan_ctx_) {
    if (!THIS_WORKER.has_req_flag()) {
      // For background threads, need to call destructor
      phy_plan_ctx_->~ObPhysicalPlanCtx();
    } else {
      // free subschema map memory
      phy_plan_ctx_->destroy();
    }
  }
  phy_plan_ctx_ = NULL;
  // destory gi task info map
  if (OB_NOT_NULL(gi_task_map_)) {
    gi_task_map_->destroy();
    gi_task_map_ = NULL;
  }
  if (OB_NOT_NULL(pl_ctx_)) {
    pl_ctx_->~ObPLCtx();
    pl_ctx_ = NULL;
  }
  if (OB_NOT_NULL(package_guard_)) {
    package_guard_->~ObPLPackageGuard();
    package_guard_ = NULL;
  }
  if (OB_NOT_NULL(group_pwj_map_)) {
    group_pwj_map_->destroy();
    group_pwj_map_ = nullptr;
  }
  if (OB_NOT_NULL(vt_ift_)) {
    if (OB_NOT_NULL(vt_factory_provider_)) {
      vt_factory_provider_->destroy_virtual_table_factory(vt_ift_);
    } else {
      vt_ift_->~ObIVirtualTableIteratorFactory();
    }
    vt_ift_ = nullptr;
  }
  vt_factory_provider_ = nullptr;
  clean_resolve_ctx();
  sqc_handler_ = nullptr;
  if (OB_LIKELY(NULL != convert_allocator_)) {
    DESTROY_CONTEXT(convert_allocator_);
    convert_allocator_ = NULL;
  }
  if (OB_LIKELY(NULL != mem_context_)) {
    DESTROY_CONTEXT(mem_context_);
    mem_context_ = NULL;
  }
  if (!temp_expr_ctx_map_.created()) {
  // do nothing
  } else {
    for (hash::ObHashMap<int64_t, int64_t>::iterator it = temp_expr_ctx_map_.begin();
        it != temp_expr_ctx_map_.end();
        ++it) {
      (reinterpret_cast<ObTempExprCtx *>(it->second))->~ObTempExprCtx();
    }
    temp_expr_ctx_map_.destroy();
  }
  update_columns_ = nullptr;
  errcode_ = OB_SUCCESS;

  if (OB_NOT_NULL(lob_access_ctx_)) {
    data_plane::destroy_lob_access_context(lob_access_ctx_);
  }
  auto_dop_map_.destroy();
}

void ObExecContext::set_my_session(ObSQLSessionInfo *session)
{
  my_session_ = session;
  if (OB_NOT_NULL(session)) {
    session_mgr_ = session->get_session_manager();
  }
  if (OB_NOT_NULL(session)) {
    set_mem_attr(ObMemAttr(ObModIds::OB_SQL_EXEC_CONTEXT,
                          ObCtxIds::EXECUTE_CTX_ID));
  }
}

void ObExecContext::clean_resolve_ctx()
{
  if (OB_NOT_NULL(expr_factory_)) {
    expr_factory_->~ObRawExprFactory();
    expr_factory_ = nullptr;
  }
  if (OB_NOT_NULL(stmt_factory_)) {
    stmt_factory_->~ObStmtFactory();
    stmt_factory_ = nullptr;
  }
  sql_ctx_ = nullptr;
  pl_stack_ctx_ = nullptr;
  procedural_context_ = nullptr;
}

uint64_t ObExecContext::get_ser_version() const
{
  return SER_VERSION_1;
}

int ObExecContext::get_exec_stat_collector(ObExecStatCollector *&collector)
{
  int ret = OB_SUCCESS;
  collector = exec_stat_collector_;
  if (OB_ISNULL(collector)) {
    void *buf = allocator_.alloc(sizeof(ObExecStatCollector));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate execution stat collector failed", K(ret));
    } else {
      collector = new (buf) ObExecStatCollector();
      exec_stat_collector_ = collector;
    }
  }
  return ret;
}

void ObExecContext::reset_op_ctx()
{
  reset_expr_op();
  op_kit_store_.destroy();
}

void ObExecContext::reset_op_env()
{
  reset_op_ctx();
  op_kit_store_.reset();
  phy_op_size_ = 0;
  expr_op_size_ = 0;
  output_row_ = NULL;
  field_columns_ = NULL;
  if (OB_NOT_NULL(gi_task_map_)) {
    if (gi_task_map_->created()) {
      gi_task_map_->clear();
    }
  }
}
int ObExecContext::init_phy_op(const uint64_t phy_op_size)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(phy_op_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fail to initialize", K(phy_op_size));
  } else if (OB_UNLIKELY(phy_op_size_ > 0)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init exec ctx twice", K_(phy_op_size));
  } else if (NULL == my_session_) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info not set", K(ret));
  } else {
    phy_op_size_ = phy_op_size;
    if (OB_FAIL(op_kit_store_.init(allocator_, phy_op_size))) {
    }
  }
  return ret;
}

int ObExecContext::init_expr_op(uint64_t expr_op_size, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  ObIAllocator &real_alloc = allocator != NULL ? *allocator : allocator_;
  if (OB_UNLIKELY(expr_op_size_ > 0)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init exec ctx twice", K(ret), K_(expr_op_size));
  } else if (expr_op_size > 0) {
    int64_t ctx_store_size = static_cast<int64_t>(expr_op_size * sizeof(ObExprOperatorCtx *));
    if (OB_ISNULL(expr_op_ctx_store_ = static_cast<ObExprOperatorCtx **>(real_alloc.alloc(ctx_store_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("fail to alloc expr_op_ctx_store_ memory", K(ret), K(ctx_store_size));
    } else {
      expr_op_size_ = expr_op_size;
      MEMSET(expr_op_ctx_store_, 0, ctx_store_size);
    }
  }
  return ret;
}

void ObExecContext::reset_expr_op()
{
  if (expr_op_ctx_store_ != NULL) {
    ObExprOperatorCtx **it = expr_op_ctx_store_;
    ObExprOperatorCtx **it_end = &expr_op_ctx_store_[expr_op_size_];
    for (; it != it_end; ++it) {
      if (NULL != (*it)) {
        (*it)->~ObExprOperatorCtx();
      }
    }
    has_non_trivial_expr_op_ctx_ = false;
    expr_op_ctx_store_ = NULL;
    expr_op_size_ = 0;
  }
}

void ObExecContext::destroy_eval_allocator()
{
  eval_res_allocator_.reset();
  eval_tmp_allocator_.reset();
  tmp_alloc_used_ = false;
}

int ObExecContext::get_temp_expr_eval_ctx(const ObTempExpr &temp_expr,
                                          ObTempExprCtx *&temp_expr_ctx)
{
  int ret = OB_SUCCESS;
  if (use_temp_expr_ctx_cache_) {
    if (!temp_expr_ctx_map_.created()) {
      OZ(temp_expr_ctx_map_.create(8, ObMemAttr("TempExprCtx")));
    }
    if (OB_SUCC(ret)) {
      int64_t ctx_ptr = 0;
      if (OB_FAIL(temp_expr_ctx_map_.get_refactored(reinterpret_cast<int64_t>(&temp_expr),
                                                    ctx_ptr))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          OZ(build_temp_expr_ctx(temp_expr, temp_expr_ctx));
          CK(OB_NOT_NULL(temp_expr_ctx));
          OZ(temp_expr_ctx_map_.set_refactored(reinterpret_cast<int64_t>(&temp_expr),
                                               reinterpret_cast<int64_t>(temp_expr_ctx)));
        } else {
          LOG_WARN("fail to get temp expr ctx", K(temp_expr), K(ret));
        }
      } else {
        temp_expr_ctx = reinterpret_cast<ObTempExprCtx *>(ctx_ptr);
      }
    }
  } else {
    OZ(build_temp_expr_ctx(temp_expr, temp_expr_ctx));
  }

  return ret;
}

int ObExecContext::build_temp_expr_ctx(const ObTempExpr &temp_expr, ObTempExprCtx *&temp_expr_ctx)
{
  int ret = OB_SUCCESS;
  uint64_t frame_cnt = 0;
  char **frames = NULL;
  char *mem = static_cast<char*>(get_allocator().alloc(sizeof(ObTempExprCtx)));
  ObArray<char *> tmp_param_frame_ptrs;
  if (OB_ISNULL(mem)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("no more memory to create temp expr ctx", K(ret));
  }
  OX(temp_expr_ctx = new(mem)ObTempExprCtx(*this));
  OZ(temp_expr.alloc_frame(get_allocator(), tmp_param_frame_ptrs, frame_cnt, frames));
  OX(temp_expr_ctx->frames_ = frames);
  OX(temp_expr_ctx->frame_cnt_ = frame_cnt);
  // init expr_op_size_ and expr_op_ctx_store_
  if (OB_SUCC(ret)) {
    if (temp_expr.need_ctx_cnt_ > 0) {
      int64_t ctx_store_size = static_cast<int64_t>(
                               temp_expr.need_ctx_cnt_ * sizeof(ObExprOperatorCtx *));
      if (OB_ISNULL(temp_expr_ctx->expr_op_ctx_store_
                    = static_cast<ObExprOperatorCtx **>(allocator_.alloc(ctx_store_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("fail to alloc expr_op_ctx_store_ memory", K(ret), K(ctx_store_size));
      } else {
        temp_expr_ctx->expr_op_size_ = temp_expr.need_ctx_cnt_;
        MEMSET(temp_expr_ctx->expr_op_ctx_store_, 0, ctx_store_size);
      }
    }
  }

  return ret;
}



ObIAllocator &ObExecContext::get_sche_allocator()
{
  return sche_allocator_;
}

ObIAllocator &ObExecContext::get_allocator()
{
  return allocator_;
}

int ObExecContext::create_expr_op_ctx(uint64_t op_id, int64_t op_ctx_size, void *&op_ctx)
{
  int ret = OB_SUCCESS;
  ObIAllocator &allocator = OB_NOT_NULL(pl_expr_allocator_) ? *pl_expr_allocator_ : allocator_;
  if (OB_UNLIKELY(op_id >= expr_op_size_ || op_ctx_size <= 0 || OB_ISNULL(expr_op_ctx_store_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(op_id), K(op_ctx_size), K(expr_op_ctx_store_));
  } else if (OB_UNLIKELY(NULL != get_expr_op_ctx(op_id))) {
    ret = OB_INIT_TWICE;
    LOG_WARN("expr operator context has been created", K(op_id));
  } else if (OB_ISNULL(op_ctx = allocator.alloc(op_ctx_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("allocate memory failed", K(ret), K(op_id), K(op_ctx_size));
  } else {
    expr_op_ctx_store_[op_id] = static_cast<ObExprOperatorCtx *>(op_ctx);
    has_non_trivial_expr_op_ctx_ = true;
  }
  return ret;
}

void *ObExecContext::get_expr_op_ctx(uint64_t op_id)
{
  return (OB_LIKELY(op_id < expr_op_size_) && !OB_ISNULL(expr_op_ctx_store_)) ? expr_op_ctx_store_[op_id] : NULL;
}

int ObExecContext::create_physical_plan_ctx()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *local_plan_ctx = NULL;
  if (OB_UNLIKELY(phy_plan_ctx_ != NULL)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("phy_plan_ctx_ is not null");
  } else if (OB_UNLIKELY(NULL == (local_plan_ctx = static_cast<ObPhysicalPlanCtx *>(
      allocator_.alloc(sizeof(ObPhysicalPlanCtx)))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("no more memory to create physical_plan_ctx");
  } else {
    phy_plan_ctx_ = new (local_plan_ctx) ObPhysicalPlanCtx(allocator_);
    phy_plan_ctx_->set_exec_ctx(this);
  }
  return ret;
}

ObStmtFactory *ObExecContext::get_stmt_factory()
{
  if (OB_ISNULL(stmt_factory_)) {
    if (OB_ISNULL(stmt_factory_ = OB_NEWx(ObStmtFactory, (&allocator_), allocator_))) {
      LOG_ERROR_RET(OB_ALLOCATE_MEMORY_FAILED, "fail to create log plan factory", K(stmt_factory_));
    }
  } else {
    // do nothing
  }
  return stmt_factory_;
}

ObRawExprFactory *ObExecContext::get_expr_factory()
{
  if (OB_ISNULL(expr_factory_)) {
    if (OB_ISNULL(expr_factory_ = OB_NEWx(ObRawExprFactory, (&allocator_), allocator_))) {
      LOG_ERROR_RET(OB_ALLOCATE_MEMORY_FAILED, "fail to create log plan factory", K(expr_factory_));
    }
  } else {
    // do nothing
  }
  return expr_factory_;
}

int ObExecContext::check_status()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical plan ctx is null");
  } else if (phy_plan_ctx_->is_exec_timeout()) {
    ret = OB_TIMEOUT;
    LOG_WARN("query is timeout", K(ret));
  } else if (OB_ISNULL(my_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is null");
  } else if (my_session_->is_terminate(ret)){
    LOG_WARN("execution was terminated", K(ret));
  } else if (IS_INTERRUPTED()) {
    ObInterruptCode &ic = GET_INTERRUPT_CODE();
    ret = ic.code_;
    LOG_WARN("px execution was interrupted", K(ic), K(ret));
  } else if (OB_UNLIKELY((OB_SUCCESS != (ret = CHECK_MEM_STATUS())))) {
  }
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = check_extra_status())) {
    LOG_WARN("check extra status failed", K(tmp_ret));
    if (OB_SUCC(ret)) {
      ret = tmp_ret;
    }
  }
  return ret;
}

int ObExecContext::fast_check_status(const int64_t n)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((check_status_times_++ & n) == n)) {
    ret = check_status();
  }
  return ret;
}

int ObExecContext::check_status_ignore_interrupt()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical plan ctx is null", K(ret));
  } else if (phy_plan_ctx_->is_timeout()) {
    ret = OB_TIMEOUT;
    LOG_WARN("query is timeout", K(ret));
  } else if (OB_ISNULL(my_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is null", K(ret));
  } else if (my_session_->is_terminate(ret)){
    LOG_WARN("execution was terminated", K(ret));
  }
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = check_extra_status())) {
  } else if (OB_SUCC(ret)) {
    ret = tmp_ret;
  }

  return ret;
}

int ObExecContext::fast_check_status_ignore_interrupt(const int64_t n)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((check_status_times_++ & n) == n)) {
    ret = check_status_ignore_interrupt();
  }
  return ret;
}

int ObExecContext::init_pl_ctx()
{
  int ret = OB_SUCCESS;
  pl::ObPLCtx *pl_ctx = NULL;
  if (OB_ISNULL(pl_ctx =
    static_cast<pl::ObPLCtx*>(get_allocator().alloc(sizeof(pl::ObPLCtx))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocator memory", K(ret), K(sizeof(pl::ObPLCtx)));
  } else {
    new(pl_ctx)pl::ObPLCtx();
    set_pl_ctx(pl_ctx);
  }
  return ret;
}

const common::ObAddr& ObExecContext::get_addr() const
{
  return GCTX.self_addr();
}

int ObExecContext::get_gi_task_map(GIPrepareTaskMap *&gi_task_map)
{
  int ret = OB_SUCCESS;
  gi_task_map = nullptr;
  if (nullptr == gi_task_map_) {
    void *buf = allocator_.alloc(sizeof(GIPrepareTaskMap));
    if (nullptr == buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to allocate memories", K(ret));
    } else if (FALSE_IT(gi_task_map_ = new(buf) GIPrepareTaskMap())) {
    } else if (OB_FAIL(gi_task_map_->create(PARTITION_WISE_JOIN_TSC_HASH_BUCKET_NUM, /* assume no more than 8 table scan in a plan */
                                            ObModIds::OB_SQL_PX))) {
    } else {
      gi_task_map = gi_task_map_;
    }
  } else {
    gi_task_map = gi_task_map_;
  }
  return ret;
}

int ObExecContext::get_convert_charset_allocator(ObArenaAllocator *&allocator)
{
  int ret = OB_SUCCESS;
  allocator = NULL;
  if (OB_ISNULL(convert_allocator_)) {
    if (OB_ISNULL(my_session_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("session is null", K(ret));
    } else {
      lib::ContextParam param;
      param.set_properties(lib::USE_TL_PAGE_OPTIONAL)
           .set_mem_attr(common::ObModIds::OB_SQL_EXPR_CALC,
                         common::ObCtxIds::DEFAULT_CTX_ID);
      if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(convert_allocator_, param))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    allocator = &convert_allocator_->get_arena_allocator();
  }

  return ret;
}

int ObExecContext::get_malloc_allocator(ObIAllocator *&allocator)
{
  int ret = OB_SUCCESS;
  allocator = NULL;
  if (OB_ISNULL(mem_context_)) {
    if (OB_ISNULL(my_session_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("session is null", K(ret));
    } else {
      lib::ContextParam param;
      param.set_properties(lib::USE_TL_PAGE_OPTIONAL)
           .set_mem_attr(common::ObModIds::OB_SQL_EXPR_CALC,
                         common::ObCtxIds::DEFAULT_CTX_ID);
      if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    allocator = &mem_context_->get_malloc_allocator();
  }

  return ret;
}

void ObExecContext::try_reset_convert_charset_allocator()
{
  if (OB_NOT_NULL(convert_allocator_)) {
    convert_allocator_->reset_remain_one_page();
  }
}


int ObExecContext::add_temp_table_interm_result_ids(uint64_t temp_table_id,
                                                    const ObIArray<uint64_t> &ids)
{
  int ret = OB_SUCCESS;
  bool is_existed = false;
  ObIArray<ObSqlTempTableCtx>& temp_ctx = get_temp_table_ctx();
  for (int64_t i = 0; OB_SUCC(ret) && !is_existed && i < temp_ctx.count(); i++) {
    ObSqlTempTableCtx &ctx = temp_ctx.at(i);
    if (temp_table_id == ctx.temp_table_id_) {
      ObTempTableResultInfo info;
      if (OB_FAIL(info.interm_result_ids_.assign(ids))) {
      } else if (OB_FAIL(ctx.interm_result_infos_.push_back(info))) {
      } else {
        is_existed = true;
      }
    }
  }
  if (OB_SUCC(ret) && !is_existed) {
    ObSqlTempTableCtx ctx;
    ctx.is_local_interm_result_ = false;
    ctx.temp_table_id_ = temp_table_id;
    ObTempTableResultInfo info;
    if (OB_FAIL(info.interm_result_ids_.assign(ids))) {
    } else if (OB_FAIL(ctx.interm_result_infos_.push_back(info))) {
    } else if (OB_FAIL(temp_ctx.push_back(ctx))) {
    }
  }
  return ret;
}

ObVirtualTableCtx ObExecContext::get_virtual_table_ctx()
{
  int ret = OB_SUCCESS;
  ObVirtualTableCtx vt_ctx;
  if (OB_ISNULL(vt_ift_)) {
    if (OB_ISNULL(vt_factory_provider_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("virtual table factory provider is null", K(ret));
    } else if (OB_FAIL(vt_factory_provider_->create_virtual_table_factory(allocator_, vt_ift_))) {
    }
  }
  vt_ctx.vt_iter_factory_ = vt_ift_;
  vt_ctx.session_ = my_session_;
  vt_ctx.schema_guard_ = sql_ctx_->schema_guard_;
  return vt_ctx;
}

int ObExecContext::init_physical_plan_ctx(const ObPhysicalPlan &plan)
{
  int ret = OB_SUCCESS;
  int64_t foreign_key_checks = 0;
  uint64_t data_format_version = 0;
  bool supprt_check_pdml_affected_row = false;
  if (OB_ISNULL(phy_plan_ctx_) || OB_ISNULL(my_session_) || OB_ISNULL(sql_ctx_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K_(phy_plan_ctx), K_(my_session), K(ret));
  } else if (OB_FAIL(my_session_->get_foreign_key_checks(foreign_key_checks))) {
  } else {
    int64_t start_time = my_session_->get_query_start_time();
    int64_t plan_timeout = 0;
    const ObPhyPlanHint &phy_plan_hint = plan.get_phy_plan_hint();
    ObConsistencyLevel consistency = INVALID_CONSISTENCY;
    my_session_->set_cur_phy_plan(const_cast<ObPhysicalPlan*>(&plan));
    
    part_ranges_.set_label("PxTabletRangArr");
    if (OB_UNLIKELY(phy_plan_hint.query_timeout_ > 0)) {
      plan_timeout = phy_plan_hint.query_timeout_;
    } else {
      if (OB_FAIL(my_session_->get_query_timeout(plan_timeout))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(phy_plan_ctx_->reserve_param_space(plan.get_param_count()))) {
      LOG_WARN("reserve param space failed", K(ret), K(plan.get_param_count()));
    }
    if (OB_SUCC(ret)) {
      if (stmt::T_SELECT == plan.get_stmt_type()) { // select has weak
        if (OB_UNLIKELY(phy_plan_hint.read_consistency_ != INVALID_CONSISTENCY)) {
          consistency = phy_plan_hint.read_consistency_;
        } else {
          consistency = my_session_->get_consistency_level();
        }
      } else {
        consistency = STRONG;
      }
      phy_plan_ctx_->set_consistency_level(consistency);
      phy_plan_ctx_->set_timeout_timestamp(start_time + plan_timeout);
      reference_my_plan(&plan);
      phy_plan_ctx_->set_ignore_stmt(plan.is_ignore());
      phy_plan_ctx_->set_foreign_key_checks(0 != foreign_key_checks);
      phy_plan_ctx_->set_table_row_count_list_capacity(plan.get_access_table_num());
      phy_plan_ctx_->set_check_pdml_affected_rows(supprt_check_pdml_affected_row);
      THIS_WORKER.set_timeout_ts(phy_plan_ctx_->get_timeout_timestamp());
    }
  }
  return ret;
}

int ObExecContext::set_partition_ranges(const Ob2DArray<ObPxTabletRange> &part_ranges,
                                        char *buf, int64_t size)
{
  int ret = OB_SUCCESS;
  part_ranges_.reset();
  if (OB_UNLIKELY(part_ranges.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("part ranges is empty", K(ret), K(part_ranges.count()));
  } else {
    int64_t pos = 0;
    ObPxTabletRange tmp_range;
    for (int64_t i = 0; OB_SUCC(ret) && i < part_ranges.count(); ++i) {
      const ObPxTabletRange &cur_range = part_ranges.at(i);
      if (0 == size && OB_FAIL(tmp_range.deep_copy_from<true>(cur_range, get_allocator(), buf, size, pos))) {
        LOG_WARN("deep copy partition range failed", K(ret), K(cur_range));
      } else if (0 != size && OB_FAIL(tmp_range.deep_copy_from<false>(cur_range, get_allocator(), buf, size, pos))) {
        LOG_WARN("deep copy partition range failed", K(ret), K(cur_range));
      } else if (OB_FAIL(part_ranges_.push_back(tmp_range))) {
      }
    }
  }
  return ret;
}

int ObExecContext::reset_one_row_id_list(const common::ObIArray<int64_t> *row_id_list)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(row_id_list));
  if (OB_SUCC(ret)) {
    row_id_list_array_.reset();
    total_row_count_ = 0;
    OZ(row_id_list_array_.push_back(row_id_list));
    total_row_count_ += row_id_list->count();
  }
  return ret;
}


int ObExecContext::get_group_pwj_map(GroupPWJTabletIdMap *&group_pwj_map)
{
  int ret = OB_SUCCESS;
  group_pwj_map = nullptr;
  if (nullptr == group_pwj_map_) {
    void *buf = allocator_.alloc(sizeof(GroupPWJTabletIdMap));
    if (nullptr == buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to allocate memories", K(ret));
    } else {
      group_pwj_map_ = new (buf) GroupPWJTabletIdMap();
      /* assume no more than 8table scan in a plan */
      if (OB_FAIL(group_pwj_map_->create(PARTITION_WISE_JOIN_TSC_HASH_BUCKET_NUM, ObModIds::OB_SQL_PX))) {
      } else {
        group_pwj_map = group_pwj_map_;
      }
    }
  } else {
    group_pwj_map = group_pwj_map_;
  }
  return ret;
}

int ObExecContext::deep_copy_group_pwj_map(const GroupPWJTabletIdMap *src)
{
  int ret = OB_SUCCESS;
  GroupPWJTabletIdMap *des = nullptr;
  if (OB_ISNULL(src)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null");
  } else if (OB_FAIL(get_group_pwj_map(des))) {
  } else if (des->size() > 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("size should be 0", K(des->size()), K(src->size()));
  } else {
    FOREACH_X(iter, *src, OB_SUCC(ret)) {
      const uint64_t table_id = iter->first;
      const GroupPWJTabletIdInfo &group_pwj_tablet_id_info = iter->second;
      if (OB_FAIL(des->set_refactored(table_id, group_pwj_tablet_id_info))) {
      }
    }
  }
  return ret;
}


int ObExecContext::fill_px_batch_info(ObBatchRescanParams &params,
    int64_t batch_id, const sql::ObExpr::ObExprIArray &array)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("phy plan ctx is null", K(ret));
  } else if (batch_id >= params.get_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("batch param is unexpected", K(ret));
  } else {
    common::ObIArray<common::ObObjParam> &one_params =
        params.get_one_batch_params(batch_id);
    ObEvalCtx eval_ctx(*this);
    for (int i = 0; OB_SUCC(ret) && i < one_params.count(); ++i) {
      if (i > params.param_idxs_.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("batch param is unexpected", K(ret));
      } else {
        phy_plan_ctx_->get_param_store_for_update().at(params.get_param_idx(i)) = one_params.at(i);
        if (params.param_expr_idxs_.count() == one_params.count()) {
          const sql::ObExpr *expr = NULL;
          int64_t idx = params.param_expr_idxs_.at(i);
          if (OB_FAIL(ret)) {
          } else if (OB_UNLIKELY(idx > array.count())) {
            // do nothing.
            LOG_TRACE("param idx out of array count", K(idx), K(array.count()));
          } else if (FALSE_IT(expr = &array.at(idx - 1))) {
          } else if (T_INVALID == expr->type_) {
            // do nothing.
          } else {
            expr->get_eval_info(eval_ctx).clear_evaluated_flag();
            ObDynamicParamSetter::clear_parent_evaluated_flag(eval_ctx, *expr);
            ObDatum &param_datum = expr->locate_datum_for_write(eval_ctx);
            if (OB_FAIL(param_datum.from_obj(one_params.at(i), expr->obj_datum_map_))) {
            } else if (is_lob_storage(one_params.at(i).get_type()) &&
                       OB_FAIL(ob_adjust_lob_datum(*this, one_params.at(i), expr->obj_meta_,
                                                   expr->obj_datum_map_, get_allocator(), param_datum))) {
              LOG_WARN("adjust lob datum failed", K(ret), K(i),
                       K(one_params.at(i).get_meta()), K(expr->obj_meta_));
            } else {
              expr->get_eval_info(eval_ctx).evaluated_ = true;
            }
          }
        }
      }
    }
    px_batch_id_ = batch_id;
  }
  return ret;
}

int ObExecContext::check_extra_status()
{
  int ret = OB_SUCCESS;
  if (!extra_status_check_.is_empty()) {
    int tmp_ret = OB_SUCCESS;
    DLIST_FOREACH_X(it, extra_status_check_, true) {
      if (OB_SUCCESS != (tmp_ret = it->check())) {
        SQL_ENG_LOG(WARN, "extra check failed", K(tmp_ret), "check_name", it->name(),
                    "query", my_session_->get_current_query_string(),
                    "key", my_session_->get_server_sid());
        ret = OB_SUCC(ret) ? tmp_ret : ret;
      }
    }
  }
  return ret;
}

pl::ObPLPackageGuard* ObExecContext::get_package_guard()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(package_guard_)) {
    if (OB_ISNULL(get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("execute context `s session info is null!", K(ret), K(get_my_session()));
    } else if (OB_ISNULL(package_guard_ =
        reinterpret_cast<pl::ObPLPackageGuard*>
          (get_allocator().alloc(sizeof(pl::ObPLPackageGuard))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for exec context`s package guard!", K(ret));
    } else {
      package_guard_ =
        new(package_guard_)pl::ObPLPackageGuard{};
      if (OB_ISNULL(package_guard_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to construct exec context`s package guard!", K(ret), K(package_guard_));
      } else if (OB_FAIL(package_guard_->init())) {
      }
    }
  }
  return package_guard_;
}

int ObExecContext::get_package_guard(pl::ObPLPackageGuard *&package_guard)
{
  int ret = OB_SUCCESS;
  package_guard = get_package_guard();
  if (OB_ISNULL(package_guard)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get package guard failed", K(ret));
  }
  return ret;
}

DEFINE_SERIALIZE(ObExecContext)
{
  int ret = OB_SUCCESS;
  uint64_t ser_version = get_ser_version();

  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec context is invalid", K_(phy_op_size), K_(phy_op_ctx_store),
             K_(phy_op_input_store), K_(phy_plan_ctx), K_(my_session), K(ret));
  } else {
    phy_plan_ctx_->set_expr_op_size(ori_expr_op_size_ > 0 ? ori_expr_op_size_ : expr_op_size_);
    OB_UNIS_ENCODE(phy_op_size_);
    OB_UNIS_ENCODE(*phy_plan_ctx_);
    OB_UNIS_ENCODE(*my_session_);

    OB_UNIS_ENCODE(sql_executor_ctx_);
    OB_UNIS_ENCODE(das_ctx_);
    OB_UNIS_ENCODE(*sql_ctx_);
  }
  return ret;
}

DEFINE_DESERIALIZE(ObExecContext)
{
  int ret = OB_ERR_UNEXPECTED;
  UNUSED(buf);
  UNUSED(data_len);
  UNUSED(pos);
  LOG_WARN("not supported", K(ret));
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObExecContext)
{
  int64_t len = 0;
  uint64_t ser_version = get_ser_version();

  if (is_valid()) {
    phy_plan_ctx_->set_expr_op_size(ori_expr_op_size_ > 0 ? ori_expr_op_size_ : expr_op_size_);
    OB_UNIS_ADD_LEN(phy_op_size_);
    OB_UNIS_ADD_LEN(*phy_plan_ctx_);
    OB_UNIS_ADD_LEN(*my_session_);
    OB_UNIS_ADD_LEN(sql_executor_ctx_);
    OB_UNIS_ADD_LEN(das_ctx_);
    OB_UNIS_ADD_LEN(*sql_ctx_);
  }
  return len;
}

int64_t ObExecContext::get_group_pwj_map_serialize_size() const
{
  int64_t len = 0;
  // add serialize size for group_pwj_map_
  int64_t pwj_map_element_count = 0;
  if (group_pwj_map_ != nullptr) {
    pwj_map_element_count = group_pwj_map_->size();
    OB_UNIS_ADD_LEN(pwj_map_element_count);
    FOREACH(iter, *group_pwj_map_) {
      const uint64_t table_id = iter->first;
      const GroupPWJTabletIdInfo &group_pwj_tablet_id_info = iter->second;
      OB_UNIS_ADD_LEN(table_id);
      OB_UNIS_ADD_LEN(group_pwj_tablet_id_info);
    }
  } else {
    OB_UNIS_ADD_LEN(pwj_map_element_count);
  }
  return len;
}

int ObExecContext::serialize_group_pwj_map(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  // serialize group_pwj_map_
  int64_t pwj_map_element_count = 0;
  if (OB_SUCC(ret)) {
    if (group_pwj_map_ != nullptr) {
      pwj_map_element_count = group_pwj_map_->size();
      OB_UNIS_ENCODE(pwj_map_element_count);
      FOREACH_X(iter, *group_pwj_map_, OB_SUCC(ret)) {
        const uint64_t table_id = iter->first;
        const GroupPWJTabletIdInfo &group_pwj_tablet_id_info = iter->second;
        OB_UNIS_ENCODE(table_id);
        OB_UNIS_ENCODE(group_pwj_tablet_id_info);
      }
    } else {
      OB_UNIS_ENCODE(pwj_map_element_count);
    }
  }
  return ret;
}

int ObExecContext::deserialize_group_pwj_map(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  // deserialize size for group_pwj_map_
  int64_t pwj_map_element_count = 0;
  OB_UNIS_DECODE(pwj_map_element_count);
  if (OB_SUCC(ret) && pwj_map_element_count > 0) {
    GroupPWJTabletIdMap *group_pwj_map = nullptr;
    uint64_t table_id;
    GroupPWJTabletIdInfo group_pwj_tablet_id_info;
    if (OB_FAIL(get_group_pwj_map(group_pwj_map))) {
    } else {
      for (int64_t i = 0; i < pwj_map_element_count && OB_SUCC(ret); ++i) {
        OB_UNIS_DECODE(table_id);
        OB_UNIS_DECODE(group_pwj_tablet_id_info);
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(group_pwj_map->set_refactored(table_id, group_pwj_tablet_id_info))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      group_pwj_map_ = group_pwj_map;
    }
  }
  return ret;
}

int ObExecContext::get_sqludt_meta_by_subschema_id(uint16_t subschema_id, ObSqlUDTMeta &udt_meta) const
{
  int ret = OB_SUCCESS;
  if (ob_is_reserved_subschema_id(subschema_id)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported reserved subschema id", K(ret), K(subschema_id));
  } else if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for subschema mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_sqludt_meta_by_subschema_id(subschema_id, udt_meta);
  }
  return ret;
}

int ObExecContext::get_sqludt_meta_by_subschema_id(uint16_t subschema_id, ObSubSchemaValue &sub_meta) const
{
  int ret = OB_SUCCESS;
  if (ob_is_reserved_subschema_id(subschema_id)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "unexpected subschema id", K(ret), K(subschema_id), K(lbt()));
  } else if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for subschema mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_sqludt_meta_by_subschema_id(subschema_id, sub_meta);
  }
  return ret;
}

int ObExecContext::get_enumset_meta_by_subschema_id(uint16_t subschema_id,
                                                    bool is_in_pl,
                                                    const ObEnumSetMeta *&meta) const
{
  int ret = OB_SUCCESS;
  if (ob_is_reserved_subschema_id(subschema_id)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_ENG_LOG(WARN, "reserved subschema id not used in enumset meta", K(ret), K(lbt()));
  } else if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for subschema mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_enumset_meta_by_subschema_id(subschema_id, is_in_pl, meta);
  }
  return ret;
}

int ObExecContext::get_subschema_id_by_udt_id(uint64_t udt_type_id,
                                              uint16_t &subschema_id,
                                              share::schema::ObSchemaGetterGuard *schema_guard) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for reverse mapping", K(ret), K(lbt()));
  } else {
    schema_guard = OB_ISNULL(schema_guard) ? get_sql_ctx()->schema_guard_ : schema_guard;
    ret = phy_plan_ctx_->get_subschema_id_by_udt_id(udt_type_id, subschema_id, schema_guard);
  }
  return ret;
}

int ObExecContext::get_subschema_id_by_collection_elem_type(ObNestedType coll_type,
                                                            const ObDataType &elem_type,
                                                            uint16_t &subschema_id) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for reverse mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_subschema_id_by_collection_elem_type(coll_type, elem_type, subschema_id);
  }
  return ret;
}

int ObExecContext::get_subschema_id_by_type_info(const ObObjMeta &obj_meta,
                                                 const ObIArray<common::ObString> &type_info,
                                                 uint16_t &subschema_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for reverse mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_subschema_id_by_type_info(obj_meta, type_info, subschema_id);
  }
  return ret;
}

int ObExecContext::get_subschema_id_by_type_info(const ObObjMeta &obj_meta,
                                                 const ObIArray<common::ObString> &type_info,
                                                 uint16_t &subschema_id) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for reverse mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_subschema_id_by_type_info(obj_meta, type_info, subschema_id);
  }
  return ret;
}

int ObExecContext::get_subschema_id_by_type_string(const ObString &type_string, uint16_t &subschema_id) 
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for reverse mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_subschema_id_by_type_string(type_string, subschema_id);
  }
  return ret;
}

int ObExecContext::get_subschema_id_by_type_string(const ObString &type_string, uint16_t &subschema_id) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan_ctx_)) {
    ret = OB_NOT_INIT;
    SQL_ENG_LOG(WARN, "not phyical plan ctx for reverse mapping", K(ret), K(lbt()));
  } else {
    ret = phy_plan_ctx_->get_subschema_id_by_type_string(type_string, subschema_id);
  }
  return ret;
}

int ObExecContext::get_lob_access_ctx(common::ObILobAccessContext *&lob_access_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(lob_access_ctx_)) {
    lob_access_ctx = lob_access_ctx_;
  } else if (OB_FAIL(data_plane::create_lob_access_context(
                 get_allocator(), lob_access_ctx_))) {
  } else {
    lob_access_ctx = lob_access_ctx_;
  }
  return ret;
}

int ObExecContext::get_lob_read_options(
    const common::ObLobReadOptions *&lob_read_options)
{
  int ret = OB_SUCCESS;
  lob_read_options = nullptr;
  common::ObILobReadService *read_service = lob_read_service_;
  common::ObILobAccessContext *lob_access_ctx = nullptr;
  if (OB_ISNULL(read_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("LOB read service is not installed in execution context", K(ret));
  } else if (OB_FAIL(get_lob_access_ctx(lob_access_ctx))) {
  } else {
    const int64_t timeout_ts = OB_ISNULL(my_session_)
        ? 0
        : query::ObSessionAccess::get_query_timeout_ts(my_session_);
    if (OB_ISNULL(lob_read_options_)) {
      void *buf = allocator_.alloc(sizeof(common::ObLobReadOptions));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate LOB read options failed", K(ret));
      } else {
        lob_read_options_ = new (buf) common::ObLobReadOptions(
            *read_service, timeout_ts, lob_access_ctx);
      }
    } else {
      lob_read_options_->read_service_ = read_service;
      lob_read_options_->timeout_ts_ = timeout_ts;
      lob_read_options_->access_context_ = lob_access_ctx;
    }
    if (OB_SUCC(ret)) {
      lob_read_options = lob_read_options_;
    }
  }
  return ret;
}

int ObExecContext::get_datum_access_ctx(
    const common::ObDatumAccessContext *&datum_access_ctx)
{
  int ret = OB_SUCCESS;
  datum_access_ctx = nullptr;
  const common::ObLobReadOptions *lob_read_options = nullptr;
  common::ObILobReadService *read_service = lob_read_service_;
  // Datum comparison and hashing only need this context when they encounter
  // an out-row LOB.  Keep it absent for pure in-row execution; the LOB
  // iterator rejects a missing read service at the actual dereference point.
  if (OB_ISNULL(read_service)) {
  } else if (OB_FAIL(get_lob_read_options(lob_read_options))) {
  } else if (OB_ISNULL(datum_access_ctx_)) {
    void *buf = allocator_.alloc(sizeof(common::ObDatumAccessContext));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate datum access context failed", K(ret));
    } else {
      datum_access_ctx_ =
          new (buf) common::ObDatumAccessContext(*lob_read_options);
    }
  } else {
    datum_access_ctx_->lob_read_options_ = lob_read_options;
  }
  if (OB_SUCC(ret)) {
    datum_access_ctx = datum_access_ctx_;
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase

namespace oceanbase
{
namespace query
{

sql::ObSQLSessionInfo *ObExecContextAccess::get_session(sql::ObExecContext &ctx)
{
  return ctx.get_my_session();
}

void ObExecContextAccess::configure_obj_cast(
    sql::ObExecContext &ctx,
    common::ObObjCastParams &params)
{
  if (OB_NOT_NULL(ctx.get_my_session())) {
    ctx.get_my_session()->configure_obj_cast(
        params, ctx.get_srs_provider(), ctx.get_lob_read_service());
  }
}

common::ObMySQLProxy *ObExecContextAccess::get_sql_proxy(sql::ObExecContext &ctx)
{
  return ctx.get_sql_proxy();
}

share::schema::ObSchemaGetterGuard *ObExecContextAccess::get_schema_guard(
    sql::ObExecContext &ctx)
{
  sql::ObSqlCtx *sql_ctx = ctx.get_sql_ctx();
  return nullptr == sql_ctx ? nullptr : sql_ctx->schema_guard_;
}

int ObExecContextAccess::check_status(sql::ObExecContext &ctx)
{
  return ctx.check_status();
}

int ObExecContextAccess::get_error_code(const sql::ObExecContext &ctx)
{
  return ctx.get_errcode();
}

uint64_t ObExecContextAccess::get_server_session_id(
    const sql::ObSQLSessionInfo *session)
{
  return nullptr == session ? common::OB_INVALID_ID : session->get_server_sid();
}

uint64_t ObExecContextAccess::get_priv_user_id(
    const sql::ObSQLSessionInfo *session)
{
  return nullptr == session ? common::OB_INVALID_ID : session->get_priv_user_id();
}

} // namespace query
} // namespace oceanbase
