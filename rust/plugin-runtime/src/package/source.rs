// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! In-memory entrance to the same owned package-source model. Host-only: source
//! validation is not SQL execution, session authority or transaction admission.
use super::{component, finish_read, input, Control, Error, Package, Script, INVALID, SQL_LIMIT};
use std::{collections::HashSet, ffi::c_char, mem::size_of, slice};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text {
    pub data: *const u8,
    pub length: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ScriptInput {
    pub from_version: Text,
    pub to_version: Text,
    pub sql: Text,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct SourceInput {
    pub struct_size: u32,
    pub relocatable: u32,
    pub name: Text,
    pub from_version: Text,
    pub version: Text,
    pub native_module: Text,
    pub schema: Text,
    pub dependencies: *const Text,
    pub dependency_count: u32,
    pub scripts: *const ScriptInput,
    pub script_count: u32,
    pub native_install: u32,
    pub prerequisites: *const Text,
    pub prerequisite_count: u32,
}

unsafe fn text<'a>(span: Text, limit: usize) -> Result<&'a str, Error> {
    unsafe { input(span.data, span.length, limit) }
}

unsafe fn snapshot(source: *const SourceInput) -> Result<Package, Error> {
    unsafe { snapshot_with_policy(source, 0) }
}

unsafe fn snapshot_with_policy(
    source: *const SourceInput,
    invoker_only: u32,
) -> Result<Package, Error> {
    if source.is_null() || unsafe { (*source).struct_size } < size_of::<SourceInput>() as u32 {
        return Err((INVALID, "missing or short in-memory package source"));
    }
    let source = unsafe { &*source };
    if source.relocatable > 1
        || source.native_install > 1
        || invoker_only > 1
        || source.dependency_count > 64
        || source.prerequisite_count > 64 - source.dependency_count
        || source.script_count > 1024
        || (source.dependency_count != 0 && source.dependencies.is_null())
        || (source.script_count != 0 && source.scripts.is_null())
        || (source.prerequisite_count != 0 && source.prerequisites.is_null())
    {
        return Err((INVALID, "invalid in-memory package arrays or flags"));
    }
    let name = unsafe { text(source.name, 255) }?;
    let from = unsafe { text(source.from_version, 255) }?;
    let version = unsafe { text(source.version, 255) }?;
    let native_module = unsafe { text(source.native_module, 255) }?;
    let schema = unsafe { text(source.schema, 255) }?;
    if !component(name)
        || !component(version)
        || (!from.is_empty() && !component(from))
        || (!native_module.is_empty()
            && !crate::extension_install::valid_native_module(native_module))
        || schema.chars().any(char::is_control)
        || (source.relocatable != 0 && !schema.is_empty())
        || (source.native_install != 0 && native_module.is_empty())
    {
        return Err((INVALID, "invalid in-memory package identity or namespace"));
    }
    let no_op = !from.is_empty() && from == version;
    let native_install = from.is_empty() && source.native_install != 0;
    if (source.script_count == 0) != (no_op || native_install) {
        return Err((
            INVALID,
            "zero scripts require a same-version update or explicit native installation",
        ));
    }
    let dependencies = if source.dependency_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(source.dependencies, source.dependency_count as usize) }
    };
    let scripts = if source.script_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(source.scripts, source.script_count as usize) }
    };
    let mut seen = HashSet::new();
    let prerequisites = if source.prerequisite_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(source.prerequisites, source.prerequisite_count as usize) }
    };
    if (no_op || native_install) && !prerequisites.is_empty() {
        return Err((
            INVALID,
            "zero-script source cannot declare intermediate prerequisites",
        ));
    }
    let mut requires = Vec::new();
    for &dependency in dependencies {
        let dependency = unsafe { text(dependency, 255) }?;
        if !component(dependency) || dependency == name || !seen.insert(dependency) {
            return Err((INVALID, "invalid, duplicate or self package dependency"));
        }
        requires.push(dependency.to_owned());
    }
    let mut total = 0;
    let mut temporary = Vec::new();
    for &dependency in prerequisites {
        let dependency = unsafe { text(dependency, 255) }?;
        if !component(dependency) || dependency == name || !seen.insert(dependency) {
            return Err((INVALID, "invalid, duplicate or self package prerequisite"));
        }
        temporary.push(dependency.to_owned());
    }
    let mut previous = from;
    let mut owned = Vec::new();
    for script in scripts {
        let from = unsafe { text(script.from_version, 255) }?;
        let to = unsafe { text(script.to_version, 255) }?;
        let sql = unsafe { text(script.sql, SQL_LIMIT - total) }?;
        if from != previous
            || !component(to)
            || from == to
            || (from.is_empty() && sql.trim().is_empty())
        {
            return Err((INVALID, "invalid in-memory SQL chain or empty base"));
        }
        total += sql.len();
        previous = to;
        owned.push(Script {
            from: from.to_owned(),
            to: to.to_owned(),
            sql: sql.to_owned(),
        });
    }
    if !no_op && !native_install && previous != version {
        return Err((
            INVALID,
            "in-memory SQL chain does not reach the requested version",
        ));
    }
    Ok(Package {
        name: name.to_owned(),
        from: from.to_owned(),
        version: version.to_owned(),
        control: Control {
            default_version: version.to_owned(),
            native_module: native_module.to_owned(),
            schema: schema.to_owned(),
            requires,
            relocatable: source.relocatable != 0,
            native_install: source.native_install != 0,
            invoker_only: invoker_only != 0,
            // In-memory sources already contain prepared SQL, not filesystem
            // controls; never substitute them a second time.
            ..Control::default()
        },
        scripts: owned,
        prerequisites: temporary,
    })
}

/// Deep-copy a preselected source without filesystem or catalog access.
/// # Safety
/// Source/arrays/spans are aligned, readable and immutable for this call and
/// satisfy their advertised lengths. Output/diagnostic are writable and disjoint
/// from each other and all inputs. On success the caller owns the package handle.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_from_source(
    source: *const SourceInput,
    output: *mut *mut Package,
    error: *mut c_char,
    capacity: u32,
) -> i32 {
    unsafe { finish_read(output, error, capacity, || snapshot(source)) }
}

/// Additive host entrance: preserve the existing source struct layout and
/// default policy rather than interpreting its former padding as authority.
/// # Safety
/// Same pointer/ownership contract as `seekdb_runtime_package_from_source`.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_from_source_with_policy(
    source: *const SourceInput,
    invoker_only: u32,
    output: *mut *mut Package,
    error: *mut c_char,
    capacity: u32,
) -> i32 {
    unsafe {
        finish_read(output, error, capacity, || {
            snapshot_with_policy(source, invoker_only)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ptr;
    fn span(text: &str) -> Text {
        Text {
            data: text.as_ptr(),
            length: text.len() as u32,
        }
    }
    fn source(scripts: &[ScriptInput]) -> SourceInput {
        SourceInput {
            struct_size: size_of::<SourceInput>() as u32,
            relocatable: 0,
            name: span("memory_ops"),
            from_version: span(""),
            version: span("2"),
            native_module: span("org.memory"),
            schema: span(""),
            dependencies: ptr::null(),
            dependency_count: 0,
            scripts: scripts.as_ptr(),
            script_count: scripts.len() as u32,
            native_install: 0,
            prerequisites: ptr::null(),
            prerequisite_count: 0,
        }
    }
    #[test]
    fn invoker_policy_is_owned_and_validated() {
        let scripts = [ScriptInput {
            from_version: span(""),
            to_version: span("2"),
            sql: span("SELECT 1;"),
        }];
        let input = source(&scripts);
        assert!(!unsafe { snapshot(&input) }.unwrap().control.invoker_only);
        let package = unsafe { snapshot_with_policy(&input, 1) }.unwrap();
        assert!(package.control.invoker_only);
        assert!(unsafe { snapshot_with_policy(&input, 2) }.is_err());
    }

    #[test]
    fn native_source_is_explicit_and_distinct_from_update_scripts() {
        let mut input = source(&[]);
        assert!(unsafe { snapshot(&input) }.is_err());
        input.native_install = 1;
        let package = unsafe { snapshot(&input) }.unwrap();
        assert!(
            package.control.native_install && package.scripts.is_empty() && package.from.is_empty()
        );
        input.native_module = span("");
        assert!(unsafe { snapshot(&input) }.is_err());
        input.native_module = span("org.test");
        input.native_install = 2;
        assert!(unsafe { snapshot(&input) }.is_err());
        input.native_install = 1;
        let scripts = [ScriptInput {
            from_version: span("1"),
            to_version: span("2"),
            sql: span("SELECT 1;"),
        }];
        input.scripts = scripts.as_ptr();
        input.script_count = 1;
        assert!(unsafe { snapshot(&input) }.is_err());
        input.from_version = span("1");
        assert_eq!(unsafe { snapshot(&input) }.unwrap().scripts.len(), 1);
        input.script_count = 0;
        assert!(unsafe { snapshot(&input) }.is_err());
        input.from_version = span("2");
        assert!(unsafe { snapshot(&input) }.unwrap().scripts.is_empty());
    }
    #[test]
    fn owns_ordered_sources_and_supports_update_only_and_noop() {
        let mut body = "CREATE PROCEDURE p() BEGIN SELECT 'a;b'; END; -- tail".to_owned();
        let scripts = [
            ScriptInput {
                from_version: span(""),
                to_version: span("1"),
                sql: span(&body),
            },
            ScriptInput {
                from_version: span("1"),
                to_version: span("2"),
                sql: span(""),
            },
        ];
        let mut source = source(&scripts);
        let package = unsafe { snapshot(&source) }.unwrap();
        body.clear();
        assert!(package.scripts[0].sql.ends_with("-- tail"));
        assert_eq!(package.scripts[1].sql, "");
        assert_eq!(package.control.native_module, "org.memory");
        source.from_version = span("1");
        source.scripts = scripts[1..].as_ptr();
        source.script_count = 1;
        assert_eq!(unsafe { snapshot(&source) }.unwrap().scripts.len(), 1);
        source.from_version = span("2");
        source.scripts = ptr::null();
        source.script_count = 0;
        assert!(unsafe { snapshot(&source) }.unwrap().scripts.is_empty());
    }
    #[test]
    fn prerequisites_are_owned_disjoint_bounded_and_require_scripts() {
        let scripts = [ScriptInput {
            from_version: span(""),
            to_version: span("2"),
            sql: span("SELECT 1;"),
        }];
        let mut input = source(&scripts);
        let final_dependencies = [span("base")];
        let mut temporary_name = "migration".to_owned();
        let temporary = [span(&temporary_name)];
        input.dependencies = final_dependencies.as_ptr();
        input.dependency_count = 1;
        input.prerequisites = temporary.as_ptr();
        input.prerequisite_count = 1;
        let package = unsafe { snapshot(&input) }.unwrap();
        temporary_name.clear();
        assert_eq!(package.prerequisites, ["migration"]);
        assert_eq!(package.control.requires, ["base"]);
        for names in [
            vec![span("base")],
            vec![span("memory_ops")],
            vec![span("a"), span("a")],
            vec![span("../escape")],
        ] {
            input.prerequisites = names.as_ptr();
            input.prerequisite_count = names.len() as u32;
            assert!(unsafe { snapshot(&input) }.is_err());
        }
        input.prerequisites = ptr::null();
        input.prerequisite_count = 1;
        assert!(unsafe { snapshot(&input) }.is_err());
        let temporary = [span("migration")];
        input.prerequisites = temporary.as_ptr();
        input.prerequisite_count = 64; // Rejected before reading the short array.
        assert!(unsafe { snapshot(&input) }.is_err());
        input.prerequisite_count = 1;
        input.script_count = 0;
        input.from_version = span("2");
        assert!(unsafe { snapshot(&input) }.is_err());
        input.from_version = span("");
        input.native_install = 1;
        assert!(unsafe { snapshot(&input) }.is_err());
    }
    #[test]
    fn rejects_inconsistent_identity_paths_and_metadata() {
        let scripts = [ScriptInput {
            from_version: span(""),
            to_version: span("2"),
            sql: span("SELECT 1;"),
        }];
        let base = source(&scripts);
        let bad_dep = [span("memory_ops")];
        let duplicate = [span("other"), span("other")];
        let mut cases = Vec::new();
        let mut case = base;
        case.name = span("../escape");
        cases.push(case);
        let mut case = base;
        case.from_version = span("1");
        cases.push(case);
        let mut case = base;
        case.from_version = span("2");
        cases.push(case);
        let mut case = base;
        case.version = span("3");
        cases.push(case);
        let mut case = base;
        case.schema = span("db");
        case.relocatable = 1;
        cases.push(case);
        let mut case = base;
        case.schema = span("x\ny");
        cases.push(case);
        let mut case = base;
        case.native_module = span("Upper.Module");
        cases.push(case);
        let mut case = base;
        case.dependencies = bad_dep.as_ptr();
        case.dependency_count = 1;
        cases.push(case);
        let mut case = base;
        case.dependencies = duplicate.as_ptr();
        case.dependency_count = 2;
        cases.push(case);
        let mut case = base;
        case.script_count = 1025;
        cases.push(case);
        let mut case = base;
        case.dependency_count = 65;
        cases.push(case);
        let mut case = base;
        case.scripts = ptr::null();
        cases.push(case);
        let mut case = base;
        case.struct_size = 0;
        cases.push(case);
        for case in cases {
            assert!(unsafe { snapshot(&case) }.is_err());
        }
    }
    #[test]
    fn invalid_sql_and_ffi_failure_clear_output() {
        let large = "x".repeat(SQL_LIMIT + 1);
        for sql in [" ", "SELECT '\0'", large.as_str()] {
            let scripts = [ScriptInput {
                from_version: span(""),
                to_version: span("2"),
                sql: span(sql),
            }];
            let mut out = ptr::dangling_mut();
            let mut error = [1i8; 80];
            assert_eq!(
                unsafe {
                    seekdb_runtime_package_from_source(
                        &source(&scripts),
                        &mut out,
                        error.as_mut_ptr(),
                        80,
                    )
                },
                INVALID
            );
            assert!(out.is_null() && error[0] != 0 && error.contains(&0));
        }
        let first = "x".repeat(SQL_LIMIT);
        let scripts = [
            ScriptInput {
                from_version: span(""),
                to_version: span("1"),
                sql: span(&first),
            },
            ScriptInput {
                from_version: span("1"),
                to_version: span("2"),
                sql: span("x"),
            },
        ];
        assert!(unsafe { snapshot(&source(&scripts)) }.is_err());
        let mut bad = scripts[1];
        bad.sql = Text {
            data: b"\xff".as_ptr(),
            length: 1,
        };
        let scripts = [scripts[0], bad];
        assert!(unsafe { snapshot(&source(&scripts)) }.is_err());
    }
}
