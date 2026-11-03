// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual catalog/SQL binder/Rust driver with controlled transport and schema
// adapter. Does NOT emulate database isolation or prove real DDL rollback.
#pragma once

namespace extension_requires_test {
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::plugin;

inline void configure(ExtensionVersionRows &transport, bool missing = false, bool alias = false, bool incoming = false,
                      bool missing_temporary = false, bool alias_temporary = false)
{
  transport.transaction_status = transport.write_status = OB_SUCCESS;
  transport.rows.clear();
  transport.on_read = [=](ExtensionVersionRows &f) {
    CHECK(f.active);
    CHECK(f.sql.find("FOR UPDATE") != std::string::npos);
    f.rows.clear();
    ExtensionVersionRows::Row row;
    if (f.sql.find("SELECT extension_id,owner_id") == 0) {
      CHECK(f.sql.find("tenant_id=1 AND database_id=2") != std::string::npos);
      if (missing) return;
      const bool temporary = f.sql.find("extension_name='migration'") != std::string::npos;
      if (temporary && missing_temporary) return;
      row.id = temporary ? (alias_temporary ? 11 : 44) :
          f.sql.find("extension_name='alpha'") != std::string::npos ? 11 : alias ? 11 : 22;
    } else if (f.sql.find("SELECT next_value") == 0) row.id = 100;
    else if (f.sql.find("SELECT owner_id") == 0) row.id = 123;
    else if (f.sql.find("FROM __all_extension_dependency") != std::string::npos) {
      CHECK(f.sql.find("tenant_id=1 AND database_id=2 AND required_extension_id=100") != std::string::npos);
      if (!incoming) return;
      row.id = 200;
    } else CHECK(false);
    f.rows.push_back(row);
  };
}

class Installer final : public IExtensionSchemaInstaller {
public:
  explicit Installer(ExtensionVersionRows &transport) : transport_(transport) {}
  int preflight(const ExtensionInstallSpec &, std::string &) override { return OB_SUCCESS; }
  int apply(ObPluginSqlConnection &connection, const ExtensionInstallSpec &spec,
            std::vector<ExtensionMemberIdentity> &members, std::string &) override {
    CHECK(connection.is_in_transaction());
    CHECK(spec.requires_ == (std::vector<std::string>{"zulu", "alpha"}));
    CHECK(spec.prerequisites_ == (std::vector<std::string>{"migration"}));
    CHECK(transport_.queries.size() == 3 && transport_.writes == 0);
    CHECK(transport_.queries[0].find("extension_name='alpha'") != std::string::npos);
    CHECK(transport_.queries[1].find("extension_name='migration'") != std::string::npos);
    CHECK(transport_.queries[2].find("extension_name='zulu'") != std::string::npos);
    applied = true; members = {{1, 99}}; return apply_status;
  }
  ExtensionVersionRows &transport_;
  bool applied = false;
  int apply_status = OB_SUCCESS;
};

class Updater : public IExtensionSchemaUpdater {
public:
  bool admitted = false;
  int preflight(const ExtensionUpdateRequest &, std::string &) override { return OB_SUCCESS; }
  int admit(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &,
            const ExtensionUpdateSnapshot &snapshot, std::string &) override {
    CHECK(connection.is_in_transaction());
    CHECK(snapshot.installed_.requires_ == (std::vector<std::string>{"alpha", "zulu"}));
    admitted = true; return OB_SUCCESS;
  }
  int apply(ObPluginSqlConnection &, const ExtensionUpdateRequest &, const ExtensionUpdateSnapshot &,
            std::vector<ExtensionMemberIdentity> &, std::string &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
};

class VersionUpdater final : public Updater {
public:
  bool applied = false;
  int apply(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
            const ExtensionUpdateSnapshot &snapshot, std::vector<ExtensionMemberIdentity> &members,
            std::string &) override {
    CHECK(admitted && connection.is_in_transaction());
    CHECK(request.from_version_ == "1.0" && request.to_version_ == "1.1");
    CHECK(request.prerequisites_ == (std::vector<std::string>{"migration"}));
    CHECK(snapshot.installed_.version_ == "1.0");
    applied = true; members.clear(); return OB_SUCCESS;
  }
};

inline void snapshot_transport(ExtensionVersionRows &transport, bool incoming)
{
  transport.transaction_status = transport.write_status = OB_SUCCESS;
  transport.on_read = [=](ExtensionVersionRows &f) {
    CHECK(f.active);
    f.rows.clear();
    if (f.sql.find("SELECT extension_id,owner_id") == 0) {
      CHECK(f.sql.find("extension_name='consumer' FOR UPDATE") != std::string::npos);
      f.rows.push_back({100, 123, "1.0", "", ""});
    } else if (f.sql.find("SELECT object_class,object_id") == 0) {
      CHECK(f.sql.find("extension_id=100") != std::string::npos);
    } else if (f.sql.find("SELECT i.extension_name") == 0) {
      CHECK(f.sql.find("d.tenant_id=1 AND d.database_id=2 AND d.extension_id=100") != std::string::npos);
      f.rows.push_back({11, 123, "1.0", "", "alpha"});
      f.rows.push_back({22, 123, "1.0", "", "zulu"});
    } else if (f.sql.find("SELECT extension_id FROM __all_extension_dependency") == 0) {
      CHECK(f.sql.find("required_extension_id=100 FOR UPDATE") != std::string::npos);
      if (incoming) f.rows.push_back({200, 123, "1.0", "", ""});
    } else CHECK(false);
  };
}

class Dropper final : public IExtensionSchemaDropper {
public:
  bool admitted = false;
  int preflight(const ExtensionDropRequest &, std::string &) override { return OB_SUCCESS; }
  int admit(ObPluginSqlConnection &, const ExtensionDropRequest &, const ExtensionDropSnapshot &, std::string &) override {
    admitted = true; return OB_SUCCESS;
  }
  int apply(ObPluginSqlConnection &, const ExtensionDropSnapshot &, std::string &) override {
    CHECK(false); return OB_ERR_UNEXPECTED;
  }
};

inline void run(const char *package_root)
{
  ExtensionInstallSpec spec{1, 2, 123, "consumer", "1.0", "", {}, {"zulu", "alpha"}};
  spec.prerequisites_ = {"migration"};
  for (int scenario = 0; scenario < 7; ++scenario) {
    ExtensionVersionRows transport;
    configure(transport, scenario == 1, scenario == 2, false, scenario == 5, scenario == 6);
    ObPluginCatalog catalog;
    CHECK(catalog.init(&transport) == OB_SUCCESS);
    Installer installer(transport);
    if (scenario == 3) installer.apply_status = OB_TIMEOUT;
    if (scenario == 4) transport.write_status = OB_TIMEOUT;
    uint64_t id = 999;
    std::string error;
    const int ret = catalog.install_extension(spec, installer, id, error, &transport, 42);
    CHECK(!transport.active && transport.starts == 1 && transport.ends == 1);
    CHECK(transport.commits == std::vector<bool>{scenario == 0});
    if (scenario == 0) {
      CHECK(ret == OB_SUCCESS && id == 100 && installer.applied);
      CHECK(transport.written.size() == 6); // sequence seed/update, instance/member, two edges
      CHECK(transport.written[4].find("__all_extension_dependency") != std::string::npos);
      CHECK(transport.written[4].find("VALUES(1,2,11,100)") != std::string::npos);
      CHECK(transport.written[5].find("VALUES(1,2,22,100)") != std::string::npos);
    } else {
      CHECK(id == 0 && !error.empty());
      CHECK(ret == (scenario == 1 || scenario == 5 ? OB_ENTRY_NOT_EXIST :
                    scenario == 2 || scenario == 6 ? OB_ENTRY_EXIST : OB_TIMEOUT));
      if (scenario <= 2 || scenario >= 5) CHECK(!installer.applied);
      if (scenario != 4) CHECK(transport.writes == 0);
    }
  }
  for (bool incoming : {false, true}) {
    ExtensionVersionRows transport;
    configure(transport, false, false, incoming);
    ObPluginCatalog catalog;
    CHECK(catalog.init(&transport) == OB_SUCCESS);
    transport.active = true;
    ObPluginSqlConnection connection(&transport);
    std::string error;
    const int ret = catalog.record_extension_drop(connection, 1, 2, 100, 123, error);
    CHECK(ret == (incoming ? OB_STATE_NOT_MATCH : OB_SUCCESS));
    CHECK(transport.active && transport.starts == 0 && transport.ends == 0);
    if (incoming) CHECK(transport.writes == 0);
    else {
      CHECK(transport.written.size() == 3);
      CHECK(transport.written[0].find("DELETE FROM __all_extension_dependency") == 0);
      CHECK(transport.written[0].find("tenant_id=1 AND database_id=2 AND extension_id=100") != std::string::npos);
      CHECK(transport.written[2].find("DELETE FROM __all_extension_instance") == 0);
    }
  }
  for (bool changed_requirements : {false, true}) {
    ExtensionVersionRows transport;
    snapshot_transport(transport, false);
    ObPluginCatalog catalog;
    CHECK(catalog.init(&transport) == OB_SUCCESS);
    ExtensionUpdateRequest request{1, 2, "consumer", 100, "1.0", "1.0", {"zulu", "alpha"}};
    if (changed_requirements) request.requires_ = {"alpha"};
    Updater updater;
    uint64_t id = 999; bool changed = true; std::string error;
    const int ret = catalog.update_extension(request, updater, id, changed, error, &transport, 42);
    CHECK(ret == (changed_requirements ? OB_NOT_SUPPORTED : OB_SUCCESS));
    CHECK(id == (changed_requirements ? 0 : 100) && !changed && updater.admitted != changed_requirements);
    CHECK(transport.writes == 0 && transport.starts == 1 && transport.ends == 1 && !transport.active);
    CHECK(transport.commits == std::vector<bool>{!changed_requirements});
  }
  // A real version change can replace/remove providers. Use the actual catalog
  // SQL binder and Rust graph validator, not a second graph implementation.
  oceanbase::sql::ExtensionUpdatePlan dependency_plan;
  std::string planning_error;
  CHECK(dependency_plan.load(package_root, 1, 2, "consumer", {100, 123, "1.0", ""},
      "1.1", 0, planning_error) == OB_SUCCESS);
  CHECK(dependency_plan.request().requires_ == (std::vector<std::string>{"gamma", "zulu"}));
  CHECK(dependency_plan.request().prerequisites_ == (std::vector<std::string>{"migration"}));
  for (int scenario = 0; scenario < 8; ++scenario) {
    ExtensionVersionRows transport;
    snapshot_transport(transport, false);
    transport.affected_rows = [](const std::string &sql) -> int64_t {
      // The locked snapshot has no members; detach must report exactly zero.
      return sql.find("DELETE FROM __all_extension_member") == 0 ? 0 : 1;
    };
    const auto read_snapshot = transport.on_read;
    transport.on_read = [=](ExtensionVersionRows &f) {
      CHECK(f.active && !f.written.empty());
      CHECK(f.written[0].find("sql-extension-dependency-update") != std::string::npos);
      f.rows.clear();
      if (f.sql.find("SELECT extension_id,owner_id") == 0 &&
          f.sql.find("extension_name='consumer'") == std::string::npos) {
        CHECK(f.sql.find("tenant_id=1 AND database_id=2") != std::string::npos);
        CHECK(f.sql.find("FOR UPDATE") != std::string::npos);
        if (scenario == 3) return;
        const bool gamma = f.sql.find("extension_name='gamma'") != std::string::npos;
        const bool temporary = f.sql.find("extension_name='migration'") != std::string::npos;
        if (temporary && scenario == 6) return;
        f.rows.push_back({temporary ? (scenario == 7 ? 33 : 44) : gamma ? (scenario == 1 ? 300 : 33) : 22,
                          123, "1.0", "", ""});
      } else if (f.sql.find("SELECT required_extension_id,extension_id") == 0) {
        CHECK(f.sql.find("tenant_id=1 AND database_id=2 ORDER BY required_extension_id,extension_id FOR UPDATE") != std::string::npos);
        // Transport's integer columns 0/1 encode provider/consumer here.
        f.rows = {{11, 100, "", "", ""}, {22, 100, "", "", ""}, {100, 300, "", "", ""}};
        if (scenario == 4) f.read_status = OB_TIMEOUT;
      } else read_snapshot(f);
    };
    if (scenario == 5) transport.fail_write_at = 4; // after detach + old-edge deletion
    ObPluginCatalog catalog;
    CHECK(catalog.init(&transport) == OB_SUCCESS);
    ExtensionUpdateRequest request = dependency_plan.request();
    if (scenario == 2) request.requires_.clear();
    VersionUpdater updater;
    uint64_t id = 999; bool changed = false; std::string error;
    const int ret = catalog.update_extension(request, updater, id, changed, error, &transport, 42);
    const bool success = scenario == 0 || scenario == 2;
    const int expected = success ? OB_SUCCESS : scenario == 1 ? OB_OP_NOT_ALLOW :
        scenario == 3 || scenario == 6 ? OB_ENTRY_NOT_EXIST : scenario == 7 ? OB_INVALID_ARGUMENT : OB_TIMEOUT;
    if (ret != expected) std::cerr << "dependency update scenario=" << scenario << " status=" << ret
                                 << " expected=" << expected << " " << error << std::endl;
    CHECK(ret == expected);
    CHECK(id == (success ? 100 : 0) && changed == success);
    CHECK(transport.commits == std::vector<bool>{success});
    CHECK(transport.starts == 1 && transport.ends == 1 && !transport.active);
    if (success || scenario == 5) {
      CHECK(updater.admitted && updater.applied);
      CHECK(transport.written[1].find("DELETE FROM __all_extension_member") == 0);
      CHECK(transport.written[2].find("DELETE FROM __all_extension_dependency") == 0);
      if (scenario != 2) CHECK(transport.written[3].find("VALUES(1,2,33,100)") != std::string::npos);
      if (success) {
        for (const auto &sql : transport.written) CHECK(sql.find("VALUES(1,2,44,100)") == std::string::npos);
        CHECK(transport.written.size() == (scenario == 2 ? 4 : 6));
        CHECK(transport.written.back().find("UPDATE __all_extension_instance SET extension_version='1.1'") == 0);
        if (scenario == 0) CHECK(transport.written[4].find("VALUES(1,2,22,100)") != std::string::npos);
      }
    } else {
      CHECK(!updater.admitted && !updater.applied && transport.writes == 1); // fence only, rolled back
      if (scenario == 1) CHECK(error.find("cycle") != std::string::npos);
    }
  }
  for (int fault = 0; fault < 4; ++fault) {
    ExtensionVersionRows transport;
    snapshot_transport(transport, true);
    const auto read = transport.on_read;
    transport.on_read = [=](ExtensionVersionRows &f) {
      read(f);
      if (f.sql.find("SELECT extension_id FROM __all_extension_dependency") == 0) {
        if (fault == 1) f.fail_field = 0;
        if (fault == 2) f.close_status = OB_TIMEOUT;
        if (fault == 3) f.read_status = OB_TIMEOUT;
      }
    };
    ObPluginCatalog catalog;
    CHECK(catalog.init(&transport) == OB_SUCCESS);
    Dropper dropper;
    uint64_t id = 999; std::string error;
    const int expected = fault == 0 ? OB_STATE_NOT_MATCH : fault == 1 ? OB_ERR_NULL_VALUE : OB_TIMEOUT;
    CHECK(catalog.drop_extension({1, 2, "consumer", 100, false}, dropper, id, error, &transport, 42) == expected);
    CHECK(dropper.admitted && id == 0 && transport.writes == 0 && !transport.active);
    CHECK(transport.starts == 1 && transport.ends == 1);
    CHECK(transport.commits == std::vector<bool>{false});
    CHECK(!error.empty());
    if (fault == 0) CHECK(error.find("required by") != std::string::npos);
  }
}
} // namespace extension_requires_test
