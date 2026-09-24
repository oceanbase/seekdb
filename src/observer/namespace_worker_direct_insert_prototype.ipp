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

  int process(StorageSpaceHandle storage_space, RequestTag tag, DirectInsertRegistry &registry,
      const std::shared_ptr<StorageSessionState> &context, Frame &request, Frame &reply) {
    const uint64_t ns = storage_space.namespace_id();
    const uint64_t operation = request.number();
    const RequestTag parent{request.number(), request.number()};
    const uint64_t generation = request.number();
    int ret = request.ret ? request.ret
        : !storage_space.is_namespace() ? OB_INVALID_ARGUMENT : OB_SUCCESS;
    const char *failure_stage = ret ? "header" : "dispatch";
    Frame output;
    if (!ret && operation == 'S') {
      failure_stage = "decode_start";
      ObArenaAllocator route_allocator{ObMemAttr("NsDirectRoute")};
      ObDirectInsertStartParam param;
      param.ddl_task_id_ = request.number(); param.execution_id_ = request.number();
      param.table_id_ = request.number(); param.worker_count_ = request.number();
      param.data_format_version_ = request.number();
      param.snapshot_version_ = request.number(); param.schema_version_ = request.number();
      const uint64_t offline_rebuild = request.number();
      param.is_offline_index_rebuild_ = offline_rebuild;
      const ObString logical_table_schema = request.string();
      const ObString logical_lob_meta_schema = request.string();
      const ObString logical_vector_data_schema = request.string();
      const ObString logical_vector_param_schema = request.string();
      param.table_schema_ = logical_table_schema;
      param.lob_meta_table_schema_ = logical_lob_meta_schema;
      param.vector_data_table_schema_ = logical_vector_data_schema;
      param.vector_param_table_schema_ = logical_vector_param_schema;
      const uint64_t count = request.number();
      if (request.ret || !count || count > MAX_FRAME / 8 || owner || parent.slot || parent.generation
          || generation || offline_rebuild > 1 || session_generation == UINT64_MAX) { ret = OB_INVALID_ARGUMENT; }
      for (uint64_t i = 0; !ret && i < count; ++i) {
        const ObTabletID tablet(request.number());
        ret = request.ret ? request.ret : !tablet.is_valid() ? OB_INVALID_ARGUMENT : param.participants_.push_back(tablet);
      }
      if (!ret && (!request.consumed() || !param.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
      }
      if (!ret && ns > 1) {
        failure_stage = "route_table_id";
        const uint64_t logical_table_id = static_cast<uint64_t>(param.table_id_);
        uint64_t storage_table_id = OB_INVALID_ID;
        if (OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
                ns, logical_table_id, storage_table_id))) {
        } else if (storage_table_id > static_cast<uint64_t>(INT64_MAX)) {
          ret = OB_SIZE_OVERFLOW;
        } else {
          param.table_id_ = static_cast<int64_t>(storage_table_id);
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < param.participants_.count(); ++i) {
          failure_stage = "route_participant";
          ret = route_tablet_id(ns, param.participants_.at(i));
        }
        if (OB_SUCC(ret)) {
          failure_stage = "route_table_schema";
          ret = route_direct_insert_schema(
              ns, logical_table_schema, logical_table_id, route_allocator,
              param.table_schema_);
        }
        if (OB_SUCC(ret)) {
          failure_stage = "route_lob_schema";
          ret = route_direct_insert_schema(
              ns, logical_lob_meta_schema, 0, route_allocator,
              param.lob_meta_table_schema_);
        }
        if (OB_SUCC(ret)) {
          failure_stage = "route_vector_data_schema";
          ret = route_direct_insert_schema(
              ns, logical_vector_data_schema, 0, route_allocator,
              param.vector_data_table_schema_);
        }
        if (OB_SUCC(ret)) {
          failure_stage = "route_vector_param_schema";
          ret = route_direct_insert_schema(
              ns, logical_vector_param_schema, 0, route_allocator,
              param.vector_param_table_schema_);
        }
      }
      if (!ret && !tag.slot) { ret = OB_STATE_NOT_MATCH; }
      if (!ret) {
        failure_stage = "start";
        auto staged = std::make_shared<DirectInsertOwner>(
            context, tag, ns, ++session_generation);
        ret = ObDirectInsertOrchestrator::start(staged->allocator, param, *staged, staged->session);
        fprintf(stderr,
                "PROTOTYPE_V22_DIRECT_INSERT_SHARED ret=%d ns=%llu task=%ld table=%ld format=%llu snapshot=%ld schema=%ld participants=%ld\n",
                ret, (unsigned long long)ns, param.ddl_task_id_, param.table_id_,
                (unsigned long long)param.data_format_version_, param.snapshot_version_,
                param.schema_version_, param.participants_.count());
        if (!ret) {
          ret = registry.attach(tag, staged);
          if (!ret) {
            owner = std::move(staged);
            output.number(tag.slot); output.number(tag.generation); output.number(owner->generation);
          }
        }
      }
    } else if (!ret) {
      ret = resolve(parent, generation, registry);
      if (!ret && operation == 'F') {
        if (!request.consumed() || parent.slot != tag.slot || parent.generation != tag.generation) { ret = OB_INVALID_ARGUMENT; }
        else {
          std::unique_lock<std::shared_mutex> guard(owner->mutex);
          if (owner->writers) { ret = OB_STATE_NOT_MATCH; }
          else { ret = ObDirectInsertOrchestrator::finish(owner->session); }
        }
        if (!owner->session) {
          registry.clear(tag);
          owner.reset();
        }
      } else if (!ret) {
        // PX tasks use their own existing routes and can finish concurrently.
        // Only final teardown excludes active native calls.
        std::shared_lock<std::shared_mutex> guard(owner->mutex);
        auto *session = owner->session;
        if (!session) { ret = OB_NOT_INIT; }
        else if (operation == 'P') {
          const uint64_t count = request.number();
          ObArray<ObDDLTabletSliceCount> slice_counts;
          if (request.ret || !count
              || count > (request.data.size() - request.pos) / 16) {
            ret = OB_INVALID_ARGUMENT;
          }
          for (uint64_t i = 0; !ret && i < count; ++i) {
            uint64_t tablet_id = request.number();
            const uint64_t slice_count = request.number();
            if (request.ret || !slice_count || slice_count > INT64_MAX) {
              ret = OB_INVALID_ARGUMENT;
            } else if (tablet_id != 0 && ns > 1
                       && OB_FAIL(storage::NamespaceForkKernelPrototype::storage_object_id(
                              ns, tablet_id, tablet_id))) {
            } else if (tablet_id > static_cast<uint64_t>(INT64_MAX)) {
              ret = OB_SIZE_OVERFLOW;
            } else if (OB_FAIL(slice_counts.push_back(
                           ObDDLTabletSliceCount(static_cast<int64_t>(tablet_id),
                                                static_cast<int64_t>(slice_count))))) {
            }
          }
          if (!ret && !request.consumed()) { ret = OB_INVALID_ARGUMENT; }
          if (!ret) { ret = session->prepare_ordered_input(slice_counts); }
        } else if (operation == 'R') {
          const uint64_t flags = request.number();
          ObDirectInsertPlanFacts facts; ObDirectInsertWritePolicy policy;
          facts.regenerate_heap_table_pk_ = flags & 1; facts.vector_rowkey_vid_ = flags & 2;
          facts.has_table_autoinc_ = flags & 4; facts.rowkey_doc_id_ = flags & 8;
          facts.data_table_without_pk_ = flags & 16;
          if (!request.consumed() || flags > 31) { ret = OB_INVALID_ARGUMENT; }
          else { ret = session->resolve_write_policy(facts, policy); }
          if (!ret) { output.number(policy.vector_generated_id_ | (policy.idempotent_tablet_autoinc_ << 1)
              | (policy.idempotent_table_autoinc_ << 2) | (policy.idempotent_doc_id_ << 3)); }
        } else if (operation == 'A') {
          const uint64_t scope = request.number();
          ObTabletID tablet(request.number()); const int64_t slice = request.number();
          ObDirectInsertAutoincParam param;
          if (!request.consumed() || scope > DIRECT_INSERT_TABLET_AUTOINC) { ret = OB_INVALID_ARGUMENT; }
          else if (ns > 1 && OB_FAIL(route_tablet_id(ns, tablet))) {}
          else { ret = session->build_autoinc_param(static_cast<ObDirectInsertAutoincScope>(scope), tablet, slice, param); }
          if (!ret) { output.number(param.enabled_); output.number(param.slice_count_);
            output.number(param.slice_index_); output.number(param.range_interval_); }
        } else if (operation == 'T') {
          ObTabletID tablet(request.number()), target(request.number());
          const int64_t slice = request.number(), rows = request.number();
          if (!request.consumed()) { ret = OB_INVALID_ARGUMENT; }
          else if (ns > 1 && OB_FAIL(route_tablet_id(ns, tablet))) {}
          else if (ns > 1 && OB_FAIL(route_tablet_id(ns, target))) {}
          else { ret = session->sync_tablet_autoinc(tablet, target, slice, rows); }
        } else if (operation == 'W') {
          ObDirectInsertWriterRequest param;
          const uint64_t layout = request.number();
          param.layout_ = static_cast<ObDirectInsertWriterLayout>(layout);
          param.tablet_id_ = ObTabletID(request.number()); param.slice_index_ = request.number();
          param.parallel_count_ = request.number(); param.autoinc_column_index_ = request.number();
          const uint64_t idempotent = request.number(); param.idempotent_tablet_autoinc_ = idempotent;
          // Both sides execute in one process; the native vector writer needs
          // the same spill factory that the SQL writer uses.
          param.spool_factory_ = &sql::get_temp_column_spill_spool_factory();
          if (!request.consumed() || layout > DIRECT_INSERT_ORDERED_WRITER || idempotent > 1
              || !param.is_valid() || writer_generation == UINT64_MAX) { ret = OB_INVALID_ARGUMENT; }
          if (!ret && ns > 1) { ret = route_tablet_id(ns, param.tablet_id_); }
          auto staged = ret ? nullptr : std::make_unique<DirectInsertWriterOwner>(owner);
          if (!ret) { ret = session->get_writer_factory().create(staged->allocator, param, staged->writer); }
          if (!ret) { const uint64_t id = ++writer_generation; writers.emplace(id, std::move(staged)); output.number(id); }
        } else if (operation == 'B' || operation == 'E' || operation == 'X') {
          const uint64_t id = request.number();
          auto entry = writers.find(id);
          if (request.ret || entry == writers.end()) { ret = OB_STATE_NOT_MATCH; }
          else if (operation == 'B') {
            const uint64_t rows = request.number(), columns = request.number();
            if (request.ret || !rows || rows > 32 || !columns || columns > OB_MAX_COLUMN_NUMBER) { ret = OB_INVALID_ARGUMENT; }
            std::vector<ObDatum> cells;
            if (!ret) { cells.resize(rows * columns); }
            for (auto &cell : cells) {
              request.read(cell);
              // Native datum decoding borrows the frame; validate its payload
              // boundary before invoking any writer or decoding another datum.
              if (request.ret || request.pos > static_cast<int64_t>(request.data.size())) {
                ret = OB_INVALID_ARGUMENT; break;
              }
            }
            if (!ret && !request.consumed()) { ret = OB_INVALID_ARGUMENT; }
            std::vector<ObDatum *> row;
            if (!ret) { row.resize(columns); }
            for (uint64_t i = 0; !ret && i < rows; ++i) {
              for (uint64_t j = 0; j < columns; ++j) { row[j] = &cells[i * columns + j]; }
              ret = entry->second->writer->append_row(ObDirectInsertRowView(row.data(), columns));
            }
          } else if (!request.consumed()) { ret = OB_INVALID_ARGUMENT; }
          else if (operation == 'E') { ret = entry->second->writer->close(); }
          if (!ret && operation != 'X') { output.number(entry->second->writer->get_row_count()); }
          if (!ret && operation == 'X') { writers.erase(entry); }
        } else { ret = OB_INVALID_ARGUMENT; }
      }
    }
    reply = Frame('g'); reply.number(ret);
    if (!ret) { reply.data.insert(reply.data.end(), output.data.begin() + Frame::HEADER_SIZE, output.data.end()); }
    fprintf(stderr,
            "PROTOTYPE_V22_DIRECT_INSERT_ROUTE ns=%llu op=%c ret=%d stage=%s owner=%d writers=%zu\n",
            (unsigned long long)ns, static_cast<char>(operation), ret,
            failure_stage, nullptr != owner, writers.size());
    return output.ret ? output.ret : reply.ret;
  }
};
int call_in_process_direct_insert_simple(RequestTag parent, uint64_t generation,
                                         char operation, bool &is_final);

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
  int call(char operation, const Frame &payload, Frame &reply,
      sql::ObSQLSessionInfo *session = nullptr, bool cleanup = false) const {
    StorageSessionScope scope(session ? session : THIS_WORKER.get_session());
    int ret = scope.error() ? scope.error() : cleanup ? OB_SUCCESS : error.load();
    Frame request('J'); request.number(operation); request.number(origin.slot);
    request.number(origin.generation); request.number(generation);
    request.data.insert(request.data.end(), payload.data.begin() + Frame::HEADER_SIZE, payload.data.end());
    if (!ret) { ret = payload.ret ? payload.ret : request.data.size() + 64 > request.limit ? OB_SIZE_OVERFLOW : worker_send(request, cleanup); }
    if (!ret) { ret = worker_read(reply); }
    if (!ret) { ret = reply.type() == 'g' ? static_cast<int>(reply.number()) : OB_INVALID_ARGUMENT; }
    if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
    fprintf(stderr,
            "PROTOTYPE_V22_DIRECT_INSERT_CALL op=%c ret=%d sticky=%d cleanup=%d\n",
            operation, ret, error.load(), cleanup);
    return ret;
  }
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
    int ret = OB_SUCCESS;
    Frame payload, reply;
    if (OB_SUCC(ret)) {
      payload.number(slice_counts.count());
      for (int64_t i = 0; !payload.ret && i < slice_counts.count(); ++i) {
        const ObDDLTabletSliceCount &entry = slice_counts.at(i);
        if (entry.slice_count_ <= 0) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          payload.number(entry.tablet_id_);
          payload.number(entry.slice_count_);
        }
      }
      if (OB_SUCC(ret) && payload.ret) { ret = payload.ret; }
    }
    if (OB_SUCC(ret)) { ret = call('P', payload, reply); }
    if (OB_SUCC(ret) && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
    if (OB_FAIL(ret)) {
      int expected = OB_SUCCESS;
      error.compare_exchange_strong(expected, ret);
    }
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
    Frame payload, reply;
    payload.number(facts.regenerate_heap_table_pk_ | (facts.vector_rowkey_vid_ << 1)
        | (facts.has_table_autoinc_ << 2) | (facts.rowkey_doc_id_ << 3) | (facts.data_table_without_pk_ << 4));
    int ret = call('R', payload, reply);
    const uint64_t flags = ret ? 0 : reply.number();
    if (!ret && (!reply.consumed() || flags > 15)) { ret = OB_INVALID_ARGUMENT; }
    if (!ret) { policy.vector_generated_id_ = flags & 1; policy.idempotent_tablet_autoinc_ = flags & 2;
      policy.idempotent_table_autoinc_ = flags & 4; policy.idempotent_doc_id_ = flags & 8; }
    return ret;
  }
  int build_autoinc_param(ObDirectInsertAutoincScope scope, const ObTabletID &tablet,
      int64_t slice, ObDirectInsertAutoincParam &param) override {
    Frame payload, reply; payload.number(scope); payload.number(tablet.id()); payload.number(slice);
    int ret = call('A', payload, reply);
    if (!ret) {
      const uint64_t enabled = reply.number(); param.enabled_ = enabled;
      param.slice_count_ = reply.number(); param.slice_index_ = reply.number(); param.range_interval_ = reply.number();
      if (!reply.consumed() || enabled > 1 || !param.is_valid()) { ret = OB_INVALID_ARGUMENT; }
    }
    return ret;
  }
  int sync_tablet_autoinc(const ObTabletID &tablet, const ObTabletID &target, int64_t slice, int64_t rows) override {
    Frame payload, reply; payload.number(tablet.id()); payload.number(target.id()); payload.number(slice); payload.number(rows);
    int ret = call('T', payload, reply);
    return ret ? ret : reply.consumed() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  ObIDirectInsertWriterFactory &get_writer_factory() override { return *this; }
  int create(ObIAllocator &, const ObDirectInsertWriterRequest &, ObIDirectInsertWriter *&) override;
private:
  int finish_and_destroy() override {
    Frame payload, reply; int ret = call('F', payload, reply, sqc_session, true);
    if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
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
  int send_rows(Frame &payload) {
    Frame reply; int ret = session.call('B', payload, reply, sql_session);
    if (!ret) { rows = reply.number(); if (!reply.consumed() || rows < 0) { ret = OB_INVALID_ARGUMENT; } }
    return ret;
  }
  int append_row(const ObDirectInsertRowView &row) override {
    if (!row.is_valid() || row.datum_count_ > OB_MAX_COLUMN_NUMBER) { return OB_INVALID_ARGUMENT; }
    Frame payload; payload.number(id); payload.number(1); payload.number(row.datum_count_);
    for (int64_t i = 0; i < row.datum_count_; ++i) {
      if (!row.datums_[i]) { return OB_INVALID_ARGUMENT; }
      payload.append(*row.datums_[i]);
    }
    return send_rows(payload);
  }
  int append_batch(const ObDirectInsertBatchView &batch) override {
    if (!batch.is_valid() || batch.vector_count_ > OB_MAX_COLUMN_NUMBER) { return OB_INVALID_ARGUMENT; }
    int ret = OB_SUCCESS;
    for (int64_t first = 0; !ret && first < batch.row_count_; first += 32) {
      const int64_t count = std::min<int64_t>(32, batch.row_count_ - first);
      Frame payload; payload.number(id); payload.number(count); payload.number(batch.vector_count_);
      for (int64_t i = first; i < first + count; ++i) {
        const int64_t index = batch.selection_type_ == ObDirectInsertBatchView::CONTIGUOUS_SELECTION
            ? batch.offset_ + i : batch.indices_[i];
        for (int64_t col = 0; col < batch.vector_count_; ++col) {
          if (!batch.vectors_[col]) { return OB_INVALID_ARGUMENT; }
          bool is_null = false; const char *value = nullptr; ObLength length = 0;
          batch.vectors_[col]->get_payload(index, is_null, value, length);
          ObDatum datum; if (is_null) { datum.set_null(); } else { datum.set_string(value, length); }
          payload.append(datum);
        }
      }
      ret = send_rows(payload);
    }
    return ret;
  }
  int close() override {
    Frame payload, reply; payload.number(id);
    int ret = session.call('E', payload, reply, sql_session);
    if (!ret) { rows = reply.number(); if (!reply.consumed() || rows < 0) { ret = OB_INVALID_ARGUMENT; } }
    return ret;
  }
  int64_t get_row_count() const override { return rows; }
  const ObTabletID &get_tablet_id() const override { return tablet; }
  int64_t get_slice_index() const override { return slice; }
private:
  void destroy_self() override {
    Frame payload, reply; payload.number(id);
    const int ret = session.call('X', payload, reply, sql_session, true);
    if (!ret && !reply.consumed()) { session.error = OB_INVALID_ARGUMENT; }
    auto &a = allocator; this->~RemoteDirectInsertWriter(); a.free(this);
  }
};
int RemoteDirectInsertSession::create(ObIAllocator &a, const ObDirectInsertWriterRequest &request,
    ObIDirectInsertWriter *&writer) {
  writer = nullptr;
  if (!request.is_valid()) { return OB_INVALID_ARGUMENT; }
  auto *storage = a.alloc(sizeof(RemoteDirectInsertWriter));
  if (!storage) { return OB_ALLOCATE_MEMORY_FAILED; }
  Frame payload, reply;
  payload.number(request.layout_); payload.number(request.tablet_id_.id()); payload.number(request.slice_index_);
  payload.number(request.parallel_count_); payload.number(request.autoinc_column_index_); payload.number(request.idempotent_tablet_autoinc_);
  int ret = call('W', payload, reply);
  const uint64_t id = ret ? 0 : reply.number();
  if (!ret && (!reply.consumed() || !id)) { ret = OB_INVALID_ARGUMENT; }
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
    Frame request('J'), reply; request.number('S'); request.number(0); request.number(0); request.number(0);
    request.number(param.ddl_task_id_); request.number(param.execution_id_); request.number(param.table_id_);
    request.number(param.worker_count_); request.number(param.data_format_version_);
    request.number(param.snapshot_version_); request.number(param.schema_version_);
    request.number(param.is_offline_index_rebuild_);
    request.string(param.table_schema_); request.string(param.lob_meta_table_schema_);
    request.string(param.vector_data_table_schema_);
    request.string(param.vector_param_table_schema_);
    request.number(param.participants_.count());
    for (int64_t i = 0; i < param.participants_.count(); ++i) { request.number(param.participants_.at(i).id()); }
    int ret = scope.error();
    if (!ret) { ret = worker_send(request); }
    if (!ret) { ret = worker_read(reply); }
    if (!ret) { ret = reply.type() == 'g' ? static_cast<int>(reply.number()) : OB_INVALID_ARGUMENT; }
    fprintf(stderr,
            "PROTOTYPE_V22_DIRECT_INSERT_WORKER ret=%d task=%ld table=%ld format=%llu snapshot=%ld schema=%ld participants=%ld\n",
            ret, param.ddl_task_id_, param.table_id_,
            (unsigned long long)param.data_format_version_, param.snapshot_version_,
            param.schema_version_, param.participants_.count());
    if (!ret) {
      const RequestTag origin{reply.number(), reply.number()}; const uint64_t generation = reply.number();
      if (!reply.consumed() || !generation || !origin.slot || !origin.generation) { ret = OB_INVALID_ARGUMENT; }
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
