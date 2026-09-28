// Included inside oceanbase::observer::namespace_worker_prototype.
int find_template_namespace(const char *name, uint64_t &id)
{
  id = 0;
  auto *access = share::server_service<storage::ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(access->instance_meta_store());
  rootserver::InstanceNamespaceRecord record;
  int ret = directory.find_live(name, ObTimeUtility::current_time() + 120 * 1000 * 1000,
      record);
  if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_SUCCESS; }
  if (OB_SUCC(ret)) { id = record.id; }
  return ret;
}
std::string quote_sql_identifier(const std::string &name)
{
  std::string quoted = "`";
  for (char ch : name) {
    quoted += ch;
    if (ch == '`') { quoted += '`'; }
  }
  quoted += '`';
  return quoted;
}
int require_raw_schema_id(uint64_t id, uint64_t &raw_id)
{
  if (NamespaceForkKernelPrototype::is_encoded_id(id)) { return OB_INVALID_ARGUMENT; }
  raw_id = id;
  return OB_SUCCESS;
}
int ensure_template_namespace()
{
  uint64_t template_id = 0;
  uint64_t build_id = 0;
  int ret = find_template_namespace("__template__", template_id);
  if (OB_FAIL(ret) || template_id != 0) { return ret; }
  if (OB_FAIL(find_template_namespace("__template_build__", build_id))) { return ret; }
  if (build_id == 0) {
    ns::NamespaceRuntime *source = nullptr;
    if (!ns::namespace_registry().get(1, source) || source == nullptr) {
      ret = OB_NOT_INIT;
    } else {
      ret = NamespaceForkKernelPrototype::control_namespace(
          ObString::make_string(source->ns().name()),
          ObString::make_string("__template_build__"), build_id);
    }
  }
  if (OB_FAIL(ret)) {
  } else if (ns::namespace_registry().add(build_id, "") != 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ensure_in_process_namespace(build_id))) {
  } else if (OB_FAIL(inprocess_refresh_schema(build_id))) {
  } else {
    InProcessServingScope serving(build_id);
    auto *schema_service = namespace_schema_service(build_id);
    auto *sql_proxy = namespace_sql_proxy(build_id);
    std::vector<std::string> user_databases;
    std::vector<std::string> user_views;
    std::vector<std::string> user_tables;
    std::vector<std::string> user_routines;
    std::vector<std::string> user_accounts;
    if (schema_service == nullptr || sql_proxy == nullptr) {
      ret = OB_NOT_INIT;
    } else {
      ret = schema_service->refresh_and_add_schema(false);
    }
    if (OB_SUCC(ret)) {
      ObSchemaGetterGuard guard;
      ObArray<const ObDatabaseSchema *> databases;
      ObArray<const ObTableSchema *> tables;
      ObArray<const ObRoutineInfo *> routines;
      ObArray<const ObUserInfo *> users;
      std::map<uint64_t, std::string> system_databases;
      if (OB_FAIL(schema_service->get_runtime_schema_guard(guard))) {
      } else if (OB_FAIL(guard.get_database_schemas_in_runtime(databases))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < databases.count(); ++i) {
          const auto &database = *databases.at(i);
          uint64_t local_id = 0;
          if (OB_FAIL(require_raw_schema_id(database.get_database_id(), local_id))) {
          } else if (is_inner_db(local_id)) {
            const ObString name = database.get_database_name_str();
            system_databases.emplace(local_id, std::string(name.ptr(), name.length()));
          } else if (!is_inner_db(local_id)) {
            const ObString name = database.get_database_name_str();
            user_databases.emplace_back(name.ptr(), name.length());
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_table_schemas_in_runtime(tables))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
          const auto &table = *tables.at(i);
          if (!table.is_user_table() && !table.is_view_table()) { continue; }
          uint64_t local_db_id = 0;
          uint64_t local_table_id = 0;
          if (OB_FAIL(require_raw_schema_id(table.get_database_id(), local_db_id))) {
          } else if (OB_FAIL(require_raw_schema_id(table.get_table_id(), local_table_id))) {
          } else if (is_inner_table(local_table_id)) {
          } else {
            const auto database = system_databases.find(local_db_id);
            if (database != system_databases.end()) {
              const ObString name = table.get_table_name_str();
              const std::string qualified = quote_sql_identifier(database->second) + "."
                  + quote_sql_identifier(std::string(name.ptr(), name.length()));
              (table.is_view_table() ? user_views : user_tables).push_back(qualified);
            }
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_routine_infos_in_runtime(routines))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < routines.count(); ++i) {
          const auto &routine = *routines.at(i);
          if (routine.get_package_id() != OB_INVALID_ID) { continue; }
          uint64_t local_db_id = 0;
          if (OB_FAIL(require_raw_schema_id(routine.get_database_id(), local_db_id))) {
          } else if (is_inner_db(local_db_id)) {
            const auto database = system_databases.find(local_db_id);
            if (database != system_databases.end()) {
              const ObString name = routine.get_routine_name();
              const std::string qualified = quote_sql_identifier(database->second) + "."
                  + quote_sql_identifier(std::string(name.ptr(), name.length()));
              user_routines.push_back(std::string(routine.is_function()
                  ? "DROP FUNCTION IF EXISTS " : "DROP PROCEDURE IF EXISTS ") + qualified);
            }
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_user_schemas_in_runtime(users))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); ++i) {
          const auto &user = *users.at(i);
          uint64_t local_id = 0;
          if (OB_FAIL(require_raw_schema_id(user.get_user_id(), local_id))) {
          } else if (!is_inner_object_id(local_id)) {
            const ObString name = user.get_user_name_str();
            const ObString host = user.get_host_name_str();
            const std::string account = quote_sql_identifier(std::string(name.ptr(), name.length()))
                + "@" + quote_sql_identifier(std::string(host.ptr(), host.length()));
            user_accounts.push_back(std::string(user.is_role()
                ? "DROP ROLE IF EXISTS " : "DROP USER IF EXISTS ") + account);
          }
        }
      }
    }
    for (const auto &command : user_routines) {
      if (OB_FAIL(ret)) { break; }
      int64_t affected_rows = 0;
      ret = sql_proxy->write(command.c_str(), affected_rows);
    }
    for (const auto &view : user_views) {
      if (OB_FAIL(ret)) { break; }
      int64_t affected_rows = 0;
      ret = sql_proxy->write(("DROP VIEW IF EXISTS " + view).c_str(), affected_rows);
    }
    for (const auto &table : user_tables) {
      if (OB_FAIL(ret)) { break; }
      int64_t affected_rows = 0;
      ret = sql_proxy->write(("DROP TABLE IF EXISTS " + table).c_str(), affected_rows);
    }
    for (const auto &name : user_databases) {
      if (OB_FAIL(ret)) { break; }
      const std::string command = "DROP DATABASE IF EXISTS " + quote_sql_identifier(name);
      int64_t affected_rows = 0;
      ret = sql_proxy->write(command.c_str(), affected_rows);
    }
    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ret = sql_proxy->write("CREATE DATABASE IF NOT EXISTS test", affected_rows);
    }
    for (const auto &command : user_accounts) {
      if (OB_FAIL(ret)) { break; }
      int64_t affected_rows = 0;
      ret = sql_proxy->write(command.c_str(), affected_rows);
    }
    if (OB_SUCC(ret) && OB_FAIL(schema_service->refresh_and_add_schema(false))) {
    } else if (OB_SUCC(ret)) {
      ObSchemaGetterGuard guard;
      ObArray<const ObDatabaseSchema *> databases;
      ObArray<const ObTableSchema *> tables;
      ObArray<const ObRoutineInfo *> routines;
      ObArray<const ObUserInfo *> users;
      bool default_database_found = false;
      if (OB_FAIL(schema_service->get_runtime_schema_guard(guard))) {
      } else if (OB_FAIL(guard.get_database_schemas_in_runtime(databases))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < databases.count(); ++i) {
          const auto &database = *databases.at(i);
          uint64_t local_id = 0;
          if (OB_FAIL(require_raw_schema_id(database.get_database_id(), local_id))) {
          } else if (database.get_database_name_str() == "test") {
            default_database_found = true;
          } else if (!is_inner_db(local_id)) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
        if (OB_SUCC(ret) && !default_database_found) { ret = OB_ERR_UNEXPECTED; }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_table_schemas_in_runtime(tables))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
          const auto &table = *tables.at(i);
          if (!table.is_user_table() && !table.is_view_table()) { continue; }
          uint64_t local_db_id = 0;
          uint64_t local_table_id = 0;
          if (OB_FAIL(require_raw_schema_id(table.get_database_id(), local_db_id))) {
          } else if (OB_FAIL(require_raw_schema_id(table.get_table_id(), local_table_id))) {
          } else if (is_inner_db(local_db_id) && !is_inner_table(local_table_id)) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_routine_infos_in_runtime(routines))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < routines.count(); ++i) {
          const auto &routine = *routines.at(i);
          uint64_t local_db_id = 0;
          if (OB_FAIL(require_raw_schema_id(routine.get_database_id(), local_db_id))) {
          } else if (is_inner_db(local_db_id) && routine.get_package_id() == OB_INVALID_ID) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_user_schemas_in_runtime(users))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); ++i) {
          uint64_t local_id = 0;
          if (OB_FAIL(require_raw_schema_id(users.at(i)->get_user_id(), local_id))) {
          } else if (!is_inner_object_id(local_id)) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      auto *access = share::server_service<storage::ObAccessService>();
      if (access == nullptr) {
        ret = OB_NOT_INIT;
      } else {
        rootserver::InstanceNamespaceDirectory directory(access->instance_meta_store());
        ret = directory.rename_live(build_id, "__template_build__", "__template__",
            ObTimeUtility::current_time() + 120 * 1000 * 1000);
        if (OB_SUCC(ret)) { ns::namespace_registry().remove(build_id); }
      }
    }
    LOG_INFO("PROTOTYPE_TEMPLATE_MIGRATION", K(ret), K(build_id),
        "filtered_databases", user_databases.size(),
        "filtered_views", user_views.size(), "filtered_tables", user_tables.size(),
        "filtered_routines", user_routines.size(),
        "filtered_accounts", user_accounts.size());
  }
  return ret;
}
int restore_namespace_registry() {
  auto *access = share::server_service<storage::ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  rootserver::InstanceNamespaceDirectory directory(access->instance_meta_store());
  std::vector<rootserver::InstanceNamespaceRecord> records;
  int ret = directory.list_live(ObTimeUtility::current_time() + 120 * 1000 * 1000, records);
  for (const auto &record : records) {
    if (OB_FAIL(ret)) { break; }
    if (record.name == "__template__" || record.name == "__template_build__") {
      continue;
    }
    char name_buf[ns::Namespace::MAX_NAME_LEN];
    if (record.id == 0 || record.id >= ns::NamespaceObjectKey::NAMESPACE_LIMIT
        || record.name.empty() || record.name.size() >= sizeof(name_buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(name_buf, record.name.data(), record.name.size());
      name_buf[record.name.size()] = '\0';
      if (ns::namespace_registry().add(record.id, name_buf) != 0) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  if (OB_SUCC(ret)) { ret = ensure_template_namespace(); }
  return ret;
}
