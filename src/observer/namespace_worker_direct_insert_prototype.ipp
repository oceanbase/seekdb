// Native direct-insert DAGs and slice writers stay beside shared tablets.
#include "data_plane/ddl/ob_direct_insert.h"
#include "data_plane/ddl/ob_ddl_schedule.h"
#include "sql/engine/basic/ob_temp_column_spill_spool.h"
#include "query/engine/vector/ob_i_vector.h"
#include "share/ob_ddl_checksum.h"
#include <map>
#include <mutex>
#include <shared_mutex>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace data_plane;

int route_direct_insert_schema(
    uint64_t ns,
    const common::ObString &logical_bytes,
    uint64_t expected_table_id,
    common::ObIAllocator &allocator,
    common::ObString &storage_bytes)
{
  storage_bytes.reset();
  if (logical_bytes.empty()) {
    return OB_SUCCESS;
  }
  if (ns <= 1) {
    storage_bytes = logical_bytes;
    return OB_SUCCESS;
  }

  share::schema::ObTableSchema logical_schema(&allocator);
  share::schema::ObTableSchema storage_schema(&allocator);
  int64_t pos = 0;
  int ret = logical_schema.deserialize(
      logical_bytes.ptr(), logical_bytes.length(), pos);
  if (OB_SUCC(ret) && pos != logical_bytes.length()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(ret) && expected_table_id != 0
             && logical_schema.get_table_id() != expected_table_id) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(ret)) {
    ret = storage::NamespaceForkKernelPrototype::make_storage_schema(
        ns, logical_schema, storage_schema);
  }
  const int64_t size = OB_SUCC(ret) ? storage_schema.get_serialize_size() : 0;
  char *buffer = OB_SUCC(ret)
      ? static_cast<char *>(allocator.alloc(size)) : nullptr;
  pos = 0;
  if (OB_SUCC(ret) && OB_ISNULL(buffer)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_SUCC(ret)
             && OB_FAIL(storage_schema.serialize(buffer, size, pos))) {
  } else if (OB_SUCC(ret) && pos != size) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_SUCC(ret)) {
    storage_bytes.assign_ptr(buffer, static_cast<int32_t>(pos));
  }
  return ret;
}

struct StorageSessionState {
  ObArenaAllocator allocator{ObMemAttr("NsStorageSess")};
  sql::ObSQLSessionInfo session;
};

struct DirectInsertOwner final : ObIDirectInsertWorkerContext {
  ObArenaAllocator allocator{ObMemAttr("NsDirectInsert")};
  std::shared_ptr<StorageSessionState> context;
  ObIDirectInsertSession *session = nullptr;
  RequestTag origin;
  uint64_t namespace_id;
  uint64_t generation;
  int64_t deadline;
  std::shared_mutex mutex;
  std::atomic<int64_t> writers{0};
  DirectInsertOwner(std::shared_ptr<StorageSessionState> state, RequestTag tag,
                    uint64_t ns, uint64_t id)
      : context(std::move(state)), origin(tag), namespace_id(ns),
        generation(id), deadline(THIS_WORKER.get_timeout_ts()) {}
  ~DirectInsertOwner() { ObDirectInsertOrchestrator::finish(session); }
  void bind_current_thread() override {
    // Pin the existing storage session, without retaining its route or channel.
    THIS_WORKER.set_session(&context->session);
    THIS_WORKER.set_timeout_ts(deadline);
  }
  int report_ddl_checksum(
      uint64_t data_format_version,
      int64_t execution_id,
      int64_t ddl_task_id,
      uint64_t table_id,
      const ObTabletID &tablet_id,
      const ObIArray<uint64_t> &column_ids,
      const ObIArray<int64_t> &column_checksums) override {
    uint64_t logical_table_id = table_id;
    uint64_t logical_tablet_id = tablet_id.id();
    int ret = column_ids.count() <= 0
            || column_ids.count() != column_checksums.count()
        ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    if (OB_SUCC(ret) && namespace_id > 1) {
      if (OB_FAIL(storage::NamespaceForkKernelPrototype::local_object_id(
              namespace_id, table_id, logical_table_id))) {
      } else if (OB_FAIL(storage::NamespaceForkKernelPrototype::local_object_id(
                     namespace_id, tablet_id.id(), logical_tablet_id))) {
      }
    }
    ObArray<share::ObDDLChecksumItem> items;
    for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
      share::ObDDLChecksumItem item;
      item.execution_id_ = execution_id;
      item.table_id_ = logical_table_id;
      item.tablet_id_ = logical_tablet_id;
      item.ddl_task_id_ = ddl_task_id;
      item.column_id_ = column_ids.at(i);
      item.task_id_ = logical_tablet_id;
      item.checksum_ = column_checksums.at(i);
      ret = items.push_back(item);
    }
    if (OB_SUCC(ret) && OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_NOT_INIT;
    } else if (OB_SUCC(ret)) {
      TargetSqlProxy target_sql(namespace_id);
      if (OB_SUCC(ret = target_sql.init(false))) {
        ret = share::ObDDLChecksumOperator::update_checksum(
            data_format_version, items, target_sql);
      }
    }
    fprintf(stderr,
        "PROTOTYPE_NAMESPACE_DDL_CHECKSUM ns=%llu table=%llu tablet=%llu count=%lld ret=%d\n",
        static_cast<unsigned long long>(namespace_id),
        static_cast<unsigned long long>(logical_table_id),
        static_cast<unsigned long long>(logical_tablet_id),
        static_cast<long long>(items.count()), ret);
    return ret;
  }
  bool matches(RequestTag tag, uint64_t id) const {
    return origin.slot == tag.slot && origin.generation == tag.generation && generation == id;
  }
};

class DirectInsertRegistry final {
public:
  RequestTag acquire() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (next_slot_ == UINT64_MAX) { return {}; }
    const RequestTag tag{++next_slot_, 1};
    entries_.emplace(tag.slot, Entry{});
    return tag;
  }
  int attach(RequestTag tag, const std::shared_ptr<DirectInsertOwner> &owner) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto entry = entries_.find(tag.slot);
    if (tag.generation != 1 || entry == entries_.end() || !owner) {
      return OB_STATE_NOT_MATCH;
    }
    entry->second.owner = owner;
    return OB_SUCCESS;
  }
  std::shared_ptr<DirectInsertOwner> find(RequestTag tag) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto entry = entries_.find(tag.slot);
    return tag.generation == 1 && entry != entries_.end()
        ? entry->second.owner.lock() : nullptr;
  }
  void clear(RequestTag tag) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto entry = entries_.find(tag.slot);
    if (tag.generation == 1 && entry != entries_.end()) {
      entry->second.owner.reset();
    }
  }
  void release(RequestTag tag) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (tag.generation == 1) { entries_.erase(tag.slot); }
  }
private:
  struct Entry { std::weak_ptr<DirectInsertOwner> owner; };
  std::mutex mutex_;
  std::map<uint64_t, Entry> entries_;
  uint64_t next_slot_ = 0;
};

struct DirectInsertWriterOwner {
  ObArenaAllocator allocator{ObMemAttr("NsDirectWriter")};
  std::shared_ptr<DirectInsertOwner> owner;
  ObIDirectInsertWriter *writer = nullptr;
  explicit DirectInsertWriterOwner(std::shared_ptr<DirectInsertOwner> session) : owner(std::move(session)) {
    ++owner->writers;
  }
  ~DirectInsertWriterOwner() {
    ObIDirectInsertWriterFactory::destroy(writer);
    --owner->writers;
  }
};

struct DirectInsertRoute {
  std::shared_ptr<DirectInsertOwner> owner;
  std::map<uint64_t, std::unique_ptr<DirectInsertWriterOwner>> writers;
  uint64_t session_generation = 0, writer_generation = 0;
  void reset() { writers.clear(); owner.reset(); }
  int resolve(RequestTag parent, uint64_t generation, DirectInsertRegistry &registry) {
    if (!owner || !owner->matches(parent, generation)) {
      if (!writers.empty()) { return OB_STATE_NOT_MATCH; }
      owner.reset();
      owner = registry.find(parent);
      if (!owner || !owner->matches(parent, generation)) {
        owner.reset();
        return OB_STATE_NOT_MATCH;
      }
    }
    return OB_SUCCESS;
  }
  int simple(StorageSpaceHandle storage_space, RequestTag parent, uint64_t generation,
             DirectInsertRegistry &registry, char operation, bool &is_final) {
    is_final = false;
    if (!storage_space.is_namespace() || (operation != 'I' && operation != 'C')) {
      return OB_INVALID_ARGUMENT;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    ObIDirectInsertSession *session = owner->session;
    if (!session) { return OB_NOT_INIT; }
    if (operation == 'I') { is_final = session->is_final(); }
    else {
      ObIDirectInsertWorkerContext *previous_context =
          set_current_direct_insert_worker_context(owner.get());
      ret = session->complete_px_worker();
      set_current_direct_insert_worker_context(previous_context);
    }
    fprintf(stderr, "PROTOTYPE_DIRECT_INSERT_SIMPLE ns=%llu op=%c ret=%d final=%d\n",
        static_cast<unsigned long long>(storage_space.namespace_id()), operation, ret, is_final);
    return ret;
  }
  int resolve_policy(StorageSpaceHandle storage_space, RequestTag parent,
                     uint64_t generation, DirectInsertRegistry &registry,
                     const ObDirectInsertPlanFacts &facts,
                     ObDirectInsertWritePolicy &policy) {
    if (!storage_space.is_namespace()) { return OB_INVALID_ARGUMENT; }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    ret = owner->session ? owner->session->resolve_write_policy(facts, policy) : OB_NOT_INIT;
    fprintf(stderr, "PROTOTYPE_DIRECT_INSERT_POLICY ns=%llu ret=%d\n",
        static_cast<unsigned long long>(storage_space.namespace_id()), ret);
    return ret;
  }
  int build_autoinc(StorageSpaceHandle storage_space, RequestTag parent,
                    uint64_t generation, DirectInsertRegistry &registry,
                    ObDirectInsertAutoincScope scope, const ObTabletID &logical_tablet,
                    int64_t slice, ObDirectInsertAutoincParam &param) {
    if (!storage_space.is_namespace() || scope > DIRECT_INSERT_TABLET_AUTOINC) {
      return OB_INVALID_ARGUMENT;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    if (!owner->session) { return OB_NOT_INIT; }
    ObTabletID tablet = logical_tablet;
    const uint64_t ns = storage_space.namespace_id();
    if (ns > 1) { ret = route_tablet_id(ns, tablet); }
    if (!ret) { ret = owner->session->build_autoinc_param(scope, tablet, slice, param); }
    return ret ? ret : param.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  int sync_autoinc(StorageSpaceHandle storage_space, RequestTag parent,
                   uint64_t generation, DirectInsertRegistry &registry,
                   const ObTabletID &logical_tablet, const ObTabletID &logical_target,
                   int64_t slice, int64_t rows) {
    if (!storage_space.is_namespace()) { return OB_INVALID_ARGUMENT; }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    if (!owner->session) { return OB_NOT_INIT; }
    ObTabletID tablet = logical_tablet, target = logical_target;
    const uint64_t ns = storage_space.namespace_id();
    if (ns > 1 && OB_FAIL(route_tablet_id(ns, tablet))) {
    } else if (ns > 1 && OB_FAIL(route_tablet_id(ns, target))) {
    } else {
      ret = owner->session->sync_tablet_autoinc(tablet, target, slice, rows);
    }
    return ret;
  }
  int prepare_ordered(StorageSpaceHandle storage_space, RequestTag parent,
                      uint64_t generation, DirectInsertRegistry &registry,
                      const ObIArray<ObDDLTabletSliceCount> &logical_counts) {
    if (!storage_space.is_namespace() || logical_counts.count() <= 0
        || logical_counts.count() > (MAX_SQL_MESSAGE - 64) / 16) {
      return OB_INVALID_ARGUMENT;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    if (!owner->session) { return OB_NOT_INIT; }
    ObArray<ObDDLTabletSliceCount> routed;
    const uint64_t ns = storage_space.namespace_id();
    for (int64_t i = 0; !ret && i < logical_counts.count(); ++i) {
      const ObDDLTabletSliceCount &entry = logical_counts.at(i);
      if (entry.tablet_id_ < 0 || entry.slice_count_ <= 0) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        uint64_t tablet_id = static_cast<uint64_t>(entry.tablet_id_);
        if (tablet_id != 0 && ns > 1) {
          ret = storage::NamespaceForkKernelPrototype::storage_object_id(ns, tablet_id, tablet_id);
        }
        if (!ret && tablet_id > static_cast<uint64_t>(INT64_MAX)) { ret = OB_SIZE_OVERFLOW; }
        if (!ret) { ret = routed.push_back(ObDDLTabletSliceCount(
            static_cast<int64_t>(tablet_id), entry.slice_count_)); }
      }
    }
    if (!ret) { ret = owner->session->prepare_ordered_input(routed); }
    return ret;
  }
  int finish(StorageSpaceHandle storage_space, RequestTag tag, RequestTag parent,
             uint64_t generation, DirectInsertRegistry &registry) {
    if (!storage_space.is_namespace() || parent.slot != tag.slot
        || parent.generation != tag.generation) {
      return OB_INVALID_ARGUMENT;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    {
      std::unique_lock<std::shared_mutex> guard(owner->mutex);
      ret = owner->writers ? OB_STATE_NOT_MATCH : ObDirectInsertOrchestrator::finish(owner->session);
    }
    if (!owner->session) {
      registry.clear(tag);
      owner.reset();
    }
    return ret;
  }
  int create_writer(StorageSpaceHandle storage_space, RequestTag parent,
                    uint64_t generation, DirectInsertRegistry &registry,
                    const ObDirectInsertWriterRequest &logical_request,
                    uint64_t &writer_id) {
    writer_id = 0;
    if (!storage_space.is_namespace() || !logical_request.is_valid()
        || logical_request.layout_ > DIRECT_INSERT_ORDERED_WRITER
        || writer_generation == UINT64_MAX) {
      return OB_INVALID_ARGUMENT;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    if (!owner->session) { return OB_NOT_INIT; }
    ObDirectInsertWriterRequest request = logical_request;
    request.spool_factory_ = &sql::get_temp_column_spill_spool_factory();
    const uint64_t ns = storage_space.namespace_id();
    if (ns > 1) { ret = route_tablet_id(ns, request.tablet_id_); }
    auto staged = ret ? nullptr : std::make_unique<DirectInsertWriterOwner>(owner);
    if (!ret) {
      ret = owner->session->get_writer_factory().create(
          staged->allocator, request, staged->writer);
    }
    if (!ret) {
      writer_id = ++writer_generation;
      writers.emplace(writer_id, std::move(staged));
    }
    return ret;
  }
  int control_writer(StorageSpaceHandle storage_space, RequestTag parent,
                     uint64_t generation, DirectInsertRegistry &registry,
                     uint64_t writer_id, char operation, int64_t &rows) {
    rows = 0;
    if (!storage_space.is_namespace() || (operation != 'E' && operation != 'X')) {
      return OB_INVALID_ARGUMENT;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    if (!owner->session) { return OB_NOT_INIT; }
    auto entry = writers.find(writer_id);
    if (entry == writers.end()) { return OB_STATE_NOT_MATCH; }
    if (operation == 'E') {
      ret = entry->second->writer->close();
      if (!ret) {
        rows = entry->second->writer->get_row_count();
        if (rows < 0) { ret = OB_INVALID_ARGUMENT; }
      }
    } else {
      writers.erase(entry);
    }
    return ret;
  }
  int append_writer(StorageSpaceHandle storage_space, RequestTag parent,
                    uint64_t generation, DirectInsertRegistry &registry,
                    uint64_t writer_id, ObDatum *cells,
                    int64_t row_count, int64_t column_count, int64_t &rows) {
    rows = 0;
    if (!storage_space.is_namespace() || !cells || row_count <= 0 || row_count > 32
        || column_count <= 0 || column_count > OB_MAX_COLUMN_NUMBER) {
      return OB_INVALID_ARGUMENT;
    }
    int64_t payload_size = 121;
    for (int64_t i = 0; i < row_count * column_count; ++i) {
      const int64_t cell_size = cells[i].get_serialize_size();
      if (cell_size < 0 || cell_size > static_cast<int64_t>(MAX_SQL_MESSAGE) - payload_size) {
        return OB_SIZE_OVERFLOW;
      }
      payload_size += cell_size;
    }
    int ret = resolve(parent, generation, registry);
    if (ret) { return ret; }
    std::shared_lock<std::shared_mutex> guard(owner->mutex);
    if (!owner->session) { return OB_NOT_INIT; }
    auto entry = writers.find(writer_id);
    if (entry == writers.end()) { return OB_STATE_NOT_MATCH; }
    std::vector<ObDatum *> row(column_count);
    for (int64_t i = 0; !ret && i < row_count; ++i) {
      for (int64_t j = 0; j < column_count; ++j) {
        row[j] = &cells[i * column_count + j];
      }
      ret = entry->second->writer->append_row(ObDirectInsertRowView(row.data(), column_count));
    }
    if (!ret) {
      rows = entry->second->writer->get_row_count();
      if (rows < 0) { ret = OB_INVALID_ARGUMENT; }
    }
    return ret;
  }

  int start(StorageSpaceHandle storage_space, RequestTag tag, DirectInsertRegistry &registry,
            const std::shared_ptr<StorageSessionState> &context,
            const ObDirectInsertStartParam &logical, RequestTag &origin,
            uint64_t &generation) {
    origin = {};
    generation = 0;
    if (!storage_space.is_namespace() || !logical.is_valid() || owner
        || session_generation == UINT64_MAX
        || logical.participants_.count() > MAX_FRAME / 8) {
      return OB_INVALID_ARGUMENT;
    }
    if (!tag.slot) { return OB_STATE_NOT_MATCH; }
    size_t request_size = 137 + 8 * logical.participants_.count();
    const ObString schemas[] = {logical.table_schema_, logical.lob_meta_table_schema_,
        logical.vector_data_table_schema_, logical.vector_param_table_schema_};
    for (const ObString &schema : schemas) {
      if (schema.length() < 0 || schema.length() > MAX_SQL_MESSAGE - request_size) {
        return OB_SIZE_OVERFLOW;
      }
      request_size += schema.length();
    }
    ObArenaAllocator route_allocator{ObMemAttr("NsDirectRoute")};
    ObDirectInsertStartParam param;
    param.ddl_task_id_ = logical.ddl_task_id_;
    param.execution_id_ = logical.execution_id_;
    param.table_id_ = logical.table_id_;
    param.worker_count_ = logical.worker_count_;
    param.data_format_version_ = logical.data_format_version_;
    param.snapshot_version_ = logical.snapshot_version_;
    param.schema_version_ = logical.schema_version_;
    param.is_offline_index_rebuild_ = logical.is_offline_index_rebuild_;
    param.table_schema_ = logical.table_schema_;
    param.lob_meta_table_schema_ = logical.lob_meta_table_schema_;
    param.vector_data_table_schema_ = logical.vector_data_table_schema_;
    param.vector_param_table_schema_ = logical.vector_param_table_schema_;
    int ret = OB_SUCCESS;
    for (int64_t i = 0; !ret && i < logical.participants_.count(); ++i) {
      const ObTabletID tablet = logical.participants_.at(i);
      ret = tablet.is_valid() ? param.participants_.push_back(tablet) : OB_INVALID_ARGUMENT;
    }
    const uint64_t ns = storage_space.namespace_id();
    if (!ret && ns > 1) {
      const uint64_t logical_table_id = static_cast<uint64_t>(param.table_id_);
      uint64_t storage_table_id = OB_INVALID_ID;
      if (OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
              ns, logical_table_id, storage_table_id))) {
      } else if (storage_table_id > static_cast<uint64_t>(INT64_MAX)) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        param.table_id_ = static_cast<int64_t>(storage_table_id);
      }
      for (int64_t i = 0; !ret && i < param.participants_.count(); ++i) {
        ret = route_tablet_id(ns, param.participants_.at(i));
      }
      if (!ret) { ret = route_direct_insert_schema(ns, logical.table_schema_,
          logical_table_id, route_allocator, param.table_schema_); }
      if (!ret) { ret = route_direct_insert_schema(ns, logical.lob_meta_table_schema_,
          0, route_allocator, param.lob_meta_table_schema_); }
      if (!ret) { ret = route_direct_insert_schema(ns, logical.vector_data_table_schema_,
          0, route_allocator, param.vector_data_table_schema_); }
      if (!ret) { ret = route_direct_insert_schema(ns, logical.vector_param_table_schema_,
          0, route_allocator, param.vector_param_table_schema_); }
    }
    if (!ret) {
      auto staged = std::make_shared<DirectInsertOwner>(
          context, tag, ns, ++session_generation);
      ret = ObDirectInsertOrchestrator::start(
          staged->allocator, param, *staged, staged->session);
      fprintf(stderr,
          "PROTOTYPE_V22_DIRECT_INSERT_SHARED ret=%d ns=%llu task=%ld table=%ld format=%llu snapshot=%ld schema=%ld participants=%ld\n",
          ret, (unsigned long long)ns, param.ddl_task_id_, param.table_id_,
          (unsigned long long)param.data_format_version_, param.snapshot_version_,
          param.schema_version_, param.participants_.count());
      if (!ret) {
        ret = registry.attach(tag, staged);
        if (!ret) {
          owner = std::move(staged);
          origin = tag;
          generation = owner->generation;
        }
      }
    }
    return ret;
  }
};
int call_in_process_direct_insert_simple(RequestTag parent, uint64_t generation,
                                         char operation, bool &is_final);
int resolve_in_process_direct_insert_policy(RequestTag parent, uint64_t generation,
                                            const ObDirectInsertPlanFacts &facts,
                                            ObDirectInsertWritePolicy &policy);
int build_in_process_direct_insert_autoinc(RequestTag parent, uint64_t generation,
                                           ObDirectInsertAutoincScope scope,
                                           const ObTabletID &tablet, int64_t slice,
                                           ObDirectInsertAutoincParam &param);
int sync_in_process_direct_insert_autoinc(RequestTag parent, uint64_t generation,
                                          const ObTabletID &tablet,
                                          const ObTabletID &target,
                                          int64_t slice, int64_t rows);
int prepare_in_process_direct_insert_ordered(RequestTag parent, uint64_t generation,
    const ObIArray<ObDDLTabletSliceCount> &slice_counts);
int finish_in_process_direct_insert(RequestTag parent, uint64_t generation);
int create_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    const ObDirectInsertWriterRequest &request, uint64_t &writer_id);
int control_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    uint64_t writer_id, char operation, int64_t &rows);
int append_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    uint64_t writer_id, ObDatum *cells, int64_t row_count,
    int64_t column_count, int64_t &rows);
int start_in_process_direct_insert(const ObDirectInsertStartParam &param,
    RequestTag &origin, uint64_t &generation);

class RemoteDirectInsertSession final : public ObIDirectInsertSession, public ObIDirectInsertWriterFactory {
public:
  ObIAllocator &allocator;
  DirectInsertScheduleRegistry &schedule_registry;
  int64_t ddl_task_id;
  std::shared_ptr<DirectInsertSchedule> schedule;
  sql::ObSQLSessionInfo *sqc_session;
  RequestTag origin;
  uint64_t generation;
  mutable std::atomic<int> error{OB_SUCCESS};
  RemoteDirectInsertSession(ObIAllocator &a,
      DirectInsertScheduleRegistry &registry, int64_t task_id,
      std::shared_ptr<DirectInsertSchedule> task_schedule,
      sql::ObSQLSessionInfo *session, RequestTag tag, uint64_t id)
      : allocator(a), schedule_registry(registry), ddl_task_id(task_id),
        schedule(std::move(task_schedule)), sqc_session(session), origin(tag),
        generation(id) {}
  int simple_call(char operation, bool &is_final) const {
    StorageSessionScope scope(THIS_WORKER.get_session());
    int ret = scope.error() ? scope.error() : error.load();
    if (!ret) { ret = call_in_process_direct_insert_simple(origin, generation, operation, is_final); }
    if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  bool is_final() const override {
    bool final = false;
    return !simple_call('I', final) && final;
  }
  int prepare_ordered_input(
      const common::ObIArray<ObDDLTabletSliceCount> &slice_counts) override {
    StorageSessionScope binding(THIS_WORKER.get_session());
    int ret = binding.error() ? binding.error() : error.load();
    if (!ret) { ret = prepare_in_process_direct_insert_ordered(origin, generation, slice_counts); }
    if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  int prepare_ordered_input() override {
    int ret = OB_SUCCESS;
    std::vector<ObDDLTabletSliceCount> snapshot;
    ObArray<ObDDLTabletSliceCount> slice_counts;
    if (!schedule) { ret = OB_NOT_INIT; }
    if (!ret) { ret = schedule->snapshot(snapshot); }
    if (!ret) { ret = slice_counts.reserve(snapshot.size()); }
    for (size_t i = 0; !ret && i < snapshot.size(); ++i) {
      ret = slice_counts.push_back(snapshot[i]);
    }
    if (!ret) { ret = prepare_ordered_input(slice_counts); }
    return ret;
  }
  int complete_px_worker() override {
    bool unused = false;
    return simple_call('C', unused);
  }
  int resolve_write_policy(const ObDirectInsertPlanFacts &facts, ObDirectInsertWritePolicy &policy) const override {
    StorageSessionScope scope(THIS_WORKER.get_session());
    int ret = scope.error() ? scope.error() : error.load();
    if (!ret) { ret = resolve_in_process_direct_insert_policy(origin, generation, facts, policy); }
    if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  int build_autoinc_param(ObDirectInsertAutoincScope scope, const ObTabletID &tablet,
      int64_t slice, ObDirectInsertAutoincParam &param) override {
    StorageSessionScope binding(THIS_WORKER.get_session());
    int ret = binding.error() ? binding.error() : error.load();
    if (!ret) { ret = build_in_process_direct_insert_autoinc(
        origin, generation, scope, tablet, slice, param); }
    if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  int sync_tablet_autoinc(const ObTabletID &tablet, const ObTabletID &target, int64_t slice, int64_t rows) override {
    StorageSessionScope binding(THIS_WORKER.get_session());
    int ret = binding.error() ? binding.error() : error.load();
    if (!ret) { ret = sync_in_process_direct_insert_autoinc(
        origin, generation, tablet, target, slice, rows); }
    if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  ObIDirectInsertWriterFactory &get_writer_factory() override { return *this; }
  int create(ObIAllocator &, const ObDirectInsertWriterRequest &, ObIDirectInsertWriter *&) override;
private:
  int finish_and_destroy() override {
    StorageSessionScope binding(sqc_session);
    int ret = binding.error() ? binding.error()
        : finish_in_process_direct_insert(origin, generation);
    if (error) { ret = error; }
    schedule_registry.release(ddl_task_id, schedule);
    auto &a = allocator; this->~RemoteDirectInsertSession(); a.free(this);
    return ret;
  }
};

class RemoteDirectInsertWriter final : public ObIDirectInsertWriter {
public:
  ObIAllocator &allocator;
  RemoteDirectInsertSession &session;
  sql::ObSQLSessionInfo *sql_session;
  uint64_t id;
  ObTabletID tablet;
  int64_t slice, rows = 0;
  RemoteDirectInsertWriter(ObIAllocator &a, RemoteDirectInsertSession &s, uint64_t handle,
      const ObDirectInsertWriterRequest &request)
      : allocator(a), session(s), sql_session(THIS_WORKER.get_session()), id(handle),
        tablet(request.tablet_id_), slice(request.slice_index_) {}
  int send_rows(ObDatum *cells, int64_t row_count, int64_t column_count) {
    StorageSessionScope binding(sql_session);
    int ret = binding.error() ? binding.error() : session.error.load();
    int64_t new_rows = 0;
    if (!ret) { ret = append_in_process_direct_insert_writer(
        session.origin, session.generation, id, cells, row_count, column_count, new_rows); }
    if (!ret) { rows = new_rows; }
    if (ret) { int expected = OB_SUCCESS; session.error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  int append_row(const ObDirectInsertRowView &row) override {
    if (!row.is_valid() || row.datum_count_ > OB_MAX_COLUMN_NUMBER) { return OB_INVALID_ARGUMENT; }
    std::vector<ObDatum> cells(row.datum_count_);
    for (int64_t i = 0; i < row.datum_count_; ++i) {
      if (!row.datums_[i]) { return OB_INVALID_ARGUMENT; }
      cells[i] = *row.datums_[i];
    }
    return send_rows(cells.data(), 1, row.datum_count_);
  }
  int append_batch(const ObDirectInsertBatchView &batch) override {
    if (!batch.is_valid() || batch.vector_count_ > OB_MAX_COLUMN_NUMBER) { return OB_INVALID_ARGUMENT; }
    int ret = OB_SUCCESS;
    for (int64_t first = 0; !ret && first < batch.row_count_; first += 32) {
      const int64_t count = std::min<int64_t>(32, batch.row_count_ - first);
      std::vector<ObDatum> cells(count * batch.vector_count_);
      std::vector<std::string> payloads(count * batch.vector_count_);
      for (int64_t i = first; i < first + count; ++i) {
        const int64_t index = batch.selection_type_ == ObDirectInsertBatchView::CONTIGUOUS_SELECTION
            ? batch.offset_ + i : batch.indices_[i];
        for (int64_t col = 0; col < batch.vector_count_; ++col) {
          if (!batch.vectors_[col]) { return OB_INVALID_ARGUMENT; }
          bool is_null = false; const char *value = nullptr; ObLength length = 0;
          batch.vectors_[col]->get_payload(index, is_null, value, length);
          const int64_t cell_index = (i - first) * batch.vector_count_ + col;
          ObDatum &datum = cells[cell_index];
          if (is_null) {
            datum.set_null();
          } else if (length > MAX_SQL_MESSAGE) {
            return OB_SIZE_OVERFLOW;
          } else if (length > 0 && !value) {
            return OB_INVALID_ARGUMENT;
          } else {
            payloads[cell_index].assign(value ? value : "", length);
            datum.set_string(payloads[cell_index].data(), length);
          }
        }
      }
      if (!ret) { ret = send_rows(cells.data(), count, batch.vector_count_); }
    }
    return ret;
  }
  int close() override {
    StorageSessionScope binding(sql_session);
    int ret = binding.error() ? binding.error() : session.error.load();
    int64_t new_rows = 0;
    if (!ret) { ret = control_in_process_direct_insert_writer(
        session.origin, session.generation, id, 'E', new_rows); }
    if (!ret) { rows = new_rows; }
    if (ret) { int expected = OB_SUCCESS; session.error.compare_exchange_strong(expected, ret); }
    return ret;
  }
  int64_t get_row_count() const override { return rows; }
  const ObTabletID &get_tablet_id() const override { return tablet; }
  int64_t get_slice_index() const override { return slice; }
private:
  void destroy_self() override {
    StorageSessionScope binding(sql_session);
    int64_t unused_rows = 0;
    const int ret = binding.error() ? binding.error()
        : control_in_process_direct_insert_writer(
            session.origin, session.generation, id, 'X', unused_rows);
    if (ret) { int expected = OB_SUCCESS; session.error.compare_exchange_strong(expected, ret); }
    auto &a = allocator; this->~RemoteDirectInsertWriter(); a.free(this);
  }
};
int RemoteDirectInsertSession::create(ObIAllocator &a, const ObDirectInsertWriterRequest &request,
    ObIDirectInsertWriter *&writer) {
  writer = nullptr;
  if (!request.is_valid()) { return OB_INVALID_ARGUMENT; }
  auto *storage = a.alloc(sizeof(RemoteDirectInsertWriter));
  if (!storage) { return OB_ALLOCATE_MEMORY_FAILED; }
  StorageSessionScope binding(THIS_WORKER.get_session());
  int ret = binding.error() ? binding.error() : error.load();
  uint64_t id = 0;
  if (!ret) { ret = create_in_process_direct_insert_writer(origin, generation, request, id); }
  if (!ret && !id) { ret = OB_INVALID_ARGUMENT; }
  if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
  if (!ret) { writer = new (storage) RemoteDirectInsertWriter(a, *this, id, request); }
  else { a.free(storage); }
  return ret;
}

class RemoteDirectInsertService final : public IDirectInsertService {
public:
  int start(ObIAllocator &allocator, const ObDirectInsertStartParam &param,
      ObIDirectInsertWorkerContext &context, ObIDirectInsertSession *&session) override {
    session = nullptr;
    if (!param.is_valid()) { return OB_INVALID_ARGUMENT; }
    std::shared_ptr<DirectInsertSchedule> schedule =
        schedules.acquire(param.ddl_task_id_);
    if (!schedule) { return OB_ALLOCATE_MEMORY_FAILED; }
    auto *memory = allocator.alloc(sizeof(RemoteDirectInsertSession));
    if (!memory) {
      schedules.release(param.ddl_task_id_, schedule);
      return OB_ALLOCATE_MEMORY_FAILED;
    }
    auto *previous = THIS_WORKER.get_session();
    context.bind_current_thread();
    auto *sqc_session = THIS_WORKER.get_session();
    StorageSessionScope scope(sqc_session);
    RequestTag origin;
    uint64_t generation = 0;
    int ret = scope.error();
    if (!ret) { ret = start_in_process_direct_insert(param, origin, generation); }
    fprintf(stderr,
            "PROTOTYPE_V22_DIRECT_INSERT_WORKER ret=%d task=%ld table=%ld format=%llu snapshot=%ld schema=%ld participants=%ld\n",
            ret, param.ddl_task_id_, param.table_id_,
            (unsigned long long)param.data_format_version_, param.snapshot_version_,
            param.schema_version_, param.participants_.count());
    if (!ret) {
      if (!generation || !origin.slot || !origin.generation) { ret = OB_INVALID_ARGUMENT; }
      else { session = new (memory) RemoteDirectInsertSession(
          allocator, schedules, param.ddl_task_id_, schedule,
          sqc_session, origin, generation); }
    }
    THIS_WORKER.set_session(previous);
    if (ret) {
      schedules.release(param.ddl_task_id_, schedule);
      allocator.free(memory);
    }
    return ret;
  }
  int publish_ordered_input(
      int64_t task_id,
      const common::ObIArray<ObDDLTabletSliceCount> &slice_counts) override {
    return schedules.publish(task_id, slice_counts);
  }
private:
  DirectInsertScheduleRegistry schedules;
};
} } }
