class InProcessScanIterator final : public ObNewRowIterator {
public:
  ObVTableScanParam &param;
  uint64_t handle = 0, row_index = 0, rows_left = 0;
  int64_t qualified = 0, returned = 0;
  bool end = false;
  ScanBatch batch; // Own variable-length cell bytes until the next batch.
  ObNewRow row;
  explicit InProcessScanIterator(ObVTableScanParam &p) : param(p) {}
  ~InProcessScanIterator() override { reset(); }
  int open() {
    const sql::ObStoragePushdownFlag flags(param.pd_storage_flag_);
    // The storage process deliberately does not execute SQL expressions. A
    // pushed filter still has its expression list in op_filters_, so evaluate
    // it in this worker while streaming rows from the physical tablet.
    if (!param.op_ || !param.output_exprs_
        || param.output_exprs_->count() != param.column_ids_.count()
        || (param.aggregate_exprs_ && !param.aggregate_exprs_->empty())
        || flags.is_aggregate_pushdown() || flags.is_group_by_pushdown()) {
      fprintf(stderr,
          "PROTOTYPE_V22_SCAN_UNSUPPORTED table=%llu columns=%lld outputs=%lld "
          "filters=%lld flags=%d aggregate=%lld\n",
          (unsigned long long)param.index_id_,
          (long long)param.column_ids_.count(),
          (long long)(param.output_exprs_ ? param.output_exprs_->count() : -1),
          (long long)(param.op_filters_ ? param.op_filters_->count() : -1),
          param.pd_storage_flag_,
          (long long)(param.aggregate_exprs_ ? param.aggregate_exprs_->count() : -1));
      return OB_NOT_SUPPORTED;
    }
    StorageSessionScope scan_scope(param.op_->get_eval_ctx().exec_ctx_.get_my_session());
    if (scan_scope.error()) { return scan_scope.error(); }
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *logical_schema = nullptr;
    // Inner tables must carry their schema even for namespace 1: the shared
    // process otherwise re-resolves them through its own SchemaService, whose
    // lazy load needs inner SQL to this namespace's Worker.  During Worker
    // activation (crash recovery) that Worker is busy initialising itself, so
    // the lookup cannot complete and the activation deadlocks or fails.
    bool send_logical_schema = serves_namespace_schema()
        && !NamespaceForkKernelPrototype::is_encoded_id(param.index_id_);
    StorageSpaceHandle storage_space =
        StorageSpaceHandle::namespace_space(serving_namespace());
    int ret = send_logical_schema
        ? worker_local_table_schema(
              param.index_id_, param.schema_version_, schema_guard, logical_schema,
              storage_space)
        : OB_SUCCESS;
    if (ret) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_OPEN stage=schema ret=%d table=%llu schema=%lld send=%d\n",
              ret, static_cast<unsigned long long>(param.index_id_),
              static_cast<long long>(param.schema_version_), send_logical_schema);
      return ret;
    }
    ret = !send_logical_schema || logical_schema == nullptr ? OB_NOT_SUPPORTED
        : open_in_process_scan(storage_space, param, *logical_schema, handle);
    if (ret) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_OPEN stage=in_process ret=%d table=%llu\n",
              ret, static_cast<unsigned long long>(param.index_id_));
    }
    return ret;
  }
  int get_next_row(ObNewRow *&out) override {
    int ret = OB_SUCCESS;
    const size_t columns = param.column_ids_.count();
    if (!rows_left) {
      if (end) { return OB_ITER_END; }
      StorageSessionScope scope(param.op_->get_eval_ctx().exec_ctx_.get_my_session());
      ret = scope.error() ? scope.error() : fetch_in_process_scan(handle, batch);
      if (ret) { return ret; }
      end = batch.end;
      if (batch.rows > 32 || (!batch.rows && !end)
          || batch.cells.size() != batch.rows * columns) { return OB_INVALID_ARGUMENT; }
      row_index = 0; rows_left = batch.rows;
      if (!rows_left) { return OB_ITER_END; }
    }
    row.cells_ = columns ? batch.cells.data() + row_index : nullptr;
    row.count_ = columns; row_index += columns; --rows_left; out = &row;
    return ret;
  }
  int get_next_row() override {
    return next_row(false);
  }
  int next_row(bool stop_before_fetch) {
    if (param.limit_param_.limit_ >= 0 && returned >= param.limit_param_.limit_) { return OB_ITER_END; }
    int ret = OB_SUCCESS;
    for (;;) {
      if ((ret = THIS_WORKER.check_status())) { return ret; }
      // A vector batch keeps all returned string pointers in one owned batch.
      if (stop_before_fetch && !rows_left) { return OB_ITER_END; }
      ObNewRow *row = nullptr;
      if ((ret = get_next_row(row))) { return ret; }
      auto &ctx = param.op_->get_eval_ctx();
      param.op_->clear_datum_eval_flag();
      for (int64_t i = 0; !ret && i < row->count_; ++i) {
        sql::ObExpr *expr = param.output_exprs_->at(i);
        ObDatum &datum = expr->locate_datum_for_write(ctx);
        ret = datum.from_obj(row->cells_[i], expr->obj_datum_map_);
        if (!ret && row->cells_[i].has_lob_header()) {
          datum.set_has_lob_header();
        }
        expr->set_evaluated_projected(ctx);
        expr->set_evaluated_flag(ctx);
      }
      bool filtered = false;
      if (!ret && param.op_filters_) { ret = sql::ObOperator::filter_row(ctx, *param.op_filters_, filtered); }
      if (ret) { return ret; }
      if (!filtered && ++qualified > param.limit_param_.offset_) { ++returned; return OB_SUCCESS; }
    }
  }
  int get_next_rows(int64_t &count, int64_t capacity) override {
    count = 0;
    auto &ctx = param.op_->get_eval_ctx();
    sql::ObEvalCtx::BatchInfoScopeGuard batch(ctx);
    // Expression frames only reserve max_batch_size_ datums. IVF PQ may ask
    // for a larger scan batch than its frame can hold.
    const int64_t frame_capacity = ctx.max_batch_size_ > 0 ? ctx.max_batch_size_ : capacity;
    const int64_t limit = std::min<int64_t>(32, std::min(capacity, frame_capacity));
    if (limit <= 0) { return OB_INVALID_ARGUMENT; }
    batch.set_batch_size(limit);
    int ret = OB_SUCCESS;
    while (count < limit) {
      batch.set_batch_idx(count);
      ret = next_row(count != 0);
      if (ret) { break; }
      ++count;
    }
    if (ret != OB_SUCCESS && ret != OB_ITER_END) {
      fprintf(stderr,
              "PROTOTYPE_V22_SCAN_FETCH ret=%d table=%llu count=%ld capacity=%ld handle=%llu end=%d\n",
              ret, static_cast<unsigned long long>(param.index_id_), count, capacity,
              static_cast<unsigned long long>(handle), end);
    }
    return ret == OB_ITER_END && count ? OB_SUCCESS : ret;
  }
  void reset() override {
    if (handle) {
      StorageSessionScope scope(param.op_ ? param.op_->get_eval_ctx().exec_ctx_.get_my_session() : nullptr);
      if (!scope.error()) { close_in_process_scan(handle); }
      handle = 0;
    }
    batch.reset(); row_index = 0; rows_left = 0; end = false; qualified = 0; returned = 0;
  }
  // NLJ rescan: same table, same columns, only the key ranges changed. Reuse
  // the shared-side scan instead of re-shipping the schema per row. Falls
  // back to a full close+open when the shared side cannot rescan.
  int rescan() {
    batch.reset(); row_index = 0; rows_left = 0; end = false; qualified = 0; returned = 0;
    if (!handle || is_virtual_table(param.index_id_)) { reset(); return open(); }
    StorageSessionScope scope(param.op_ ? param.op_->get_eval_ctx().exec_ctx_.get_my_session() : nullptr);
    const int ret = scope.error() ? scope.error() : rescan_in_process_scan(handle, param);
    if (ret) {
      fprintf(stderr, "PROTOTYPE_V23_SCAN_RESCAN_FALLBACK ret=%d table=%llu ranges=%lld\n",
          ret, static_cast<unsigned long long>(param.index_id_),
          static_cast<long long>(param.key_ranges_.count()));
      reset(); return open();
    }
    return OB_SUCCESS;
  }
};
class InProcessTabletScan final : public ObIVirtualTableScan {
public:
  int table_scan(ObVTableScanParam &param, ObNewRowIterator *&iter) override {
    if (iter) { return OB_INVALID_ARGUMENT; }
    auto scan = std::make_unique<InProcessScanIterator>(param);
    int ret = scan->open(); if (!ret) { iter = scan.release(); } return ret;
  }
  int revert_scan_iter(ObNewRowIterator *iter) override { delete iter; return OB_SUCCESS; }
  int reuse_scan_iter(bool, ObNewRowIterator *iter) override {
    auto *scan = static_cast<InProcessScanIterator *>(iter);
    if (!scan) { return OB_SUCCESS; }
    scan->reset(); return OB_SUCCESS;
  }
  int table_rescan(ObVTableScanParam &, ObNewRowIterator *iter) override {
    auto *scan = static_cast<InProcessScanIterator *>(iter);
    if (!scan) { return OB_INVALID_ARGUMENT; }
    return scan->rescan();
  }
};
