// Included inside oceanbase::observer::namespace_worker_prototype.
int load_namespace_registry() {
  // Keep snapshot acquisition and publication ordered when startup, a timer
  // and client discovery overlap.
  static std::mutex load_mutex;
  std::lock_guard<std::mutex> load_guard(load_mutex);
  auto *access = share::server_service<storage::ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  // Capture removal candidates before reading the directory. A locally
  // created namespace published after this snapshot must not be removed.
  std::vector<uint64_t> registered;
  ns::namespace_registry().list_ids(registered);
  rootserver::InstanceNamespaceDirectory directory(access->instance_meta_store());
  std::vector<rootserver::InstanceNamespaceRecord> records;
  int ret = directory.list_live(ObTimeUtility::current_time() + 120 * 1000 * 1000, records);
  std::vector<uint64_t> live;
  for (const auto &record : records) {
    if (OB_FAIL(ret)) { break; }
    char name_buf[ns::Namespace::MAX_NAME_LEN];
    if (record.id == 0 || record.id >= ns::NamespaceObjectKey::NAMESPACE_LIMIT
        || record.name.empty() || record.name.size() >= sizeof(name_buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(name_buf, record.name.data(), record.name.size());
      name_buf[record.name.size()] = '\0';
      if (ns::namespace_registry().add(record.id, name_buf, record.allow_login) != 0) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        live.push_back(record.id);
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (uint64_t id : registered) {
      if (std::find(live.begin(), live.end(), id) == live.end()) {
        ns::namespace_registry().remove(id);
      }
    }
  }
  return ret;
}
int restore_namespace_registry() {
  return load_namespace_registry();
}
