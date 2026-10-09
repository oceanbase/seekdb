// The SQL plan owns column descriptions, expressions and scan parameters.
// This iterator only owns Namespace admission and the native scan it protects.
int invoke_native_scan(sql::ObSQLSessionInfo &session, ObTableScanParam &param,
                       const std::function<int()> &operation);

class NamespaceScanIterator final : public ObNewRowIterator {
public:
  explicit NamespaceScanIterator(ObVTableScanParam &param)
      : param_(param), virtual_(is_virtual_table(param.index_id_)) {}
  ~NamespaceScanIterator() override { reset(); }

  int open() {
    session_ = param_.op_ ? param_.op_->get_eval_ctx().exec_ctx_.get_my_session()
                         : THIS_WORKER.get_session();
    const uint64_t namespace_id = in_process_session_ns(session_);
    if (namespace_id == 0) { return OB_NOT_INIT; }
    space_ = is_inner_table(param_.index_id_) || !uses_global_storage_scope()
        ? StorageSpaceHandle::namespace_space(namespace_id) : StorageSpaceHandle::global_space();
    service_ = virtual_ ? effective_virtual_table_scan(session_)
                        : share::server_service<ObITabletScan>();
    if (service_ == nullptr) { return OB_NOT_INIT; }
    int ret = OB_SUCCESS;
    if (!virtual_) {
      auto &scan = static_cast<ObTableScanParam &>(param_);
      requested_snapshot_ = scan.fb_snapshot_;
      access_ = std::make_unique<ns::TabletAccess>();
      ret = prepare(*access_);
    }
    if (ret == OB_SUCCESS) {
      ret = invoke([&] { return service_->table_scan(param_, iter_); });
    }
    return ret;
  }

  int reuse(bool switch_param) {
    if (virtual_) {
      close_iterator();
      return OB_SUCCESS;
    }
    return iter_ == nullptr ? OB_NOT_INIT : service_->reuse_scan_iter(switch_param, iter_);
  }

  int rescan() {
    if (virtual_) {
      close_iterator();
      return service_->table_scan(param_, iter_);
    }
    auto &scan = static_cast<ObTableScanParam &>(param_);
    int ret = OB_SUCCESS;
    std::unique_ptr<ns::TabletAccess> next_access;
    if (scan.need_switch_param_) {
      // SQL may already have installed the next logical tablet. Only undo our
      // previous routing when the request still contains the resolved source.
      restore_routing();
      next_access = std::make_unique<ns::TabletAccess>();
      ret = prepare(*next_access);
    }
    if (ret == OB_SUCCESS) {
      ret = invoke([&] { return service_->table_rescan(param_, iter_); });
    }
    if (ret == OB_SUCCESS && next_access) {
      // Keep both admissions until the native iterator has switched sources.
      access_ = std::move(next_access);
    } else if (ret != OB_SUCCESS) {
      close_iterator();
      access_.reset();
    }
    return ret;
  }

  int advance() {
    return invoke([&] { return service_->table_advance_scan(param_, iter_); });
  }
  int get_next_row(ObNewRow *&row) override {
    return iter_ == nullptr ? OB_NOT_INIT : iter_->get_next_row(row);
  }
  int get_next_row() override {
    return iter_ == nullptr ? OB_NOT_INIT : iter_->get_next_row();
  }
  int get_next_rows(int64_t &count, int64_t capacity) override {
    return iter_ == nullptr ? OB_NOT_INIT : iter_->get_next_rows(count, capacity);
  }
  int get_diagnosis_info(ObRowDiagnosisInfo *info) override {
    return iter_ == nullptr ? OB_NOT_INIT : iter_->get_diagnosis_info(info);
  }
  void reset() override {
    close_iterator();
    restore_routing();
    access_.reset();
  }
private:
  int prepare(ns::TabletAccess &access) {
    auto &scan = static_cast<ObTableScanParam &>(param_);
    data_plane::ObNamespaceAccessMode mode;
    ns::NamespaceCatalogViews::Handle view;
    int ret = storage_access_mode(space_, mode);
    if (ret == OB_SUCCESS) {
      ret = find_statement_read_view(space_.tablet_namespace_id(),
          scan.snapshot_.core_.version_.get_val_for_tx(), view);
    }
    if (ret == OB_SUCCESS) {
      ret = access.prepare_scan(space_.tablet_namespace_id(), mode, scan, view);
    }
    return ret;
  }
  int invoke(const std::function<int()> &operation) {
    return virtual_ ? operation()
        : invoke_native_scan(*session_, static_cast<ObTableScanParam &>(param_), operation);
  }
  void restore_routing() {
    if (!virtual_ && access_) {
      auto &scan = static_cast<ObTableScanParam &>(param_);
      if (scan.tablet_id_ == access_->tablet()) {
        scan.tablet_id_ = access_->schema_tablet();
      }
      scan.fb_snapshot_ = requested_snapshot_;
      scan.schema_tablet_id_.reset();
    }
  }
  void close_iterator() {
    if (iter_ != nullptr) {
      service_->revert_scan_iter(iter_);
      iter_ = nullptr;
    }
  }
  ObVTableScanParam &param_;
  const bool virtual_;
  sql::ObSQLSessionInfo *session_ = nullptr;
  StorageSpaceHandle space_;
  share::SCN requested_snapshot_;
  std::unique_ptr<ns::TabletAccess> access_;
  ObITabletScan *service_ = nullptr;
  ObNewRowIterator *iter_ = nullptr;
};

class InProcessTabletScan final : public ObIVirtualTableScan {
public:
  int table_scan(ObVTableScanParam &param, ObNewRowIterator *&iter) override {
    if (iter != nullptr) { return OB_INVALID_ARGUMENT; }
    auto scan = std::make_unique<NamespaceScanIterator>(param);
    const int ret = scan->open();
    if (ret == OB_SUCCESS) { iter = scan.release(); }
    return ret;
  }
  int revert_scan_iter(ObNewRowIterator *iter) override {
    delete iter;
    return OB_SUCCESS;
  }
  int reuse_scan_iter(bool switch_param, ObNewRowIterator *iter) override {
    auto *scan = static_cast<NamespaceScanIterator *>(iter);
    return scan == nullptr ? OB_SUCCESS : scan->reuse(switch_param);
  }
  int table_rescan(ObVTableScanParam &, ObNewRowIterator *iter) override {
    auto *scan = static_cast<NamespaceScanIterator *>(iter);
    return scan == nullptr ? OB_INVALID_ARGUMENT : scan->rescan();
  }
  int table_advance_scan(ObVTableScanParam &, ObNewRowIterator *iter) override {
    auto *scan = static_cast<NamespaceScanIterator *>(iter);
    return scan == nullptr ? OB_INVALID_ARGUMENT : scan->advance();
  }
};
