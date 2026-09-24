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
      DirectInsertWorkerContextScope context_scope(owner.get());
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
    DirectInsertWorkerContextScope context_scope(owner.get());
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
