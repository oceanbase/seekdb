class InProcessDirectInsertSession final : public ObIDirectInsertSession, public ObIDirectInsertWriterFactory {
public:
  ObIAllocator &allocator;
  DirectInsertScheduleRegistry &schedule_registry;
  int64_t ddl_task_id;
  std::shared_ptr<DirectInsertSchedule> schedule;
  sql::ObSQLSessionInfo *sqc_session;
  RequestTag origin;
  uint64_t generation;
  mutable std::atomic<int> error{OB_SUCCESS};
  InProcessDirectInsertSession(ObIAllocator &a,
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
    auto &a = allocator; this->~InProcessDirectInsertSession(); a.free(this);
    return ret;
  }
};

class InProcessDirectInsertWriter final : public ObIDirectInsertWriter {
public:
  ObIAllocator &allocator;
  InProcessDirectInsertSession &session;
  sql::ObSQLSessionInfo *sql_session;
  uint64_t id;
  ObTabletID tablet;
  int64_t slice, rows = 0;
  InProcessDirectInsertWriter(ObIAllocator &a, InProcessDirectInsertSession &s, uint64_t handle,
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
    auto &a = allocator; this->~InProcessDirectInsertWriter(); a.free(this);
  }
};
int InProcessDirectInsertSession::create(ObIAllocator &a, const ObDirectInsertWriterRequest &request,
    ObIDirectInsertWriter *&writer) {
  writer = nullptr;
  if (!request.is_valid()) { return OB_INVALID_ARGUMENT; }
  auto *storage = a.alloc(sizeof(InProcessDirectInsertWriter));
  if (!storage) { return OB_ALLOCATE_MEMORY_FAILED; }
  StorageSessionScope binding(THIS_WORKER.get_session());
  int ret = binding.error() ? binding.error() : error.load();
  uint64_t id = 0;
  if (!ret) { ret = create_in_process_direct_insert_writer(origin, generation, request, id); }
  if (!ret && !id) { ret = OB_INVALID_ARGUMENT; }
  if (ret) { int expected = OB_SUCCESS; error.compare_exchange_strong(expected, ret); }
  if (!ret) { writer = new (storage) InProcessDirectInsertWriter(a, *this, id, request); }
  else { a.free(storage); }
  return ret;
}

class InProcessDirectInsertService final : public IDirectInsertService {
public:
  int start(ObIAllocator &allocator, const ObDirectInsertStartParam &param,
      ObIDirectInsertWorkerContext &context, ObIDirectInsertSession *&session) override {
    session = nullptr;
    if (!param.is_valid()) { return OB_INVALID_ARGUMENT; }
    std::shared_ptr<DirectInsertSchedule> schedule =
        schedules.acquire(param.ddl_task_id_);
    if (!schedule) { return OB_ALLOCATE_MEMORY_FAILED; }
    auto *memory = allocator.alloc(sizeof(InProcessDirectInsertSession));
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
      else { session = new (memory) InProcessDirectInsertSession(
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
