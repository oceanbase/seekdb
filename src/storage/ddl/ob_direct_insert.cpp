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

#include "data_plane/ddl/ob_direct_insert.h"
#include "data_plane/ddl/ob_direct_load_type.h"
#include "query/engine/basic/ob_spill_batch_spool.h"
#include "share/ob_batch_selector.h"
#include "share/ob_ddl_common.h"
#include "storage/ddl/ob_ddl_dag_thread_pool.h"
#include "storage/ddl/ob_ddl_insert_dag.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/ddl/ob_tablet_slice_writer.h"
#include "data_plane/scheduler/ob_dag_scheduler.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

enum DirectInsertWriterImplType
{
  INVALID_WRITER_IMPL = 0,
  HEAP_ROW_WRITER_IMPL,
  HEAP_BATCH_WRITER_IMPL,
  ORDERED_ROW_WRITER_IMPL,
  ORDERED_BATCH_WRITER_IMPL
};

// Adapts a public, non-owning pointer span to the legacy storage writer
// contract without copying or allocating per row/batch.
template <typename T>
class BorrowedIArray final : public common::ObIArray<T>
{
public:
  BorrowedIArray(const T *data, const int64_t count)
    : common::ObIArray<T>(const_cast<T *>(data), count)
  {}

  int push_back(const T &) override { return common::OB_NOT_SUPPORTED; }
  void pop_back() override {}
  int pop_back(T &) override { return common::OB_NOT_SUPPORTED; }
  int remove(int64_t) override { return common::OB_NOT_SUPPORTED; }
  int at(const int64_t idx, T &obj) const override
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(idx < 0 || idx >= this->count_)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      obj = this->data_[idx];
    }
    return ret;
  }
  void reset() override {}
  void reuse() override {}
  void destroy() override {}
  int reserve(int64_t) override { return common::OB_NOT_SUPPORTED; }
  int assign(const common::ObIArray<T> &) override
  {
    return common::OB_NOT_SUPPORTED;
  }
  int prepare_allocate(int64_t) override { return common::OB_NOT_SUPPORTED; }
  void extra_access_check() const override {}
};

void destroy_writer_impl(common::ObIAllocator &allocator,
                         storage::ObISliceWriter *&writer,
                         const DirectInsertWriterImplType type)
{
  if (nullptr != writer) {
    switch (type) {
      case HEAP_ROW_WRITER_IMPL:
        static_cast<storage::ObHeapRsSliceWriter *>(writer)->~ObHeapRsSliceWriter();
        break;
      case HEAP_BATCH_WRITER_IMPL:
        static_cast<storage::ObHeapBatchSliceWriter *>(writer)->~ObHeapBatchSliceWriter();
        break;
      case ORDERED_ROW_WRITER_IMPL:
        static_cast<storage::ObRsSliceWriter *>(writer)->~ObRsSliceWriter();
        break;
      case ORDERED_BATCH_WRITER_IMPL:
        static_cast<storage::ObBatchSliceWriter *>(writer)->~ObBatchSliceWriter();
        break;
      default: {
        const int ret = common::OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid direct insert writer implementation", K(type), KP(writer));
        break;
      }
    }
    allocator.free(writer);
    writer = nullptr;
  }
}

class ObDirectInsertWriterAdapter final : public ObIDirectInsertWriter
{
public:
  ObDirectInsertWriterAdapter(common::ObIAllocator &allocator,
                              storage::ObISliceWriter &writer,
                              const DirectInsertWriterImplType type)
    : allocator_(&allocator), writer_(&writer), type_(type)
  {}

  ~ObDirectInsertWriterAdapter() override
  {
    destroy_writer_impl(*allocator_, writer_, type_);
  }

  int append_row(const ObDirectInsertRowView &row) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(!row.is_valid())) {
      ret = common::OB_INVALID_ARGUMENT;
      LOG_WARN("invalid direct insert row view", K(ret));
    } else {
      BorrowedIArray<common::ObDatum *> datums(row.datums_, row.datum_count_);
      if (OB_FAIL(writer_->append_current_row(datums))) {
      }
    }
    return ret;
  }

  int append_batch(const ObDirectInsertBatchView &batch) override
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(!batch.is_valid())) {
      ret = common::OB_INVALID_ARGUMENT;
      LOG_WARN("invalid direct insert batch view", K(ret));
    } else {
      share::ObBatchSelector selector;
      if (ObDirectInsertBatchView::CONTIGUOUS_SELECTION == batch.selection_type_) {
        selector.set_continous_rows(batch.offset_, batch.row_count_);
      } else {
        selector.set_active_array(batch.indices_, batch.row_count_);
      }
      BorrowedIArray<common::ObIVector *> vectors(
          batch.vectors_, batch.vector_count_);
      if (OB_FAIL(writer_->append_current_batch(vectors, selector))) {
      }
    }
    return ret;
  }

  int close() override
  {
    return writer_->close();
  }

  int64_t get_row_count() const override
  {
    return writer_->get_row_count();
  }

  const common::ObTabletID &get_tablet_id() const override
  {
    return writer_->get_tablet_id();
  }

  int64_t get_slice_index() const override
  {
    return writer_->get_slice_idx();
  }

private:
  void destroy_self() override
  {
    common::ObIAllocator *allocator = allocator_;
    this->~ObDirectInsertWriterAdapter();
    allocator->free(this);
  }

private:
  common::ObIAllocator *allocator_;
  storage::ObISliceWriter *writer_;
  DirectInsertWriterImplType type_;
};

class ObDirectInsertSessionImpl final
  : public ObIDirectInsertSession,
    public ObIDirectInsertWriterFactory
{
public:
  enum State
  {
    CREATED = 0,
    RUNNING,
    FINISHED
  };

  explicit ObDirectInsertSessionImpl(common::ObIAllocator &allocator)
    : allocator_(&allocator), dag_(nullptr), thread_pool_(),
      dag_initialized_(false), pool_started_(false), state_(CREATED)
  {}

  ~ObDirectInsertSessionImpl() override
  {
    int ret = finish();
    if (OB_SUCCESS != ret) {
    }
  }

  int start(const ObDirectInsertStartParam &param,
            ObIDirectInsertWorkerContext &worker_context)
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(CREATED != state_)) {
      ret = common::OB_INIT_TWICE;
      LOG_WARN("direct insert session initialized twice", K(ret), K(state_));
    } else if (OB_UNLIKELY(!param.is_valid())) {
      ret = common::OB_INVALID_ARGUMENT;
      LOG_WARN("invalid direct insert start parameter", K(ret),
          K(param.ddl_task_id_), K(param.execution_id_), K(param.table_id_),
          K(param.worker_count_), K(param.participants_.count()));
    } else {
      uint64_t tenant_data_version = 0;
      share::ObDDLTaskDataInfo task_data_info;
      storage::ObDDLInsertDagInitParam dag_param;

      if (OB_FAIL(share::ObDDLUtil::get_data_information(
              *GCTX.sql_proxy_, param.ddl_task_id_, task_data_info))) {
      } else if (FALSE_IT(tenant_data_version = task_data_info.data_format_version_)) {
      } else if (tenant_data_version < storage::DDL_IDEM_DATA_FORMAT_VERSION) {
        ret = common::OB_NOT_SUPPORTED;
        LOG_WARN("direct insert data format is not supported", K(ret),
            K(tenant_data_version));
      } else {
        dag_param.direct_load_type_ =
            storage::ObDirectLoadMgrUtil::ddl_get_direct_load_type();
        dag_param.ddl_thread_count_ = param.worker_count_;
        dag_param.px_thread_count_ = param.worker_count_;
        dag_param.ddl_task_param_.ddl_task_id_ = param.ddl_task_id_;
        dag_param.ddl_task_param_.execution_id_ = param.execution_id_;
        dag_param.ddl_task_param_.data_format_version_ = tenant_data_version;
        dag_param.ddl_task_param_.snapshot_version_ = task_data_info.snapshot_version_;
        dag_param.ddl_task_param_.target_table_id_ = param.table_id_;
        dag_param.ddl_task_param_.schema_version_ = task_data_info.schema_version_;
        dag_param.ddl_task_param_.is_offline_index_rebuild_ =
            task_data_info.is_offline_index_rebuild_;
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(dag_param.tablet_ids_.assign(param.participants_))) {
      } else if (OB_FAIL(share::ObDagScheduler::alloc_dag(
                     *allocator_, false /* is_ha_dag */, dag_))) {
      } else if (OB_FAIL(dag_->init(&dag_param, nullptr, true /* add trace id */))) {
      } else if (FALSE_IT(dag_initialized_ = true)) {
      } else {
        const share::schema::ObIndexType index_type =
            dag_->get_ddl_table_schema().table_item_.index_type_;
        if (share::schema::is_vec_delta_buffer_type(index_type)
            || share::schema::is_hybrid_vec_index_log_type(index_type)
            || share::schema::is_vec_index_id_type(index_type)) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected vector index type for direct insert", K(ret), K(index_type));
        } else if (OB_FAIL(thread_pool_.init(param.worker_count_, dag_, worker_context))) {
        } else if (OB_FAIL(thread_pool_.start())) {
        } else {
          pool_started_ = true;
          dag_->set_start_time();
          state_ = RUNNING;
          FLOG_INFO("started direct insert session", K(param.ddl_task_id_),
              K(param.execution_id_), K(param.table_id_), K(param.worker_count_));
        }
      }
    }
    return ret;
  }

  int finish()
  {
    int ret = common::OB_SUCCESS;
    if (FINISHED != state_) {
      if (nullptr != dag_) {
        if (dag_initialized_) {
          dag_->simply_set_stop();
          if (pool_started_) {
            thread_pool_.stop();
            thread_pool_.wait();
          }
          ret = dag_->get_dag_ret();
        }
        dag_->~ObDDLInsertDag();
        allocator_->free(dag_);
        dag_ = nullptr;
      }
      state_ = FINISHED;
      FLOG_INFO("finished direct insert session", K(ret));
    }
    return ret;
  }

  bool is_final() const override
  {
    return RUNNING != state_ || nullptr == dag_ || dag_->is_final_status();
  }

  int prepare_ordered_input() override
  {
    int ret = check_running();
    if (OB_SUCC(ret) && OB_FAIL(dag_->update_tablet_range_count())) {
      LOG_WARN("prepare ordered direct insert input failed", K(ret));
    }
    return ret;
  }

  int complete_px_worker() override
  {
    int ret = check_running();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(dag_->set_px_finished())) {
    } else if (OB_FAIL(dag_->process())) {
    }

    if (OB_SUCC(ret)) {
      ret = dag_->get_dag_ret();
    } else if (nullptr != dag_ && dag_->is_dag_failed()) {
      const int worker_ret = ret;
      ret = dag_->get_dag_ret();
      LOG_WARN("direct insert dag failed; returning first dag error",
          K(ret), K(worker_ret));
    }
    return ret;
  }

  int resolve_write_policy(const ObDirectInsertPlanFacts &facts,
                           ObDirectInsertWritePolicy &policy) const override
  {
    int ret = check_running();
    if (OB_SUCC(ret)) {
      const bool offline_rebuild =
          dag_->get_ddl_task_param().is_offline_index_rebuild_;
      ObDirectInsertWritePolicy staged;
      staged.vector_generated_id_ = facts.vector_rowkey_vid_;
      staged.idempotent_tablet_autoinc_ = facts.regenerate_heap_table_pk_
          || (facts.vector_rowkey_vid_ && offline_rebuild);
      staged.idempotent_table_autoinc_ = facts.has_table_autoinc_
          && !facts.regenerate_heap_table_pk_;
      staged.idempotent_doc_id_ = facts.rowkey_doc_id_ && offline_rebuild
          && !facts.data_table_without_pk_;
      policy = staged;
    }
    return ret;
  }

  int build_autoinc_param(const ObDirectInsertAutoincScope scope,
                          const common::ObTabletID &tablet_id,
                          const int64_t slice_index,
                          ObDirectInsertAutoincParam &param) override
  {
    int ret = check_running();
    storage::ObDDLTabletContext *tablet_context = nullptr;
    ObDirectInsertAutoincParam staged;
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(!tablet_id.is_valid() || slice_index < 0
               || (DIRECT_INSERT_TABLE_AUTOINC != scope
                   && DIRECT_INSERT_TABLET_AUTOINC != scope))) {
      ret = common::OB_INVALID_ARGUMENT;
      LOG_WARN("invalid direct insert autoinc request", K(ret), K(tablet_id),
          K(slice_index), K(scope));
    } else if (OB_FAIL(dag_->get_tablet_context(tablet_id, tablet_context))) {
    } else {
      staged.enabled_ = true;
      staged.range_interval_ = ObDirectInsertAutoincParam::RANGE_INTERVAL;
      if (DIRECT_INSERT_TABLE_AUTOINC == scope) {
        staged.slice_count_ = dag_->get_total_slice_count();
        staged.slice_index_ = tablet_context->table_slice_offset_ + slice_index;
      } else {
        staged.slice_count_ = tablet_context->slice_count_;
        staged.slice_index_ = slice_index;
      }
      if (OB_UNLIKELY(!staged.is_valid())) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("invalid direct insert autoinc result", K(ret),
            K(staged.slice_count_), K(staged.slice_index_));
      } else {
        param = staged;
      }
    }
    return ret;
  }

  int sync_tablet_autoinc(const common::ObTabletID &tablet_id,
                          const common::ObTabletID &target_tablet_id,
                          const int64_t slice_index,
                          const int64_t row_count) override
  {
    int ret = check_running();
    storage::ObDDLTabletContext *tablet_context = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(!tablet_id.is_valid() || !target_tablet_id.is_valid()
               || slice_index < 0 || row_count < 0)) {
      ret = common::OB_INVALID_ARGUMENT;
      LOG_WARN("invalid direct insert tablet autoinc sync request", K(ret),
          K(tablet_id), K(target_tablet_id), K(slice_index), K(row_count));
    } else if (OB_FAIL(dag_->get_tablet_context(tablet_id, tablet_context))) {
    } else {
      const int64_t last_value = share::ObDDLUtil::generate_idempotent_value(
          tablet_context->slice_count_, slice_index,
          ObDirectInsertAutoincParam::RANGE_INTERVAL, row_count);
      if (OB_FAIL(ObDDLStorageUtil::set_tablet_autoinc_seq(
              target_tablet_id, last_value))) {
      }
    }
    return ret;
  }

  ObIDirectInsertWriterFactory &get_writer_factory() override
  {
    return *this;
  }

  common::ObIAllocator &get_allocator() const
  {
    return *allocator_;
  }

  int create(common::ObIAllocator &allocator,
             const ObDirectInsertWriterRequest &request,
             ObIDirectInsertWriter *&writer) override
  {
    int ret = check_running();
    storage::ObISliceWriter *impl = nullptr;
    DirectInsertWriterImplType impl_type = INVALID_WRITER_IMPL;
    storage::ObWriteMacroParam write_param;
    writer = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(!request.is_valid())) {
      ret = common::OB_INVALID_ARGUMENT;
      LOG_WARN("invalid direct insert writer request", K(ret),
          K(request.tablet_id_), K(request.slice_index_), K(request.layout_),
          K(request.input_format_));
    } else if (OB_FAIL(ObDDLStorageUtil::fill_writer_param(
                   request.tablet_id_, request.slice_index_, dag_,
                   0 /* max_batch_size */, write_param))) {
    } else if (DIRECT_INSERT_HEAP_WRITER == request.layout_
               && DIRECT_INSERT_BATCH_INPUT == request.input_format_) {
      storage::ObHeapBatchSliceWriter *typed_writer = nullptr;
      impl_type = HEAP_BATCH_WRITER_IMPL;
      if (OB_ISNULL(typed_writer = OB_NEWx(storage::ObHeapBatchSliceWriter, &allocator))) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else if (FALSE_IT(impl = typed_writer)) {
      } else if (OB_FAIL(typed_writer->init(
                     write_param, request.parallel_count_,
                     request.autoinc_column_index_, true /* direct write */,
                     request.max_batch_size_, request.idempotent_tablet_autoinc_,
                     *request.spool_factory_))) {
      }
    } else if (DIRECT_INSERT_HEAP_WRITER == request.layout_) {
      storage::ObHeapRsSliceWriter *typed_writer = nullptr;
      impl_type = HEAP_ROW_WRITER_IMPL;
      if (OB_ISNULL(typed_writer = OB_NEWx(storage::ObHeapRsSliceWriter, &allocator))) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else if (FALSE_IT(impl = typed_writer)) {
      } else if (OB_FAIL(typed_writer->init(
                     write_param, request.parallel_count_,
                     request.autoinc_column_index_,
                     request.idempotent_tablet_autoinc_))) {
      }
    } else if (dag_->get_ddl_table_schema().table_item_.vec_dim_ > 0) {
      storage::ObBatchSliceWriter *typed_writer = nullptr;
      impl_type = ORDERED_BATCH_WRITER_IMPL;
      if (OB_ISNULL(request.spool_factory_)) {
        ret = common::OB_INVALID_ARGUMENT;
        LOG_WARN("spill factory is required by vector direct insert writer", K(ret));
      } else if (OB_ISNULL(typed_writer = OB_NEWx(storage::ObBatchSliceWriter, &allocator))) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else if (FALSE_IT(impl = typed_writer)) {
      } else if (OB_FAIL(typed_writer->init(
                     write_param, false /* direct write macro block */,
                     request.append_batch_, 0 /* unused max batch size */,
                     *request.spool_factory_))) {
      }
    } else {
      storage::ObRsSliceWriter *typed_writer = nullptr;
      impl_type = ORDERED_ROW_WRITER_IMPL;
      if (OB_ISNULL(typed_writer = OB_NEWx(storage::ObRsSliceWriter, &allocator))) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else if (FALSE_IT(impl = typed_writer)) {
      } else if (OB_FAIL(typed_writer->init(write_param))) {
      }
    }

    if (OB_SUCC(ret)) {
      ObDirectInsertWriterAdapter *adapter =
          OB_NEWx(ObDirectInsertWriterAdapter, &allocator,
                  allocator, *impl, impl_type);
      if (OB_ISNULL(adapter)) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else {
        writer = adapter;
      }
    }
    if (OB_FAIL(ret)) {
      destroy_writer_impl(allocator, impl, impl_type);
    }
    return ret;
  }

private:
  int check_running() const
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(RUNNING != state_ || nullptr == dag_)) {
      ret = common::OB_NOT_INIT;
      LOG_WARN("direct insert session is not running", K(ret), K(state_), KP(dag_));
    }
    return ret;
  }

private:
  common::ObIAllocator *allocator_;
  storage::ObDDLInsertDag *dag_;
  storage::ObDDLDagThreadPool thread_pool_;
  bool dag_initialized_;
  bool pool_started_;
  State state_;
};

} // namespace

void ObIDirectInsertWriterFactory::destroy(ObIDirectInsertWriter *&writer)
{
  if (nullptr != writer) {
    writer->destroy_self();
    writer = nullptr;
  }
}

int ObDirectInsertOrchestrator::start(
    common::ObIAllocator &allocator,
    const ObDirectInsertStartParam &param,
    ObIDirectInsertWorkerContext &worker_context,
    ObIDirectInsertSession *&session)
{
  int ret = common::OB_SUCCESS;
  session = nullptr;
  ObDirectInsertSessionImpl *impl = nullptr;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("invalid direct insert start parameter", K(ret));
  } else if (OB_ISNULL(impl = OB_NEWx(ObDirectInsertSessionImpl, &allocator,
                                     allocator))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate direct insert session failed", K(ret));
  } else if (OB_FAIL(impl->start(param, worker_context))) {
  } else {
    session = impl;
  }

  if (OB_FAIL(ret) && nullptr != impl) {
    const int cleanup_ret = impl->finish();
    if (common::OB_SUCCESS != cleanup_ret) {
    }
    impl->~ObDirectInsertSessionImpl();
    allocator.free(impl);
  }
  return ret;
}

int ObDirectInsertOrchestrator::finish(ObIDirectInsertSession *&session)
{
  int ret = common::OB_SUCCESS;
  if (nullptr != session) {
    ObDirectInsertSessionImpl *impl =
        static_cast<ObDirectInsertSessionImpl *>(session);
    common::ObIAllocator &allocator = impl->get_allocator();
    ret = impl->finish();
    impl->~ObDirectInsertSessionImpl();
    allocator.free(impl);
    session = nullptr;
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
