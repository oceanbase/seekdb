// Included inside oceanbase::observer::namespace_worker_prototype.
int find_template_namespace(const char *name, uint64_t &id)
{
  id = 0;
  const char *query = strcmp(name, "__template__") == 0
      ? "SELECT namespace_id FROM __fork_proto_meta.namespaces WHERE state=0 AND name='__template__'"
      : "SELECT namespace_id FROM __fork_proto_meta.namespaces WHERE state=0 AND name='__template_build__'";
  ObMySQLProxy::MySQLResult result;
  sqlclient::ObMySQLResult *rows = nullptr;
  int ret = GCTX.sql_proxy_->read(result, query);
  if (OB_SUCC(ret) && OB_ISNULL(rows = result.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_SUCC(ret)) {
    const int next_ret = rows->next();
    if (next_ret != OB_ITER_END) {
      ret = next_ret == OB_SUCCESS ? rows->get_uint(0L, id) : next_ret;
    }
  }
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
int ensure_legacy_template_namespace()
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
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, database.get_database_id(), local_id))) {
          } else if (is_inner_db(local_id)) {
            const ObString name = database.get_database_name_str();
            system_databases.emplace(local_id, std::string(name.ptr(), name.length()));
          } else if (!is_inner_db(local_id)
                     && !database.get_database_name_str().prefix_match("__fork_proto_meta")) {
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
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, table.get_database_id(), local_db_id))) {
          } else if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, table.get_table_id(), local_table_id))) {
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
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, routine.get_database_id(), local_db_id))) {
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
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, user.get_user_id(), local_id))) {
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
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, database.get_database_id(), local_id))) {
          } else if (database.get_database_name_str() == "test") {
            default_database_found = true;
          } else if (!is_inner_db(local_id)
                     && !database.get_database_name_str().prefix_match("__fork_proto_meta")) {
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
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, table.get_database_id(), local_db_id))) {
          } else if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, table.get_table_id(), local_table_id))) {
          } else if (is_inner_db(local_db_id) && !is_inner_table(local_table_id)) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_routine_infos_in_runtime(routines))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < routines.count(); ++i) {
          const auto &routine = *routines.at(i);
          uint64_t local_db_id = 0;
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, routine.get_database_id(), local_db_id))) {
          } else if (is_inner_db(local_db_id) && routine.get_package_id() == OB_INVALID_ID) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(guard.get_user_schemas_in_runtime(users))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); ++i) {
          uint64_t local_id = 0;
          if (OB_FAIL(NamespaceForkKernelPrototype::local_object_id(
                  build_id, users.at(i)->get_user_id(), local_id))) {
          } else if (!is_inner_object_id(local_id)) {
            ret = OB_ERR_UNEXPECTED;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObSqlString query;
      int64_t affected_rows = 0;
      if (OB_FAIL(query.assign_fmt(
              "UPDATE __fork_proto_meta.namespaces SET name='__template__' "
              "WHERE namespace_id=%lu AND state=0 AND name='__template_build__'",
              build_id))) {
      } else if (OB_FAIL(GCTX.sql_proxy_->write(query.ptr(), affected_rows))) {
      } else if (affected_rows != 1) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ns::namespace_registry().remove(build_id);
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
  if (!GCTX.sql_proxy_) { return OB_NOT_INIT; }
  ObMySQLProxy::MySQLResult result;
  sqlclient::ObMySQLResult *rows = nullptr;
  int ret = GCTX.sql_proxy_->read(result,
      "SELECT namespace_id,name FROM __fork_proto_meta.namespaces "
      "WHERE state=0 AND name NOT IN ('__template__','__template_build__') "
      "ORDER BY namespace_id");
  if (OB_SUCC(ret) && OB_ISNULL(rows = result.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  }
  while (OB_SUCC(ret)) {
    ret = rows->next();
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
    uint64_t namespace_id = 0;
    ObString name;
    char name_buf[ns::Namespace::MAX_NAME_LEN];
    if (OB_FAIL(rows->get_uint(0L, namespace_id))) {
    } else if (OB_FAIL(rows->get_varchar(1L, name))) {
    } else if (namespace_id == 0 || namespace_id >= ns::NamespaceObjectKey::NAMESPACE_LIMIT
               || name.empty() || name.length() >= sizeof(name_buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(name_buf, name.ptr(), name.length());
      name_buf[name.length()] = '\0';
      if (ns::namespace_registry().add(namespace_id, name_buf) != 0) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  if (OB_SUCC(ret)) { ret = ensure_legacy_template_namespace(); }
  return ret;
}
