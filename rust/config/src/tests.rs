// Copyright (c) 2025 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use super::*;
use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEST_DIR: AtomicU64 = AtomicU64::new(1);
static CONFIG_TEST_LOCK: Mutex<()> = Mutex::new(());

struct TestDirectory {
    path: PathBuf,
    store: Option<kv::Store>,
}
impl TestDirectory {
    fn new() -> Self {
        let id = NEXT_TEST_DIR.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!("config-{}-{id}", std::process::id()));
        fs::create_dir(&path).unwrap();
        let store = Some(kv::Store::open(path.join("config")).unwrap());
        Self { path, store }
    }
    fn storage_path(&self) -> PathBuf {
        self.path.join("config")
    }
    fn store(&self) -> &kv::Store {
        self.store.as_ref().unwrap()
    }
    fn reopen(&mut self) {
        self.store.take();
        self.store = Some(kv::Store::open(self.storage_path()).unwrap());
    }
}
impl Drop for TestDirectory {
    fn drop(&mut self) {
        self.store.take();
        fs::remove_dir_all(&self.path).unwrap();
    }
}
fn reset_active_defaults(store: &kv::Store) {
    crate::config::load_active(store, true).unwrap();
}
fn raw_put(store: &kv::Store, name: &str, value: &[u8]) {
    let mut tx = store.begin().unwrap();
    tx.put(&key(name), value).unwrap();
    tx.commit().unwrap();
}

#[test]
fn updates_survive_close_reopen_and_reset_removes_only_its_key() {
    let _guard = CONFIG_TEST_LOCK.lock().unwrap();
    let mut directory = TestDirectory::new();
    assert!(load(directory.store()).unwrap().is_empty());
    crate::config::update_parameter(directory.store(), "cpu_count", Some("4")).unwrap();
    crate::config::update_parameter(directory.store(), "data_dir", Some("quote' and slash\\"))
        .unwrap();
    directory.reopen();
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::cpu_count(), 4);
    assert_eq!(crate::config::data_dir(), "quote' and slash\\");
    crate::config::update_parameter(directory.store(), "cpu_count", None).unwrap();
    directory.reopen();
    let entries = load(directory.store()).unwrap();
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].name, "data_dir");
}

#[test]
fn concurrent_updates_keep_each_completed_value() {
    let _guard = CONFIG_TEST_LOCK.lock().unwrap();
    let mut directory = TestDirectory::new();
    let store = directory.store();
    let updates = [
        ("cpu_count", "4"),
        ("rpc_timeout", "3s"),
        ("max_string_print_length", "777"),
        ("enable_record_trace_log", "off"),
    ];
    std::thread::scope(|scope| {
        for (name, value) in updates {
            scope.spawn(move || crate::config::update_parameter(store, name, Some(value)).unwrap());
        }
    });
    directory.reopen();
    let entries = load(directory.store()).unwrap();
    let actual: BTreeMap<_, _> = entries
        .iter()
        .map(|entry| (entry.name.as_str(), entry.value.as_str()))
        .collect();
    assert_eq!(actual, updates.into_iter().collect());
}

#[test]
fn invalid_utf8_and_unknown_entries_block_startup_without_publication() {
    let _guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    reset_active_defaults(directory.store());
    raw_put(directory.store(), "cpu_count", b"4");
    raw_put(directory.store(), "data_dir", b"\xff");
    assert!(crate::config::load_active(directory.store(), true)
        .unwrap_err()
        .message
        .contains("UTF-8"));
    assert_eq!(crate::config::cpu_count(), 0);
    raw_put(directory.store(), "data_dir", b"store");
    raw_put(directory.store(), "unknown", b"1");
    let error = crate::config::load_active(directory.store(), true).unwrap_err();
    assert_eq!(error.name.as_deref(), Some("unknown"));
    assert_eq!(crate::config::cpu_count(), 0);
}

#[test]
fn startup_skips_write_policy_but_requires_storable_values() {
    let _guard = CONFIG_TEST_LOCK.lock().unwrap();
    let mut directory = TestDirectory::new();
    reset_active_defaults(directory.store());
    assert!(crate::config::validate("cpu_count", "-1").is_err());
    raw_put(directory.store(), "cpu_count", b"-1");
    directory.reopen();
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::cpu_count(), -1);
    raw_put(directory.store(), "cpu_count", b"invalid");
    assert_eq!(
        crate::config::load_active(directory.store(), false)
            .unwrap_err()
            .name
            .as_deref(),
        Some("cpu_count")
    );
    assert_eq!(crate::config::cpu_count(), -1);
}

#[test]
fn other_namespaces_are_preserved_and_excluded_from_configuration() {
    let _guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    let store = directory.store();
    let mut tx = store.begin().unwrap();
    tx.put(b"merge/status", b"idle").unwrap();
    tx.commit().unwrap();
    crate::config::update_parameter(directory.store(), "cpu_count", Some("2")).unwrap();
    assert_eq!(load(directory.store()).unwrap().len(), 1);
    crate::config::update_parameter(directory.store(), "cpu_count", None).unwrap();
    assert!(load(directory.store()).unwrap().is_empty());
    assert_eq!(
        store.get(b"merge/status").unwrap().as_deref(),
        Some(b"idle".as_slice())
    );
}

#[test]
fn declared_cpu_count_update_preserves_storage_and_runtime_value_on_rejection() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    reset_active_defaults(directory.store());
    assert_eq!(crate::config::cpu_count(), 0);
    crate::config::update_parameter(directory.store(), "CPU_COUNT", Some("8")).unwrap();
    assert_eq!(crate::config::cpu_count(), 8);
    assert_eq!(load(directory.store()).unwrap()[0].value, "8");
    assert!(crate::config::update_parameter(directory.store(), "cpu_count", Some("-1")).is_err());
    assert_eq!(crate::config::cpu_count(), 8);
    assert_eq!(load(directory.store()).unwrap()[0].value, "8");
    crate::config::update_parameter(directory.store(), "cpu_count", None).unwrap();
}

#[test]
fn declared_defaults_have_typed_readers() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    reset_active_defaults(directory.store());
    assert!(crate::config::enable_record_trace_log());
    assert_eq!(crate::config::datafile_size(), 32 * 1024 * 1024);
    assert_eq!(crate::config::internal_sql_execute_timeout(), 30_000_000);
    assert_eq!(crate::config::cpu_quota_concurrency(), 10.0);
    assert_eq!(crate::config::data_dir(), "store");
    crate::config::apply("enable_record_trace_log", "off").unwrap();
    crate::config::apply("datafile_size", "5MB").unwrap();
    crate::config::apply("internal_sql_execute_timeout", "250ms").unwrap();
    crate::config::apply("cpu_quota_concurrency", "1.5").unwrap();
    crate::config::apply("data_dir", "other").unwrap();
    assert!(!crate::config::enable_record_trace_log());
    assert_eq!(crate::config::datafile_size(), 5 * 1024 * 1024);
    assert_eq!(crate::config::internal_sql_execute_timeout(), 250_000);
    assert_eq!(crate::config::cpu_quota_concurrency(), 1.5);
    assert_eq!(crate::config::data_dir(), "other");
    crate::config::apply("enable_record_trace_log", "True").unwrap();
    crate::config::apply("datafile_size", "32M").unwrap();
    crate::config::apply("internal_sql_execute_timeout", "30s").unwrap();
    crate::config::apply("cpu_quota_concurrency", "10").unwrap();
    crate::config::apply("data_dir", "store").unwrap();
}

#[test]
fn declared_numeric_ranges_reject_without_publishing() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    reset_active_defaults(directory.store());
    assert_eq!(crate::config::rpc_port(), 2882);
    assert!(crate::config::update_parameter(directory.store(), "rpc_port", Some("1024")).is_err());
    assert_eq!(crate::config::rpc_port(), 2882);
    crate::config::update_parameter(directory.store(), "rpc_port", Some("1025")).unwrap();
    assert_eq!(crate::config::rpc_port(), 1025);
    assert!(crate::config::update_parameter(directory.store(), "rpc_port", Some("65536")).is_err());
    assert_eq!(crate::config::rpc_port(), 1025);
    crate::config::update_parameter(directory.store(), "rpc_port", Some("2882")).unwrap();

    assert!(crate::config::update_parameter(
        directory.store(),
        "cpu_quota_concurrency",
        Some("0.99")
    )
    .is_err());
    assert_eq!(crate::config::cpu_quota_concurrency(), 10.0);
    assert!(
        crate::config::update_parameter(directory.store(), "datafile_size", Some("-1M")).is_err()
    );
    assert!(
        crate::config::update_parameter(directory.store(), "datafile_size", Some("5")).is_err()
    );
    assert_eq!(crate::config::datafile_size(), 32 * 1024 * 1024);
    assert!(crate::config::update_parameter(
        directory.store(),
        "internal_sql_execute_timeout",
        Some("999us")
    )
    .is_err());
    assert_eq!(crate::config::internal_sql_execute_timeout(), 30_000_000);
}

#[test]
fn rust_validation_checks_types_and_ranges_without_business_checkers() {
    for parameter in crate::config::CATALOG {
        crate::config::validate(parameter.name, parameter.default)
            .unwrap_or_else(|error| panic!("{}: {error}", parameter.name));
    }
    assert!(crate::config::validate("not_declared", "1").is_err());
    assert!(crate::config::validate("server_create_time", "123").is_err());
    assert!(crate::config::validate("major_freeze_duty_time", "24:00").is_err());
    assert!(crate::config::validate("major_freeze_duty_time", "disable").is_ok());
    assert!(crate::config::validate("_parallel_ddl_control", "CREATE_INDEX:on").is_ok());
    assert!(crate::config::validate("_parallel_ddl_control", "UNKNOWN:on").is_ok());
    assert!(crate::config::validate("default_table_organization", "HEAP").is_ok());
    assert!(crate::config::validate("default_table_organization", "UNKNOWN").is_ok());
    assert!(crate::config::validate("syslog_level", "share.pt:trace").is_ok());
}

#[test]
fn declared_moment_value_has_typed_reader() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    reset_active_defaults(directory.store());
    let moment = crate::config::major_freeze_duty_time_parts();
    assert!(!moment.disabled);
    assert_eq!((moment.hour, moment.minute), (2, 0));
    crate::config::apply("major_freeze_duty_time", "disable").unwrap();
    assert!(crate::config::major_freeze_duty_time_parts().disabled);
    crate::config::apply("major_freeze_duty_time", "02:00").unwrap();
}

#[test]
fn declared_dynamic_and_static_updates_follow_storage_and_effective_state() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    crate::config::load_active(directory.store(), true).unwrap();
    crate::config::update_parameter(directory.store(), "rpc_port", Some("3001")).unwrap();
    assert_eq!(crate::config::rpc_port(), 3001);
    assert_eq!(
        crate::config::effective_value("rpc_port").as_deref(),
        Some("3001")
    );
    assert_eq!(
        crate::config::configured_value("rpc_port").as_deref(),
        Some("3001")
    );
    let port_row = crate::config::parameter_row(
        crate::config::CATALOG
            .iter()
            .position(|parameter| parameter.name == "rpc_port")
            .unwrap(),
    );
    assert_eq!(port_row.value, "3001");
    assert_eq!(port_row.source, "DEFAULT");
    crate::config::update_parameter(directory.store(), "enable_record_trace_log", Some("off"))
        .unwrap();
    assert_eq!(
        crate::config::effective_value("enable_record_trace_log").as_deref(),
        Some("False")
    );

    crate::config::update_parameter(directory.store(), "net_thread_count", Some("5")).unwrap();
    assert_eq!(crate::config::net_thread_count(), 0);
    assert_eq!(
        crate::config::effective_value("net_thread_count").as_deref(),
        Some("0")
    );
    assert_eq!(
        crate::config::configured_value("net_thread_count").as_deref(),
        Some("5")
    );
    crate::config::load_active(directory.store(), false).unwrap();
    assert_eq!(crate::config::net_thread_count(), 0);
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::net_thread_count(), 5);
    crate::config::update_parameter(directory.store(), "net_thread_count", None).unwrap();
    crate::config::update_parameter(directory.store(), "rpc_port", None).unwrap();
    crate::config::update_parameter(directory.store(), "enable_record_trace_log", None).unwrap();
    assert_eq!(crate::config::rpc_port(), 2882);
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::net_thread_count(), 0);
    assert!(load(directory.store()).unwrap().is_empty());
}

#[test]
fn bootstrap_overrides_are_effective_before_writeback() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    crate::config::load_active(directory.store(), true).unwrap();
    crate::config::bootstrap_set("memory_budget", "2G").unwrap();
    crate::config::bootstrap_set("net_thread_count", "3").unwrap();
    assert_eq!(crate::config::memory_budget(), 2 * 1024 * 1024 * 1024);
    assert_eq!(crate::config::net_thread_count(), 3);
    assert!(load(directory.store()).unwrap().is_empty());
    assert!(crate::config::bootstrap_set("server_create_time", "1").is_err());
    crate::config::save_bootstrap(directory.store()).unwrap();
    let entries = load(directory.store()).unwrap();
    assert_eq!(entries.len(), 2);
    assert_eq!(
        entries
            .iter()
            .find(|entry| entry.name == "memory_budget")
            .unwrap()
            .value,
        "2G"
    );
    assert_eq!(
        entries
            .iter()
            .find(|entry| entry.name == "net_thread_count")
            .unwrap()
            .value,
        "3"
    );
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::net_thread_count(), 3);
}

#[test]
fn checker_rejection_and_reentry_leave_storage_unchanged() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    crate::config::load_active(directory.store(), true).unwrap();
    let rejected =
        crate::config::update_parameter_checked(directory.store(), "cpu_count", Some("4"), || {
            Err(Error::new(None, "rejected"))
        });
    assert!(rejected.is_err());
    assert!(load(directory.store()).unwrap().is_empty());
    let reentered =
        crate::config::update_parameter_checked(directory.store(), "cpu_count", Some("4"), || {
            crate::config::snapshot(&["cpu_count"]).map(|_| ())
        });
    assert!(reentered.unwrap_err().message.contains("reentered"));
    assert!(load(directory.store()).unwrap().is_empty());
}

#[test]
fn internal_state_is_loaded_and_published_without_becoming_a_parameter() {
    let _config_guard = CONFIG_TEST_LOCK.lock().unwrap();
    let directory = TestDirectory::new();
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::server_create_time(), 0);
    assert_eq!(crate::config::server_role_info(), "");
    crate::config::update_internal_state(directory.store(), "server_create_time", "123").unwrap();
    crate::config::update_internal_state(
        directory.store(),
        "server_role_info",
        "PRIMARY:INVALID:NORMAL:0",
    )
    .unwrap();
    assert_eq!(crate::config::server_create_time(), 123);
    assert_eq!(
        crate::config::server_role_info(),
        "PRIMARY:INVALID:NORMAL:0"
    );
    assert!(crate::config::find("server_create_time").is_none());
    assert!(crate::config::update_internal_state(directory.store(), "cpu_count", "124").is_err());
    crate::config::update_internal_state(directory.store(), "server_create_time", "124").unwrap();
    crate::config::load_active(directory.store(), true).unwrap();
    assert_eq!(crate::config::server_create_time(), 124);
}
