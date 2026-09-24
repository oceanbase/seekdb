struct EngineScan {
  struct VirtualContext {
    sql::ObExecContext execution;
    sql::ObEvalCtx evaluation;
    sql::ObPushdownExprSpec spec;
    sql::ObPushdownOperator op;
    VirtualContext(ObIAllocator &allocator, sql::ObSQLSessionInfo &session)
        : execution(allocator), evaluation(execution), spec(allocator), op(evaluation, spec) {
      execution.set_my_session(&session);
    }
  };
  ObArenaAllocator allocator{ObMemAttr("NsRemoteScan")};
  // Iterator and range-key memory live in this per-scan arena so an NLJ
  // rescan can drop them without touching the schema copies in allocator.
  ObArenaAllocator iter_allocator{ObMemAttr("NsRemoteScanIt")};
  ObSchemaGetterGuard guard;
  std::unique_ptr<ScanSchema> scan_schema;
  ObTableParam table{allocator};
  ObTableScanParam param;
  std::vector<ObObj> keys;
  ObNewRowIterator *iter = nullptr;
  const ObTableSchema *schema = nullptr;
  std::unique_ptr<VirtualContext> virtual_context;
  ~EngineScan() {
    if (iter) {
      if (virtual_context) { share::server_service<ObIVirtualTableScan>()->revert_scan_iter(iter); }
      else { share::server_service<ObITabletScan>()->revert_scan_iter(iter); }
    }
  }
  int open(StorageSpaceHandle storage_space, const ObVTableScanParam &request,
           const ObTableSchema &logical_schema,
           transaction::ObTxDesc *tx, sql::ObSQLSessionInfo *session) {
    int ret = OB_SUCCESS;
    const uint64_t logical_table_id = request.index_id_;
    const uint64_t logical_tablet_id = request.tablet_id_.id();
    const bool namespace_local = storage_space.is_namespace();
    const uint64_t ns = storage_space.namespace_id();
    const int64_t requested_schema_version = request.schema_version_;
    if (OB_FAIL(copy_scan_schema(ns, namespace_local, logical_schema, scan_schema))) {
      return ret;
    }
    param.scan_flag_.flag_ = request.scan_flag_.flag_;
    const bool get = request.is_get_;
    param.limit_param_.limit_ = -1;
    param.limit_param_.offset_ = 0;
    const int64_t count = request.column_ids_.count();
    if (count > OB_MAX_COLUMN_NUMBER) { return OB_NOT_SUPPORTED; }
    bool logical_tablet_matches = false;
    for (int64_t i = 0; i < scan_schema->logical_tablets.count(); ++i) {
      if (scan_schema->logical_tablets.at(i).id() == logical_tablet_id) {
        logical_tablet_matches = true;
        break;
      }
    }
    const ObTableSchema *logical = scan_schema->logical;
    if ((requested_schema_version <= 0 && !is_inner_table(logical_table_id))
            || logical->get_table_id() != logical_table_id
            || !logical_tablet_matches
            || logical->get_schema_version() < 0
            || (requested_schema_version > 0
                && logical->get_schema_version() != requested_schema_version)) {
      return OB_INVALID_ARGUMENT;
    }
    schema = scan_schema->routed;
    uint64_t tablet_id = logical_tablet_id;
    if (schema && namespace_local && ns > 1) {
      ret = NamespaceForkKernelPrototype::storage_object_id(
          ns, logical_tablet_id, tablet_id);
    }
    bool storage_tablet_matches = false;
    if (OB_SUCC(ret) && scan_schema) {
      for (int64_t i = 0; i < scan_schema->storage_tablets.count(); ++i) {
        if (scan_schema->storage_tablets.at(i).id() == tablet_id) {
          storage_tablet_matches = true;
          break;
        }
      }
    }
    if (OB_FAIL(ret)) { return ret; }
    if (!schema || (!is_virtual_table(logical_table_id) && !storage_tablet_matches)) {
      return OB_INVALID_ARGUMENT;
    }
    const int64_t storage_schema_version = requested_schema_version > 0
        ? requested_schema_version : schema->get_schema_version();
    const uint64_t table_id = schema->is_sys_table()
        ? logical_table_id : schema->get_table_id();
    for (int64_t i = 0; !ret && i < count; ++i) {
      const uint64_t column = request.column_ids_.at(i);
      if (column != OB_HIDDEN_TRANS_VERSION_COLUMN_ID
          && column != OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID
          && column != OB_HIDDEN_GROUP_IDX_COLUMN_ID
          && !schema->get_column_schema(column)) {
        ret = OB_NOT_SUPPORTED;
      }
      else { ret = param.column_ids_.push_back(column); }
    }
    const int64_t ranges = request.key_ranges_.count();
    // Must cover MAX_IN_QUERY_PER_TIME (1000): IN-batch refreshes arrive as
    // one range per element in a single scan request.
    if (ret || ranges > 8192) { return ret ? ret : OB_NOT_SUPPORTED; }
    const int64_t width = ranges == 0 ? 1 : request.key_ranges_.at(0).start_key_.get_obj_cnt();
    if (width <= 0 || width > OB_MAX_ROWKEY_COLUMN_NUMBER) {
      return OB_INVALID_ARGUMENT;
    }
    int64_t request_bytes = 105; // Former opcode and thirteen number fields.
    auto add_bytes = [&](int64_t size) {
      if (size < 0 || size > static_cast<int64_t>(MAX_SQL_MESSAGE) - request_bytes) {
        return OB_SIZE_OVERFLOW;
      }
      request_bytes += size;
      return OB_SUCCESS;
    };
    if (OB_FAIL(add_bytes(logical_schema.get_serialize_size()))
        || OB_FAIL(add_bytes(count * 8))) { return ret; }
    if (is_virtual_table(logical_table_id)) {
      if (OB_FAIL(add_bytes(8))) { return ret; }
    } else {
      const auto &scan = static_cast<const ObTableScanParam &>(request);
      if (OB_FAIL(add_bytes(40))
          || OB_FAIL(add_bytes(scan.sample_info_.get_serialize_size()))
          || OB_FAIL(add_bytes(scan.snapshot_.get_serialize_size()))) { return ret; }
    }
    keys.resize(ranges * 2 * width);
    for (int64_t i = 0; !ret && i < ranges; ++i) {
      const ObNewRange &source = request.key_ranges_.at(i);
      if (source.start_key_.get_obj_cnt() != width || source.end_key_.get_obj_cnt() != width) {
        return OB_INVALID_ARGUMENT;
      }
      if (OB_FAIL(add_bytes(8))) { return ret; }
      ObNewRange range; range.table_id_ = table_id;
      range.border_flag_.set_data(source.border_flag_.get_data());
      for (int64_t j = 0; !ret && j < width * 2; ++j) {
        const ObObj &value = j < width ? source.start_key_.get_obj_ptr()[j]
                                       : source.end_key_.get_obj_ptr()[j - width];
        if (OB_FAIL(add_bytes(value.get_serialize_size()))) { return ret; }
        ret = ob_write_obj(iter_allocator, value, keys[i * width * 2 + j]);
      }
      range.start_key_.assign(&keys[i * width * 2], width);
      range.end_key_.assign(&keys[i * width * 2 + width], width);
      if (!ret) { ret = param.key_ranges_.push_back(range); }
    }
    if (is_virtual_table(logical_table_id)) {
      param.sql_mode_ = request.sql_mode_;
      if (ret || !session || ns != 1) { return ret ? ret : OB_INVALID_ARGUMENT; }
      virtual_context = std::make_unique<VirtualContext>(allocator, *session);
      param.index_id_ = table_id; param.tablet_id_ = ObTabletID(tablet_id);
      param.schema_version_ = schema->get_schema_version();
      param.runtime_schema_version_ = schema->get_schema_version();
      param.timeout_ = THIS_WORKER.get_timeout_ts();
      param.scan_allocator_ = &iter_allocator; param.reserved_cell_count_ = count;
      param.op_ = &virtual_context->op;
      ret = share::server_service<ObIVirtualTableScan>()->table_scan(param, iter);
      fprintf(stderr, "PROTOTYPE_V18_VIRTUAL_SCAN table=%llu ret=%d\n", (unsigned long long)table_id, ret);
      return ret;
    }
    const auto &scan = static_cast<const ObTableScanParam &>(request);
    const uint64_t txid = scan.tx_id_.get_id();
    const bool read_latest = param.scan_flag_.is_read_latest();
    param.for_update_ = request.for_update_;
    param.is_for_foreign_check_ = request.is_for_foreign_check_;
    param.sample_info_ = scan.sample_info_;
    if (!tx || static_cast<uint64_t>(data_plane::tx_desc_id(tx).get_id()) != txid) {
      return OB_INVALID_ARGUMENT;
    }
    if (OB_FAIL(param.snapshot_.assign(scan.snapshot_))) { return ret; }
    param.tx_lock_timeout_ = scan.tx_lock_timeout_;
    param.tx_seq_base_ = scan.tx_seq_base_;
    param.tx_id_ = data_plane::tx_desc_id(tx);
    param.trans_desc_ = tx; // Native pointer from this request, never from IPC.
    if (!param.snapshot_.is_valid() || param.snapshot_.is_weak_read()
        || (param.snapshot_.core_.tx_id_.is_valid() && param.snapshot_.core_.tx_id_ != param.tx_id_)
        || (!txid && read_latest)) {
      return OB_INVALID_ARGUMENT;
    }
    if (ret) { return ret; }
    param.index_id_ = table_id; param.tablet_id_ = ObTabletID(tablet_id);
    param.schema_version_ = storage_schema_version;
    param.runtime_schema_version_ = storage_schema_version;
    param.timeout_ = THIS_WORKER.get_timeout_ts();
    param.is_get_ = get;
    param.allocator_ = &iter_allocator; param.scan_allocator_ = &iter_allocator;
    param.reserved_cell_count_ = count;
    // Match the native SQL scan path: every LOB storage column needs a V2
    // locator, including __all_* columns.
    table.get_enable_lob_locator_v2() = true;
    // Reads never materialize: an inherited tablet is served through
    // resolve_read_tablet redirection inside the storage layer instead.
    if (!ret) { ret = table.convert(*schema, param.column_ids_, sql::ObStoragePushdownFlag()); }
    if (!ret) {
      param.table_param_ = &table;
      ret = share::server_service<ObITabletScan>()->table_scan(param, iter);
    }
    fprintf(stderr, "PROTOTYPE_V10_SCAN_OPEN ns=%llu table=%llu ret=%d\n",
        static_cast<unsigned long long>(ns), static_cast<unsigned long long>(table_id), ret);
    if (txid) { fprintf(stderr, "PROTOTYPE_V15_TX_SCAN tx=%llu latest=%d ret=%d\n", (unsigned long long)txid, read_latest, ret); }
    return ret;
  }
  // NLJ rescans only change key ranges. Rebuild the storage iterator in place
  // instead of paying a full schema ship + tablet lookup + scan open per row.
  int rescan(const ObVTableScanParam &request) {
    if (virtual_context || !iter) { return OB_NOT_SUPPORTED; }
    const int64_t ranges = request.key_ranges_.count();
    const int64_t width = ranges == 0 ? 1 : request.key_ranges_.at(0).start_key_.get_obj_cnt();
    if (ranges > 256 || width <= 0 || width > OB_MAX_ROWKEY_COLUMN_NUMBER) {
      return OB_INVALID_ARGUMENT;
    }
    size_t bytes = 41; // Former frame opcode and five numbers.
    for (int64_t i = 0; i < ranges; ++i) {
      const ObNewRange &range = request.key_ranges_.at(i);
      if (range.start_key_.get_obj_cnt() != width || range.end_key_.get_obj_cnt() != width
          || bytes > MAX_SQL_MESSAGE - 8) { return OB_INVALID_ARGUMENT; }
      bytes += 8;
      for (int64_t j = 0; j < width * 2; ++j) {
        const ObObj &obj = j < width ? range.start_key_.get_obj_ptr()[j]
                                      : range.end_key_.get_obj_ptr()[j - width];
        const int64_t size = obj.get_serialize_size();
        if (size < 0 || static_cast<size_t>(size) > MAX_SQL_MESSAGE - bytes) {
          return OB_SIZE_OVERFLOW;
        }
        bytes += static_cast<size_t>(size);
      }
    }
    share::server_service<ObITabletScan>()->revert_scan_iter(iter);
    iter = nullptr;
    iter_allocator.reset();
    param.key_ranges_.reset();
    keys.resize(ranges * 2 * width);
    int ret = OB_SUCCESS;
    for (int64_t i = 0; !ret && i < ranges; ++i) {
      const ObNewRange &source = request.key_ranges_.at(i);
      ObNewRange range; range.table_id_ = param.index_id_;
      range.border_flag_.set_data(source.border_flag_.get_data());
      for (int64_t j = 0; !ret && j < width * 2; ++j) {
        const ObObj &obj = j < width ? source.start_key_.get_obj_ptr()[j]
                                      : source.end_key_.get_obj_ptr()[j - width];
        ret = ob_write_obj(iter_allocator, obj, keys[i * width * 2 + j]);
      }
      range.start_key_.assign(&keys[i * width * 2], width);
      range.end_key_.assign(&keys[i * width * 2 + width], width);
      if (!ret) { ret = param.key_ranges_.push_back(range); }
    }
    param.scan_flag_.flag_ = request.scan_flag_.flag_;
    param.is_get_ = request.is_get_;
    param.timeout_ = THIS_WORKER.get_timeout_ts();
    if (!ret) { ret = share::server_service<ObITabletScan>()->table_scan(param, iter); }
    return ret;
  }
  int fetch(ScanBatch &batch) {
    batch.reset();
    uint64_t count = 0; bool end = false; int ret = OB_SUCCESS;
    for (; count < 32; ++count) {
      if (virtual_context) {
        ObNewRow *row = nullptr;
        ret = iter->get_next_row(row);
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; end = true; break; }
        if (ret) { break; }
        if (!row || row->get_count() != param.column_ids_.count()) { ret = OB_ERR_UNEXPECTED; break; }
        for (int64_t i = 0; !ret && i < row->get_count(); ++i) {
          ret = batch.push(row->get_cell(i), false);
        }
        if (ret) { break; }
        continue;
      }
      blocksstable::ObDatumRow *row = nullptr;
      ret = static_cast<ObTableScanIterator *>(iter)->get_next_row(row);
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; end = true; break; }
      if (ret) { break; }
      if (!row || row->get_column_count() != param.column_ids_.count()) { ret = OB_ERR_UNEXPECTED; break; }
      const auto &projector = table.get_output_projector();
      const auto &descriptors = table.get_read_info().get_columns_desc();
      if (projector.count() != row->get_column_count()) { ret = OB_ERR_UNEXPECTED; break; }
      for (int64_t i = 0; !ret && i < row->get_column_count(); ++i) {
        ObObj value;
        const int64_t index = projector.at(i);
        if (index < 0 || index >= descriptors.count() || descriptors.at(index).col_id_ != param.column_ids_.at(i)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          // Native descriptors include decimal precision/scale and LOB flags.
          ret = row->storage_datums_[i].to_obj_enhance(value, descriptors.at(index).col_type_);
        }
        if (!ret) {
          const bool has_lob_header = table.enable_lob_locator_v2() && value.is_lob_storage()
              && !value.is_null() && value.has_lob_header();
          ret = batch.push(value, has_lob_header);
        }
      }
      if (ret) { break; }
    }
    if (!ret) {
      batch.rows = count;
      batch.end = end;
    }
    return ret;
  }
};
struct ReadScans {
  StorageSpaceHandle storage_space;
  std::map<uint64_t, std::unique_ptr<EngineScan>> scans;
  uint64_t sequence = 0;
  explicit ReadScans(StorageSpaceHandle space) : storage_space(space) {}
  ~ReadScans() {
    const size_t remaining = scans.size();
    scans.clear();
    fprintf(stderr, "PROTOTYPE_V13_SCANS_RELEASED ns=%llu remaining=%zu\n",
        (unsigned long long)storage_space.namespace_id(), remaining);
  }
  int fetch(uint64_t id, ScanBatch &batch) {
    auto it = scans.find(id);
    return it == scans.end() ? OB_INVALID_ARGUMENT : it->second->fetch(batch);
  }
  int close(uint64_t id) {
    auto it = scans.find(id);
    if (it == scans.end()) { return OB_INVALID_ARGUMENT; }
    scans.erase(it);
    return OB_SUCCESS;
  }
  int rescan(uint64_t id, const ObVTableScanParam &param) {
    auto it = scans.find(id);
    return it == scans.end() ? OB_INVALID_ARGUMENT : it->second->rescan(param);
  }
  int open(StorageSpaceHandle requested_space, const ObVTableScanParam &param,
           const ObTableSchema &logical_schema, transaction::ObTxDesc *tx,
           sql::ObSQLSessionInfo *session, uint64_t &handle) {
    handle = 0;
    if (!storage_space.is_namespace()) { return OB_INVALID_ARGUMENT; }
    auto scan = std::make_unique<EngineScan>();
    int ret = scan->open(requested_space, param, logical_schema, tx, session);
    if (ret) { fprintf(stderr, "PROTOTYPE_V17_SCAN_FAILED ret=%d\n", ret); }
    else {
      handle = ++sequence;
      scans.emplace(handle, std::move(scan));
    }
    return ret;
  }
};
