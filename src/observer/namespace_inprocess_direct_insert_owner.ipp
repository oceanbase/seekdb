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
  int resolve_ddl_error_context(
      uint64_t &table_id, uint64_t &tablet_id,
      share::schema::ObMultiVersionSchemaService *&schema_service,
      ObMySQLProxy *&sql_proxy) override {
    schema_service = nullptr;
    sql_proxy = nullptr;
    ns::NamespaceRuntime *runtime = nullptr;
    if (!ns::namespace_registry().get(namespace_id, runtime) || runtime == nullptr) {
      return OB_NOT_INIT;
    }
    schema_service = static_cast<share::schema::ObMultiVersionSchemaService *>(
        runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE));
    sql_proxy = static_cast<ObMySQLProxy *>(
        runtime->service(ns::NamespaceRuntime::SQL_PROXY));
    if (!schema_service || !sql_proxy) { return OB_NOT_INIT; }
    if (namespace_id > 1) {
      uint64_t logical_table_id = OB_INVALID_ID;
      uint64_t logical_tablet_id = OB_INVALID_ID;
      int ret = storage::NamespaceForkKernelPrototype::local_object_id(
          namespace_id, table_id, logical_table_id);
      if (!ret) {
        ret = storage::NamespaceForkKernelPrototype::local_object_id(
            namespace_id, tablet_id, logical_tablet_id);
      }
      if (ret) { return ret; }
      table_id = logical_table_id;
      tablet_id = logical_tablet_id;
    }
    return OB_SUCCESS;
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

class DirectInsertWorkerContextScope final {
public:
  explicit DirectInsertWorkerContextScope(ObIDirectInsertWorkerContext *context)
      : previous_(set_current_direct_insert_worker_context(context)) {}
  ~DirectInsertWorkerContextScope() {
    set_current_direct_insert_worker_context(previous_);
  }
private:
  ObIDirectInsertWorkerContext *previous_;
};
