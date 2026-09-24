/**
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
use super::*;
use std::fs;
use std::sync::atomic::{AtomicU64, Ordering};

struct Fixture(PathBuf);

impl Fixture {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        loop {
            let path = std::env::temp_dir().join(format!(
                "seekdb-extension-files-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            match fs::create_dir(&path) {
                Ok(()) => return Self(path),
                Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
                Err(error) => panic!("cannot create fixture: {error}"),
            }
        }
    }

    fn put(&self, name: &str, text: &str) {
        let path = self.0.join(name);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn read(&self, name: &str, from: Option<&str>, to: &str) -> Result<Package, Error> {
        read_package(self.0.to_str().unwrap(), name, from, to)
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}

#[test]
fn selected_path_preserves_the_strongest_superuser_requirement() {
    let fixture = Fixture::new();
    fixture.put("policy.control", "default_version = '3'\nsuperuser = false");
    fixture.put("policy--1.sql", "SELECT 1;");
    fixture.put("policy--1--2.sql", "SELECT 2;");
    fixture.put("policy--2--3.sql", "SELECT 3;");
    assert!(
        fixture
            .read("policy", None, "")
            .unwrap()
            .control
            .invoker_only
    );
    fixture.put("policy--2.control", "superuser = true");
    for from in [None, Some("1")] {
        assert!(
            !fixture
                .read("policy", from, "3")
                .unwrap()
                .control
                .invoker_only
        );
    }
    // The old installed version and unrelated versions are not executed again.
    for from in [Some("2"), Some("3")] {
        assert!(
            fixture
                .read("policy", from, "3")
                .unwrap()
                .control
                .invoker_only
        );
    }
    fixture.put("policy--1.control", "superuser = true");
    assert!(
        !fixture
            .read("policy", None, "1")
            .unwrap()
            .control
            .invoker_only
    );
    fixture.put("policy.control", "default_version = '3'");
    fixture.put("policy--3.control", "superuser = false");
    assert!(
        !fixture
            .read("policy", None, "3")
            .unwrap()
            .control
            .invoker_only
    );
}

#[test]
fn flat_and_legacy_packages_share_version_planning() {
    let fixture = Fixture::new();
    for prefix in ["", "demo/"] {
        fixture.put(&format!("{prefix}demo.control"), "default_version = '2'");
        fixture.put(&format!("{prefix}demo--1.sql"), "SELECT 1;");
        fixture.put(&format!("{prefix}demo--1--2.sql"), "SELECT 2;");
        // Files from other packages must not contaminate this version graph.
        fixture.put(
            &format!("{prefix}other--broken--name--ignored.sql"),
            "invalid",
        );
        if !prefix.is_empty() {
            assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
            fs::remove_file(fixture.0.join("demo.control")).unwrap();
        }
        let package = fixture.read("demo", None, "").unwrap();
        assert_eq!(package.scripts.len(), 2);
        assert_eq!(package.scripts[0].sql, "SELECT 1;");
        assert_eq!(package.scripts[1].sql, "SELECT 2;");
        let update = fixture.read("demo", Some("1"), "2").unwrap();
        assert_eq!(update.scripts.len(), 1);
        assert_eq!(update.scripts[0].sql, "SELECT 2;");
        assert!(fixture
            .read("demo", Some("2"), "2")
            .unwrap()
            .scripts
            .is_empty());
    }
}

#[test]
fn relative_script_directory_and_per_version_module_substitution() {
    let fixture = Fixture::new();
    fixture.put("demo.control", "default_version = '2'\ndirectory = 'sql/demo'\nmodule_pathname = '$libdir/demo-v1'\nnative_module = 'org.demo'");
    fixture.put(
        "sql/demo/demo--1.sql",
        "SELECT 'MODULE_PATHNAME'; -- MODULE_PATHNAME",
    );
    fixture.put("sql/demo/demo--1--2.sql", "SELECT 'MODULE_PATHNAME';");
    fixture.put(
        "sql/demo/demo--2.control",
        "module_pathname = '$libdir/demo-v2'",
    );
    let package = fixture.read("demo", None, "").unwrap();
    assert_eq!(package.control.native_module, "org.demo");
    assert_eq!(
        package.scripts[0].sql,
        "SELECT '$libdir/demo-v1'; -- $libdir/demo-v1"
    );
    assert_eq!(package.scripts[1].sql, "SELECT '$libdir/demo-v2';");
    let update = fixture.read("demo", Some("1"), "2").unwrap();
    assert_eq!(update.scripts[0].sql, "SELECT '$libdir/demo-v2';");
    assert!(!inspect_install_source(&fixture.0, "demo").unwrap());
    fixture.put("sql/demo/demo--unused.control", "trusted = true");
    assert!(fixture.read("demo", None, "").is_ok());
    assert!(inspect_install_source(&fixture.0, "demo").is_err());
}

#[test]
fn unconfigured_module_token_is_literal_and_not_an_activation_request() {
    let fixture = Fixture::new();
    fixture.put("demo.control", "default_version = '1'");
    fixture.put("demo--1.sql", "SELECT 'MODULE_PATHNAME';");
    assert_eq!(
        fixture.read("demo", None, "").unwrap().scripts[0].sql,
        "SELECT 'MODULE_PATHNAME';"
    );
    fixture.put(
        "demo.control",
        "default_version = '1'\nmodule_pathname = '$libdir/demo'",
    );
    let package = fixture.read("demo", None, "").unwrap();
    assert!(package.control.native_module.is_empty());
    assert!(!package.control.native_install);
    // We prepare source only, not LANGUAGE C parsing or dynamic loading.
    assert_eq!(package.scripts[0].sql, "SELECT '$libdir/demo';");
}

#[test]
fn module_expansion_is_bounded_before_allocation() {
    assert_eq!(
        expand_module_pathname("MODULE_PATHNAME".into(), Some("abc"), 3).unwrap(),
        "abc"
    );
    assert!(expand_module_pathname("MODULE_PATHNAME".into(), Some("abcd"), 3).is_err());
    let fixture = Fixture::new();
    fixture.put(
        "demo.control",
        &format!(
            "default_version = '2'\nmodule_pathname = '{}'",
            "x".repeat(4096)
        ),
    );
    fixture.put("demo--1.sql", &"MODULE_PATHNAME".repeat(1024));
    fixture.put("demo--1--2.sql", "x");
    assert_eq!(
        fixture.read("demo", None, "1").unwrap().scripts[0]
            .sql
            .len(),
        SQL_LIMIT
    );
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
    fixture.put("demo--1.sql", &"MODULE_PATHNAME".repeat(1025));
    assert!(matches!(fixture.read("demo", None, "1"), Err((INVALID, _))));
}

#[test]
fn shrinking_substitutions_do_not_bypass_the_source_read_limit() {
    let fixture = Fixture::new();
    fixture.put(
        "demo.control",
        "default_version = '2'\nmodule_pathname = 'x'",
    );
    let sql = "MODULE_PATHNAME".repeat(SQL_LIMIT / 14 / 2 + 1);
    fixture.put("demo--1.sql", &sql);
    fixture.put("demo--1--2.sql", &sql);
    assert!(fixture.read("demo", None, "1").is_ok());
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
}

#[test]
fn expansion_in_one_version_bounds_later_unsubstituted_scripts() {
    let fixture = Fixture::new();
    fixture.put("demo.control", "default_version = '2'");
    fixture.put(
        "demo--1.control",
        &format!("module_pathname = '{}'", "x".repeat(4096)),
    );
    fixture.put("demo--1.sql", &"MODULE_PATHNAME".repeat(1024));
    fixture.put("demo--1--2.sql", "x");
    assert_eq!(
        fixture.read("demo", None, "1").unwrap().scripts[0]
            .sql
            .len(),
        SQL_LIMIT
    );
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
    // The primary has no substitution, and an update must not replay its base.
    assert_eq!(
        fixture.read("demo", Some("1"), "2").unwrap().scripts[0].sql,
        "x"
    );
}

#[test]
fn invalid_controls_never_fall_back_or_ignore_directory_overrides() {
    let fixture = Fixture::new();
    fixture.put("demo.control", "default_version = '1'\ntrusted = true");
    fixture.put("demo--1.sql", "SELECT 1;");
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
    for control in [
        "default_version = '1'\ndirectory = ''",
        "default_version = '1'\ndirectory = '../'",
        "default_version = '1'\nmodule_pathname = ''",
    ] {
        fixture.put("demo.control", control);
        assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
    }
    fixture.put("demo.control", "default_version = '1'");
    fixture.put("demo--1.control", "directory = 'sql'");
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
}

#[cfg(unix)]
#[test]
fn symlinked_controls_and_scripts_cannot_escape_the_root() {
    use std::os::unix::fs::symlink;
    let fixture = Fixture::new();
    let outside = Fixture::new();
    outside.put("demo.control", "default_version = '1'");
    outside.put("demo--1.sql", "SELECT 1;");
    symlink(
        outside.0.join("demo.control"),
        fixture.0.join("demo.control"),
    )
    .unwrap();
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
    fs::remove_file(fixture.0.join("demo.control")).unwrap();
    fixture.put("demo.control", "default_version = '1'\ndirectory = 'sql'");
    symlink(&outside.0, fixture.0.join("sql")).unwrap();
    assert!(matches!(fixture.read("demo", None, ""), Err((INVALID, _))));
}
