// Included inside oceanbase::observer::namespace_worker_prototype.
class InProcessWriteContext final : public ObIWriteContextService {
public:
  int acquire_write_context(int64_t, ObTxDesc &tx, const ObTxReadSnapshot &, int16_t,
                            concurrent_control::ObWriteFlag &, ObWriteContext &context) override {
    // Deferred acquisition is combined with prepare_execution in one RPC.
    context.bind(&tx, nullptr); return OB_SUCCESS;
  }
};

struct InProcessExecution final : public ObIDmlExecutionState {
  uint64_t handle = 0, txid = 0;
  ObTxDesc *tx = nullptr; // Borrowed query view; never sent across IPC.
  sql::ObSQLSessionInfo *session = nullptr;
  int64_t deadline = 0;
  std::vector<ObObjMeta> types;
  std::vector<uint64_t> columns;
  void destroy() override {
    if (handle) {
      StorageSessionScope scope(session);
      if (!scope.error() && tx) { close_in_process_write(*tx, handle); }
    }
    delete this;
  }
  int64_t timeout() const override { return deadline; }
  void set_skip_flush_redo(bool) override {} // No index writes in this slice.
};

class DuplicateRows final : public ObDatumRowIterator {
public:
  std::vector<std::unique_ptr<WriteResult>> batches;
  size_t current = 0;
  uint64_t remaining = 0;
  int64_t width;
  ObDatumRow row;
  explicit DuplicateRows(int64_t n) : width(n) {}
  int get_next_row(ObDatumRow *&out) override {
    while (!remaining) {
      if (current == batches.size()) { return OB_ITER_END; }
      remaining = batches[current]->rows;
      if (!remaining) { ++current; }
    }
    WriteResult &batch = *batches[current];
    if (batch.cells.size() != batch.rows * width
        || batch.lob_headers.size() != batch.cells.size()) { return OB_INVALID_ARGUMENT; }
    int ret = row.is_valid() ? OB_SUCCESS : row.init(width);
    for (int64_t i = 0; !ret && i < width; ++i) {
      const size_t index = (batch.rows - remaining) * width + i;
      ret = row.storage_datums_[i].from_obj_enhance(batch.cells[index]);
      if (!ret && batch.lob_headers[index]) { row.storage_datums_[i].set_has_lob_header(); }
    }
    if (!--remaining) {
      ++current;
    }
    out = &row; return ret;
  }
};

class CompletedLobReadCursor final : public common::ObILobReadCursor {
public:
  int get_next_row(ObString &) override { return OB_ITER_END; }
  void reset() override {}
};

int read_in_process_lob(common::ObLobLocatorV2 &locator, int64_t timeout,
                        common::ObIAllocator &allocator, common::ObString &output);

class InProcessLobReadService final : public common::ObILobReadService {
public:
  void set_local(common::ObILobReadService *service) { local_ = service; }

  int get_outrow_lob_full_data(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObIAllocator *tmp_alloc) override {
    return use_remote(ctx.locator_)
        ? (!has_lob_header || !is_outrow ? OB_INVALID_ARGUMENT : materialize(ctx, ctx.locator_))
        : local_ ? local_->get_outrow_lob_full_data(
              ctx, cs_type, has_lob_header, is_outrow, tmp_alloc) : OB_NOT_INIT;
  }

  int get_delta_lob_full_data(
      common::ObLobTextIterCtx &ctx,
      common::ObObjType type,
      common::ObCollationType cs_type,
      common::ObLobLocatorV2 &locator,
      common::ObIAllocator *allocator,
      common::ObString &data) override {
    return local_ ? local_->get_delta_lob_full_data(
        ctx, type, cs_type, locator, allocator, data) : OB_NOT_INIT;
  }

  int get_outrow_prefix_data(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObIAllocator *tmp_alloc,
      uint32_t prefix_char_len) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_outrow_prefix_data(
          ctx, cs_type, has_lob_header, is_outrow, tmp_alloc, prefix_char_len) : OB_NOT_INIT;
    }
    int ret = !has_lob_header || !is_outrow ? OB_INVALID_ARGUMENT : materialize(ctx, ctx.locator_);
    if (!ret && ctx.content_byte_len_ > 0) {
      const int64_t chars = common::ObCharset::strlen_char(cs_type, ctx.buff_, ctx.content_byte_len_);
      const int64_t wanted = std::min<int64_t>(chars, prefix_char_len);
      ctx.content_byte_len_ = static_cast<uint32_t>(
          common::ObCharset::charpos(cs_type, ctx.buff_, ctx.content_byte_len_, wanted));
    }
    return ret;
  }

  int get_first_block(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObIAllocator *tmp_alloc,
      common::ObString &str,
      common::ObTextStringIterState &state) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_first_block(
          ctx, cs_type, has_lob_header, is_outrow, tmp_alloc, str, state) : OB_NOT_INIT;
    }
    int ret = !has_lob_header || !is_outrow ? OB_INVALID_ARGUMENT : materialize(ctx, ctx.locator_);
    if (!ret) {
      free_lob_query_iter(ctx);
      // The reader materializes the full LOB; callers size their result for
      // the configured window, so expose only that window as the first block.
      const int64_t byte_len = ctx.content_byte_len_;
      const int64_t char_len = common::ObCharset::strlen_char(cs_type, ctx.buff_, byte_len);
      const int64_t start_char = static_cast<int64_t>(
          std::min<uint64_t>(ctx.start_offset_, static_cast<uint64_t>(char_len)));
      const int64_t read_chars = ctx.total_access_len_ == 0
          ? char_len - start_char
          : std::min<int64_t>(ctx.total_access_len_, char_len - start_char);
      if (read_chars < 0) { return OB_INVALID_ARGUMENT; }
      const int64_t start_byte = common::ObCharset::charpos(
          cs_type, ctx.buff_, byte_len, start_char);
      const int64_t end_byte = common::ObCharset::charpos(
          cs_type, ctx.buff_, byte_len, start_char + read_chars);
      if (start_byte > end_byte || end_byte > byte_len) {
        return OB_INVALID_ARGUMENT;
      }
      if (ctx.buff_ != nullptr) { ctx.buff_ += start_byte; }
      ctx.buff_byte_len_ -= static_cast<uint32_t>(start_byte);
      ctx.content_byte_len_ = static_cast<uint32_t>(end_byte - start_byte);
      ctx.content_len_ = static_cast<uint32_t>(read_chars);
      str.assign_ptr(ctx.buff_, ctx.content_byte_len_);
      ctx.accessed_byte_len_ = ctx.content_byte_len_;
      ctx.accessed_len_ = ctx.content_len_;
      ++ctx.iter_count_;
      if (ctx.content_byte_len_ == 0) {
        state = common::TEXTSTRING_ITER_END;
      } else if (OB_ISNULL(ctx.read_cursor_ = new (std::nothrow) CompletedLobReadCursor())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        state = common::TEXTSTRING_ITER_NEXT;
      }
    }
    return ret;
  }

  int get_next_block_inner(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      bool has_lob_header,
      bool is_outrow,
      common::ObString &str,
      common::ObTextStringIterState &state) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_next_block_inner(
          ctx, cs_type, has_lob_header, is_outrow, str, state) : OB_NOT_INIT;
    }
    if (!has_lob_header || !is_outrow || !ctx.read_cursor_) { return OB_INVALID_ARGUMENT; }
    free_lob_query_iter(ctx);
    str.reset();
    state = common::TEXTSTRING_ITER_END;
    return OB_SUCCESS;
  }

  int get_outrow_char_len(
      common::ObLobTextIterCtx &ctx,
      common::ObCollationType cs_type,
      common::ObIAllocator *tmp_alloc,
      int64_t &char_length) override {
    if (!use_remote(ctx.locator_)) {
      return local_ ? local_->get_outrow_char_len(ctx, cs_type, tmp_alloc, char_length) : OB_NOT_INIT;
    }
    int ret = materialize(ctx, ctx.locator_);
    if (!ret) {
      char_length = common::ObCharset::strlen_char(cs_type, ctx.buff_, ctx.content_byte_len_);
    }
    return ret;
  }

  void free_lob_query_iter(common::ObLobTextIterCtx &ctx) override {
    if (dynamic_cast<CompletedLobReadCursor *>(ctx.read_cursor_)) {
      delete static_cast<CompletedLobReadCursor *>(ctx.read_cursor_);
      ctx.read_cursor_ = nullptr;
    } else if (local_) { local_->free_lob_query_iter(ctx); }
  }

private:
  bool use_remote(common::ObLobLocatorV2 &locator) const {
    // All persistent LOB storage belongs to the shared process. This includes
    // namespace-local __all_* tables; routing those locators to the worker's
    // local LobManager loses the only process that owns their tablets.
    return locator.has_lob_header() && locator.is_persist_lob();
  }

  int materialize(common::ObLobTextIterCtx &ctx, common::ObLobLocatorV2 &locator) {
    if (!ctx.alloc_ || !locator.has_lob_header()) { return OB_INVALID_ARGUMENT; }
    auto *session = THIS_WORKER.get_session();
    StorageSessionScope scope(session);
    if (scope.error()) { return scope.error(); }
    const int64_t timeout = ctx.timeout_ts_ > 0 ? ctx.timeout_ts_ : THIS_WORKER.get_timeout_ts();
    ObString data;
    int ret = read_in_process_lob(locator, timeout, *ctx.alloc_, data);
    if (!ret && data.length() > UINT32_MAX) { ret = OB_INVALID_ARGUMENT; }
    if (!ret) {
      ctx.buff_ = data.empty() ? nullptr : data.ptr();
      ctx.buff_byte_len_ = static_cast<uint32_t>(data.length());
      ctx.content_byte_len_ = static_cast<uint32_t>(data.length());
      ctx.total_byte_len_ = data.length();
    }
    return ret;
  }

  common::ObILobReadService *local_ = nullptr;
};

int compare_in_process_lobs(ObLobLocatorV2 &left, ObLobLocatorV2 &right,
                            int64_t timeout, ObTxDesc &tx, bool &equal);

class InProcessDmlService final : public ObIDmlService {
public:
  int lob_binary_equal(
      ObLobLocatorV2 &left,
      ObLobLocatorV2 &right,
      int64_t timeout,
      ObTxDesc &tx,
      bool &equal) override {
    auto *session = THIS_WORKER.get_session();
    StorageSessionScope scope(session && session->get_tx_desc() == &tx ? session : nullptr);
    if (scope.error()) { return scope.error(); }
    return compare_in_process_lobs(left, right, timeout, tx, equal);
  }

  int prepare_execution(
      const ObDmlWriteSpec &write_spec,
      const ObDmlTablePlan &table_plan,
      const transaction::ObTxReadSnapshot &snapshot,
      common::ObIAllocator &allocator,
      const ObWriteContext &write_context,
      const concurrent_control::ObWriteFlag &write_flag,
      ObDmlExecution &execution) override {
    const auto view = table_plan.get_data_table();
    const auto &columns = table_plan.get_col_descs();
    const uint64_t table_id = view.is_valid() ? view.get_table_id() : write_spec.table_id_;
    ObSEArray<uint64_t, 16> effective_columns;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *logical_schema = nullptr;
    // Inner tables carry their schema even for namespace 1, matching the scan
    // path: the shared side must not re-resolve them through its own
    // SchemaService, whose lazy load needs inner SQL to this Worker.
    bool send_logical_schema = serves_namespace_schema()
        && !NamespaceForkKernelPrototype::is_encoded_id(table_id);
    StorageSpaceHandle storage_space =
        StorageSpaceHandle::namespace_space(serving_namespace());
    int ret = OB_SUCCESS;
    if (send_logical_schema) {
      ret = worker_local_table_schema(
          table_id, write_spec.schema_version_, schema_guard, logical_schema,
          storage_space);
      if (ret) {
        fprintf(stderr,
            "PROTOTYPE_V17_WORKER_WRITE_SCHEMA ns=%llu table=%llu version=%lld "
            "ret=%d found=%d actual_table=%llu actual_version=%lld database=%llu local=%d\n",
            (unsigned long long)serving_namespace(), (unsigned long long)table_id,
            (long long)write_spec.schema_version_, ret, logical_schema != nullptr,
            (unsigned long long)(logical_schema ? logical_schema->get_table_id() : OB_INVALID_ID),
            (long long)(logical_schema ? logical_schema->get_schema_version() : OB_INVALID_VERSION),
            (unsigned long long)(logical_schema ? logical_schema->get_database_id() : OB_INVALID_ID),
            storage_space.is_namespace());
      }
      send_logical_schema = !ret;
    } else if (columns.empty() && table_id != 0) {
      ObMultiVersionSchemaService *service = namespace_schema_service(serving_namespace());
      ret = service == nullptr ? OB_NOT_INIT
          : service->get_runtime_schema_guard(schema_guard, write_spec.schema_version_);
      if (!ret) { ret = schema_guard.get_table_schema(table_id, logical_schema); }
    }
    ObArray<const ObTableSchema *> materialization_schemas;
    if (!ret && send_logical_schema && storage_space.is_namespace()
        && serving_namespace() > 1) {
      ret = worker_materialization_schemas(
          *logical_schema, schema_guard, materialization_schemas);
    }
    if (ret) { return ret; }
    if (columns.empty() && logical_schema != nullptr) {
        for (int64_t i = 0; i < logical_schema->get_column_count(); ++i) {
          const auto *column = logical_schema->get_column_schema_by_idx(i);
          if (column != nullptr && !column->is_hidden()) { effective_columns.push_back(column->get_column_id()); }
        }
    }
    const int64_t column_count = columns.empty() ? effective_columns.count() : columns.count();
    if (!write_context.is_valid() || column_count == 0
        || column_count > OB_MAX_COLUMN_NUMBER || !write_spec.tz_info_) {
      fprintf(stderr,
          "PROTOTYPE_V22_WRITE_UNSUPPORTED table=%llu context=%d columns=%lld tz=%d\n",
          (unsigned long long)table_id, write_context.is_valid(),
          (long long)column_count, write_spec.tz_info_ != nullptr);
      return OB_NOT_SUPPORTED;
    }
    auto prepared = std::make_unique<InProcessExecution>();
    prepared->session = THIS_WORKER.get_session();
    StorageSessionScope scope(prepared->session);
    if (scope.error()) { return scope.error(); }
    auto &tx = *static_cast<ObTxDesc *>(write_context.native_handle());
    prepared->tx = &tx;
    prepared->txid = tx.get_tx_id().get_id(); prepared->deadline = write_spec.timeout_;
    for (int64_t i = 0; i < column_count; ++i) {
      const uint64_t column_id = columns.empty() ? effective_columns.at(i) : columns.at(i).col_id_;
      prepared->columns.push_back(column_id);
      if (!columns.empty()) { prepared->types.push_back(columns.at(i).col_type_); }
      else {
        if (logical_schema == nullptr || logical_schema->get_column_schema(column_id) == nullptr) { return OB_INVALID_ARGUMENT; }
        prepared->types.push_back(logical_schema->get_column_schema(column_id)->get_meta_type());
      }
    }
    execution.reset();
    if (!send_logical_schema || logical_schema == nullptr) { return OB_NOT_SUPPORTED; }
    const WritePrepareRequest request{storage_space, table_id, write_spec, *logical_schema,
        materialization_schemas, snapshot, write_flag, prepared->columns};
    ret = prepare_in_process_write(request, tx, prepared->handle);
    if (!ret) {
      if (!prepared->handle) { ret = OB_INVALID_ARGUMENT; }
      else { bind_execution(execution, prepared.release()); }
    }
    return ret; }
  int delete_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('D', tablet_id, tx_desc, execution, &column_ids, nullptr, row_iter, affected_rows); }
  int put_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('p', tablet_id, tx_desc, execution, &column_ids, nullptr, row_iter, affected_rows); }
  int insert_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('I', tablet_id, tx_desc, execution, &column_ids, nullptr, row_iter, affected_rows); }
  int write_rows(char operation, const ObTabletID &tablet_id, ObTxDesc &tx_desc,
      const ObDmlExecution &execution, const ObIArray<uint64_t> *column_ids,
      const ObIArray<uint64_t> *updated_column_ids, ObDatumRowIterator *row_iter, int64_t &affected_rows,
      int64_t lock_timeout = 0, ObRowLockMode lock_mode = ObRowLockMode::NONE,
      DuplicateRows *duplicates = nullptr, ObDuplicateReturnMode duplicate_mode = ObDuplicateReturnMode::ALL) {
    auto *state = static_cast<InProcessExecution *>(execution_state(execution));
    if (!state || !row_iter || (column_ids && column_ids->count() != static_cast<int64_t>(state->columns.size()))
        || state->txid != static_cast<uint64_t>(tx_desc.get_tx_id().get_id())) { return OB_INVALID_ARGUMENT; }
    StorageSessionScope scope(state->session);
    if (scope.error()) { return scope.error(); }
    const int64_t width = state->columns.size();
    for (int64_t i = 0; column_ids && i < width; ++i) {
      if (column_ids->at(i) != state->columns[i]) { return OB_INVALID_ARGUMENT; }
    }
    affected_rows = 0;
    int ret = OB_SUCCESS;
    bool end = false, duplicated = false;
    while (!ret && !end) {
      WriteBatch batch(operation, state->handle, tablet_id.id());
      batch.lock_timeout = lock_timeout;
      batch.lock_mode = lock_mode;
      batch.duplicate_mode = duplicate_mode;
      if (operation == 'L') { batch.bytes += 16; }
      if (duplicates) { batch.bytes += 8; }
      if (updated_column_ids) {
        if (updated_column_ids->count() > static_cast<int64_t>((MAX_SQL_MESSAGE - batch.bytes) / 8)) {
          return OB_SIZE_OVERFLOW;
        }
        batch.bytes += updated_column_ids->count() * 8;
        for (int64_t i = 0; i < updated_column_ids->count(); ++i) {
          batch.updated_columns.push_back(updated_column_ids->at(i));
        }
      }
      int64_t rows = 0;
      // Keep old/new pairs in the same batch: at most 32 logical writes.
      while (!ret && rows < (operation == 'U' ? 64 : 32)) {
        ObDatumRow *row = nullptr;
        ret = THIS_WORKER.check_status();
        if (!ret) { ret = row_iter->get_next_row(row); }
        if (ret == OB_ITER_END) { ret = OB_SUCCESS; end = true; break; }
        if (!ret && (!row || row->get_column_count() != width)) { ret = OB_INVALID_ARGUMENT; }
        for (int64_t i = 0; !ret && i < width; ++i) {
          ObObj value;
          ret = row->storage_datums_[i].to_obj_enhance(value, state->types[i]);
          if (!ret) {
            ret = batch.append(value, row->storage_datums_[i].has_lob_header());
          }
        }
        if (!ret) { ++rows; }
      }
      if (!ret && operation == 'U' && rows % 2) { ret = OB_INVALID_ARGUMENT; }
      if (!ret && rows) {
        batch.rows = rows;
        auto returned = std::make_unique<WriteResult>();
        int64_t affected = 0;
        ret = write_in_process_batch(tx_desc, batch, affected, *returned);
        if (duplicates && ret == OB_ERR_PRIMARY_KEY_DUPLICATE) { duplicated = true; ret = OB_SUCCESS; }
        if (!ret) {
          affected_rows += affected;
          if (duplicates) { duplicates->batches.push_back(std::move(returned)); }
        }
      }
    }
    return ret ? ret : duplicated ? OB_ERR_PRIMARY_KEY_DUPLICATE : OB_SUCCESS; }
  int insert_rows_fetch_duplicates(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      const common::ObIArray<uint64_t> &duplicated_column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      const ObDuplicateReturnMode return_mode,
      int64_t &affected_rows,
      blocksstable::ObDatumRowIterator *&duplicated_rows) override {
    if (duplicated_rows) { return OB_INVALID_ARGUMENT; }
    auto result = std::make_unique<DuplicateRows>(duplicated_column_ids.count());
    const int ret = write_rows('f', tablet_id, tx_desc, execution, &column_ids, &duplicated_column_ids,
                              row_iter, affected_rows, 0, ObRowLockMode::NONE, result.get(), return_mode);
    if (ret == OB_ERR_PRIMARY_KEY_DUPLICATE) { duplicated_rows = result.release(); }
    return ret;
  }
  void free_duplicate_rows_iterator(
      blocksstable::ObDatumRowIterator *iterator) override { delete iterator; }
  int update_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      const common::ObIArray<uint64_t> &updated_column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('U', tablet_id, tx_desc, execution, &column_ids, &updated_column_ids, row_iter, affected_rows); }
  int lock_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const int64_t abs_lock_timeout,
      const ObRowLockMode lock_mode,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) override {
    return write_rows('L', tablet_id, tx_desc, execution, nullptr, nullptr, row_iter, affected_rows, abs_lock_timeout, lock_mode); }
};
