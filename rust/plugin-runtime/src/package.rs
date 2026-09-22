// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Host-only, bounded package source reader. No SQL execution or module loading.
//! The administrator owns the package directory and must publish immutable
//! versioned packages; canonical-path checks are not a sandbox against an
//! administrator racing file replacement. Returned text is an owned snapshot.
use crate::{native::IO_ERROR, registration::NO_MEMORY, INVALID, OK};
use std::alloc::{alloc, Layout};
use std::collections::HashSet;
use std::ffi::c_char;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::{ptr, slice, str};
pub mod source;
mod versions;

const CONTROL_LIMIT: usize = 64 * 1024;
const SQL_LIMIT: usize = 4 * 1024 * 1024;
const DIRECTORY_LIMIT: usize = 4096;
const NOT_FOUND: i32 = 8;
type Error = (i32, &'static str);

#[derive(Clone, Default, Debug)]
struct Control {
    default_version: String,
    native_module: String,
    schema: String,
    requires: Vec<String>,
    relocatable: bool,
    native_install: bool,
}

pub struct Package {
    name: String,
    from: String, // Empty for fresh installation; otherwise the expected installed version.
    version: String,
    control: Control,
    scripts: Vec<Script>,
    prerequisites: Vec<String>, // Selected intermediate dependencies, excluding final requires.
}

struct Script {
    from: String, // Empty only for the initial installation script.
    to: String,
    sql: String,
}

pub(crate) fn component(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 255
        && value.as_bytes()[0].is_ascii_alphanumeric()
        && value
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || b"._-".contains(&c))
        && !value.contains("..")
        && !value.contains("--")
}

// Control grammar is deliberately separate from plugin.toml: one key/value
// per line, single-quoted strings with doubled quotes, or unquoted booleans.
fn value(input: &str) -> Result<String, Error> {
    let input = input.trim();
    if let Some(input) = input.strip_prefix('\'') {
        let mut chars = input.chars().peekable();
        let mut output = String::new();
        while let Some(c) = chars.next() {
            if c != '\'' {
                output.push(c);
            } else if chars.peek() == Some(&'\'') {
                chars.next();
                output.push('\'');
            } else {
                let tail: String = chars.collect();
                if tail.trim().is_empty() || tail.trim_start().starts_with('#') {
                    return Ok(output);
                }
                return Err((INVALID, "trailing text after control string"));
            }
        }
        Err((INVALID, "unterminated control string"))
    } else {
        let raw = input.split('#').next().unwrap_or("").trim();
        if raw == "true" || raw == "false" {
            Ok(raw.to_owned())
        } else {
            Err((INVALID, "control strings require single quotes"))
        }
    }
}

fn parse_control(text: &str) -> Result<Control, Error> {
    parse_control_overlay(text, Control::default(), false)
}

fn parse_control_overlay(
    text: &str,
    mut control: Control,
    secondary: bool,
) -> Result<Control, Error> {
    let mut seen = HashSet::new();
    if text.len() > CONTROL_LIMIT || text.contains('\0') {
        return Err((INVALID, "invalid control size or NUL"));
    }
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (key, raw) = line
            .split_once('=')
            .ok_or((INVALID, "missing control assignment"))?;
        let key = key.trim();
        if secondary && matches!(key, "default_version" | "directory") {
            return Err((
                INVALID,
                "secondary control cannot set default_version or directory",
            ));
        }
        if !seen.insert(key) {
            return Err((INVALID, "duplicate control key"));
        }
        if key != "relocatable" && !raw.trim_start().starts_with('\'') {
            return Err((INVALID, "control strings require single quotes"));
        }
        let parsed = value(raw)?;
        if parsed.chars().any(char::is_control) {
            return Err((INVALID, "control value contains control character"));
        }
        match key {
            "default_version" => {
                if !component(&parsed) {
                    return Err((INVALID, "invalid default version"));
                }
                control.default_version = parsed;
            }
            "native_module" => {
                if parsed.is_empty() || !crate::extension_install::valid_native_module(&parsed) {
                    return Err((INVALID, "invalid logical native module"));
                }
                control.native_module = parsed;
            }
            "install_source" => {
                control.native_install = match parsed.as_str() {
                    "sql" => false,
                    "native" => true,
                    _ => return Err((INVALID, "install_source must be sql or native")),
                };
            }
            "schema" => {
                if parsed.is_empty() || parsed.len() > 255 {
                    return Err((INVALID, "invalid schema name"));
                }
                control.schema = parsed;
            }
            "relocatable" => {
                if !matches!(raw.split('#').next().unwrap_or("").trim(), "true" | "false") {
                    return Err((INVALID, "relocatable must be an unquoted boolean"));
                }
                control.relocatable = parsed == "true";
            }
            "requires" => {
                control.requires.clear(); // Explicit empty overrides inherited dependencies.
                let mut dependencies = HashSet::new();
                if !parsed.is_empty() {
                    for name in parsed.split(',').map(str::trim) {
                        if !component(name) || !dependencies.insert(name) || dependencies.len() > 64
                        {
                            return Err((
                                INVALID,
                                "invalid, duplicate or excessive package dependency",
                            ));
                        }
                        control.requires.push(name.to_owned());
                    }
                }
            }
            "comment" => {} // Descriptive only; never confers privileges.
            _ => return Err((INVALID, "unsupported control key")),
        }
    }
    if control.relocatable && !control.schema.is_empty() {
        return Err((INVALID, "relocatable package cannot fix its schema"));
    }
    if control.native_install
        && (control.native_module.is_empty() || control.default_version.is_empty())
    {
        return Err((
            INVALID,
            "native installation requires native_module and default_version",
        ));
    }
    Ok(control)
}

fn io_error(error: std::io::Error) -> Error {
    if error.kind() == std::io::ErrorKind::NotFound {
        (NOT_FOUND, "package control or SQL file not found")
    } else {
        (IO_ERROR, "cannot read package file")
    }
}

fn read_text(root: &Path, path: &Path, limit: usize) -> Result<String, Error> {
    let canonical = path.canonicalize().map_err(io_error)?;
    if !canonical.starts_with(root) {
        return Err((INVALID, "package path escapes trusted root"));
    }
    let metadata = canonical.metadata().map_err(io_error)?;
    if !metadata.is_file() || metadata.len() > limit as u64 {
        return Err((INVALID, "package source must be a bounded regular file"));
    }
    let file = File::open(canonical).map_err(io_error)?;
    let mut bytes = Vec::new();
    file.take((limit + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(io_error)?;
    if bytes.len() > limit || bytes.contains(&0) {
        return Err((INVALID, "invalid package source size or NUL"));
    }
    String::from_utf8(bytes).map_err(|_| (INVALID, "package source is not UTF-8"))
}

fn version_control(
    root: &Path,
    directory: &Path,
    name: &str,
    version: &str,
    primary: &Control,
) -> Result<Control, Error> {
    let path = directory.join(format!("{name}--{version}.control"));
    let control = match path.symlink_metadata() {
        Ok(_) => parse_control_overlay(
            &read_text(root, &path, CONTROL_LIMIT)?,
            primary.clone(),
            true,
        ),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(primary.clone()),
        Err(error) => Err(io_error(error)),
    }?;
    if control.requires.iter().any(|dependency| dependency == name) {
        return Err((INVALID, "package cannot require itself"));
    }
    Ok(control)
}

fn read_package(
    root: &str,
    name: &str,
    from: Option<&str>,
    requested: &str,
) -> Result<Package, Error> {
    if root.is_empty()
        || !component(name)
        || (!requested.is_empty() && !component(requested))
        || from.is_some_and(|from| !component(from))
    {
        return Err((
            INVALID,
            "invalid package root, name or source/target version",
        ));
    }
    let root = Path::new(root).canonicalize().map_err(io_error)?;
    if !root.is_dir() {
        return Err((INVALID, "package root is not a directory"));
    }
    let directory = root.join(name);
    read_package_directory(root, directory, name, from, requested)
}

// Shared by server discovery and CLI validation of a flat staging directory.
// Callers validate the name/versions and pass a canonical trusted root.
fn read_package_directory(
    root: PathBuf,
    directory: PathBuf,
    name: &str,
    from: Option<&str>,
    requested: &str,
) -> Result<Package, Error> {
    let primary = parse_control(&read_text(
        &root,
        &directory.join(format!("{name}.control")),
        CONTROL_LIMIT,
    )?)?;
    let version = if requested.is_empty() {
        &primary.default_version
    } else {
        requested
    };
    if !component(version) {
        return Err((INVALID, "package needs an explicit or default version"));
    }
    // Enumerate names only; unreadable/invalid selected scripts must fail, not
    // silently cause selection of a different migration path.
    let directory = directory.canonicalize().map_err(io_error)?;
    if !directory.starts_with(&root) {
        return Err((INVALID, "package directory escapes trusted root"));
    }
    let control = version_control(&root, &directory, name, version, &primary)?;
    let mut versions = versions::Versions::default();
    for (index, entry) in directory.read_dir().map_err(io_error)?.enumerate() {
        if index >= DIRECTORY_LIMIT {
            return Err((INVALID, "too many entries in package directory"));
        }
        let entry = entry.map_err(io_error)?;
        if let Some(filename) = entry.file_name().to_str() {
            versions.add(name, filename)?;
        }
    }
    // Native installation is a declared source, never a fallback for missing or
    // malformed SQL. This control advertises one fresh-install version; update
    // paths remain explicit SQL and never re-run the native callback.
    if from.is_none() && control.native_install {
        if version != control.default_version {
            return Err((
                NOT_FOUND,
                "native installation supports only its declared default_version",
            ));
        }
        return Ok(Package {
            name: name.to_owned(),
            from: String::new(),
            version: version.to_owned(),
            control,
            scripts: Vec::new(),
            prerequisites: Vec::new(),
        });
    }
    // Same-version updates do not require an old base or even an update file.
    // This is only a source plan: the executor must still fence the installed
    // identity/version and check authority before reporting a no-op.
    let path = if from == Some(version) {
        vec![version.to_owned()]
    } else {
        versions.path(from, version)?
    };
    let mut scripts = Vec::new();
    let mut all_dependencies: std::collections::BTreeSet<String> =
        control.requires.iter().cloned().collect();
    let mut total = 0;
    for (index, to) in path.iter().enumerate().skip(usize::from(from.is_some())) {
        // Each override starts from primary, never the previous version's
        // override. Namespace relocation and native swaps are not implemented
        // by the current single-context schema transaction.
        let step = version_control(&root, &directory, name, to, &primary)?;
        if step.native_module != control.native_module
            || step.schema != control.schema
            || step.relocatable != control.relocatable
        {
            return Err((
                INVALID,
                "selected script path changes native module or namespace context",
            ));
        }
        all_dependencies.extend(step.requires);
        if all_dependencies.len() > 64 {
            return Err((
                INVALID,
                "selected script path exceeds 64 distinct dependencies",
            ));
        }
        let from = index
            .checked_sub(1)
            .map(|index| path[index].as_str())
            .unwrap_or("");
        let filename = if from.is_empty() {
            if step.native_install {
                return Err((
                    INVALID,
                    "native base cannot be executed as a SQL installation seed",
                ));
            }
            format!("{name}--{to}.sql")
        } else {
            format!("{name}--{from}--{to}.sql")
        };
        let sql = read_text(&root, &directory.join(filename), SQL_LIMIT - total)?;
        if from.is_empty() && sql.trim().is_empty() {
            return Err((INVALID, "empty base installation SQL"));
        }
        total += sql.len();
        scripts.push(Script {
            from: from.to_owned(),
            to: to.clone(),
            sql,
        });
    }
    let prerequisites = all_dependencies
        .into_iter()
        .filter(|name| !control.requires.contains(name))
        .collect();
    Ok(Package {
        name: name.to_owned(),
        from: from.unwrap_or("").to_owned(),
        version: version.to_owned(),
        control,
        scripts,
        prerequisites,
    })
}

/// Inspect a generated package's default installation using the server reader.
/// Returns true for a native source. Validates control, selected version path
/// and source bounds, not SQL syntax, object permissions or native availability.
/// Directory is trusted, immutable during inspection; no library is loaded.
pub fn inspect_install_source(directory: &Path, name: &str) -> Result<bool, String> {
    if !component(name) {
        return Err("invalid package name".into());
    }
    let root = directory.canonicalize().map_err(|e| e.to_string())?;
    let package = read_package_directory(root.clone(), root.clone(), name, None, "")
        .map_err(|(_, diagnostic)| diagnostic.to_owned())?;
    // Generated packages must not hide malformed metadata on an unselected
    // version. Inspection checks every secondary control, without executing SQL
    // or requiring every advertised version to have a direct installation seed.
    let primary = read_text(&root, &root.join(format!("{name}.control")), CONTROL_LIMIT)
        .and_then(|text| parse_control(&text))
        .map_err(|(_, diagnostic)| diagnostic.to_owned())?;
    for entry in root.read_dir().map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        let filename = entry.file_name();
        let Some(version) = filename
            .to_str()
            .and_then(|s| s.strip_prefix(&format!("{name}--")))
            .and_then(|s| s.strip_suffix(".control"))
        else {
            continue;
        };
        if !component(version) {
            return Err("invalid secondary control filename".into());
        }
        version_control(&root, &root, name, version, &primary)
            .map_err(|(_, diagnostic)| diagnostic.to_owned())?;
    }
    Ok(package.control.native_install)
}

unsafe fn input<'a>(data: *const u8, length: u32, maximum: usize) -> Result<&'a str, Error> {
    if length as usize > maximum || (length != 0 && data.is_null()) {
        return Err((INVALID, "invalid package input span"));
    }
    let bytes = if length == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(data, length as usize) }
    };
    let text = str::from_utf8(bytes).map_err(|_| (INVALID, "package input is not UTF-8"))?;
    if text.contains('\0') {
        return Err((INVALID, "NUL in package input"));
    }
    Ok(text)
}

/// Read an owned snapshot, without SQL execution, DDL, or module initialization.
/// # Safety
/// Inputs are live readable spans. Output and diagnostic are writable, disjoint
/// from each other and the inputs. Root is administrator-controlled and not
/// concurrently modified. On success destroy the handle after all borrows end.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_read(
    root: *const u8,
    root_len: u32,
    name: *const u8,
    name_len: u32,
    version: *const u8,
    version_len: u32,
    output: *mut *mut Package,
    error: *mut c_char,
    capacity: u32,
) -> i32 {
    unsafe {
        finish_read(output, error, capacity, || {
            let root = input(root, root_len, 32768)?;
            let name = input(name, name_len, 255)?;
            let version = input(version, version_len, 255)?;
            read_package(root, name, None, version)
        })
    }
}

/// Read ONLY the selected update edges from the expected installed version.
/// Equal versions return a plan with zero scripts; empty update files are valid.
/// No catalog reads, privilege admission, object changes or version writes occur.
/// # Safety
/// Same pointer, ownership and trusted-root contract as `seekdb_runtime_package_read`.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_read_update(
    root: *const u8,
    root_len: u32,
    name: *const u8,
    name_len: u32,
    from: *const u8,
    from_len: u32,
    version: *const u8,
    version_len: u32,
    output: *mut *mut Package,
    error: *mut c_char,
    capacity: u32,
) -> i32 {
    unsafe {
        finish_read(output, error, capacity, || {
            let root = input(root, root_len, 32768)?;
            let name = input(name, name_len, 255)?;
            let from = input(from, from_len, 255)?;
            let version = input(version, version_len, 255)?;
            read_package(root, name, Some(from), version)
        })
    }
}

// Check outputs before reading any inputs; both entry points publish exactly one
// fully owned plan or no handle. Allocation/panic policy remains the host policy.
unsafe fn finish_read(
    output: *mut *mut Package,
    error: *mut c_char,
    capacity: u32,
    read: impl FnOnce() -> Result<Package, Error>,
) -> i32 {
    if !output.is_null() {
        unsafe { *output = ptr::null_mut() };
    }
    if output.is_null() || error.is_null() || capacity == 0 {
        return INVALID;
    }
    let diagnostic = unsafe { slice::from_raw_parts_mut(error.cast::<u8>(), capacity as usize) };
    diagnostic[0] = 0;
    match read() {
        Ok(package) => {
            let pointer = unsafe { alloc(Layout::new::<Package>()) }.cast::<Package>();
            if pointer.is_null() {
                return NO_MEMORY;
            }
            unsafe {
                pointer.write(package);
                *output = pointer;
            }
            OK
        }
        Err((status, message)) => {
            let length = message.len().min(diagnostic.len() - 1);
            diagnostic[..length].copy_from_slice(&message.as_bytes()[..length]);
            diagnostic[length] = 0;
            status
        }
    }
}

/// # Safety
/// Handle must be live and owned exclusively, with no remaining borrowed views.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_destroy(package: *mut Package) {
    if !package.is_null() {
        drop(unsafe { Box::from_raw(package) });
    }
}

/// # Safety
/// Handle is live; outputs are writable/disjoint. Views are borrowed until
/// destroy. Field 5 is legacy single-file SQL and fails for multi-file plans;
/// fields 7/8/9 select script SQL/from-version/to-version by index. Never join
/// SQL files: a trailing comment or incomplete token cannot cross file bounds.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_text(
    package: *const Package,
    field: u32,
    index: u32,
    data: *mut *const u8,
    length: *mut u32,
) -> i32 {
    if data.is_null() || length.is_null() {
        return INVALID;
    }
    unsafe {
        *data = ptr::null();
        *length = 0;
    }
    let Some(package) = (unsafe { package.as_ref() }) else {
        return INVALID;
    };
    if !matches!(field, 6..=9 | 11) && index != 0 {
        return INVALID;
    }
    let text = match field {
        1 => &package.name,
        2 => &package.version,
        3 => &package.control.native_module,
        4 => &package.control.schema,
        10 => &package.from,
        5 if package.scripts.len() == 1 => &package.scripts[0].sql,
        6 => match package.control.requires.get(index as usize) {
            Some(value) => value,
            None => return INVALID,
        },
        11 => match package.prerequisites.get(index as usize) {
            Some(value) => value,
            None => return INVALID,
        },
        7..=9 => match package.scripts.get(index as usize) {
            Some(script) => match field {
                7 => &script.sql,
                8 => &script.from,
                _ => &script.to,
            },
            None => return INVALID,
        },
        _ => return INVALID,
    };
    unsafe {
        *data = text.as_ptr();
        *length = text.len() as u32;
    }
    OK
}

/// # Safety
/// Handle is live; count is writable and disjoint from the handle. Failure
/// clears count. A same-version update or fresh native source owns zero scripts;
/// SQL installations and non-noop updates own at least one complete script.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_script_count(
    package: *const Package,
    count: *mut u32,
) -> i32 {
    if count.is_null() {
        return INVALID;
    }
    unsafe { *count = 0 };
    let Some(package) = (unsafe { package.as_ref() }) else {
        return INVALID;
    };
    unsafe { *count = package.scripts.len() as u32 };
    OK
}

/// # Safety
/// Handle is live; output is writable and disjoint. Failure clears output.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_prerequisite_count(
    package: *const Package,
    count: *mut u32,
) -> i32 {
    if count.is_null() {
        return INVALID;
    }
    unsafe { *count = 0 };
    let Some(package) = (unsafe { package.as_ref() }) else {
        return INVALID;
    };
    unsafe { *count = package.prerequisites.len() as u32 };
    OK
}

/// # Safety
/// Handle is live and outputs are writable/disjoint from it and each other.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_info(
    package: *const Package,
    count: *mut u32,
    relocatable: *mut u32,
) -> i32 {
    if count.is_null() || relocatable.is_null() {
        return INVALID;
    }
    unsafe {
        *count = 0;
        *relocatable = 0;
    }
    let Some(package) = (unsafe { package.as_ref() }) else {
        return INVALID;
    };
    unsafe {
        *count = package.control.requires.len() as u32;
        *relocatable = u32::from(package.control.relocatable);
    }
    OK
}

/// # Safety
/// Handle is live; output is writable and disjoint from it. Failure clears output.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_package_native_install(
    package: *const Package,
    output: *mut u32,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe {
        *output = 0;
    }
    let Some(package) = (unsafe { package.as_ref() }) else {
        return INVALID;
    };
    unsafe {
        *output = u32::from(package.control.native_install);
    }
    OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn control_strings_comments_and_dependencies() {
        let control = parse_control("default_version = '1.2' # latest\nrequires = 'text, math'\ncomment = 'don''t remove # this'\nrelocatable = true\n").unwrap();
        assert_eq!(control.default_version, "1.2");
        assert_eq!(control.requires, ["text", "math"]);
        assert!(control.relocatable && control.native_module.is_empty());
    }

    #[test]
    fn secondary_controls_replace_fields_and_inherit_only_primary() {
        let primary =
            parse_control("default_version = '2'\nrequires = 'base'\nschema = 'fixed'").unwrap();
        let middle =
            parse_control_overlay("requires = 'migration'", primary.clone(), true).unwrap();
        assert_eq!(middle.requires, ["migration"]);
        assert_eq!(middle.schema, "fixed");
        let target = parse_control_overlay("comment = 'target'", primary.clone(), true).unwrap();
        assert_eq!(target.requires, ["base"]);
        assert_eq!(target.default_version, "2");
        assert!(
            parse_control_overlay("requires = ''", primary.clone(), true)
                .unwrap()
                .requires
                .is_empty()
        );
        for invalid in [
            "default_version = '3'",
            "directory = 'sql'",
            "requires = 'a'\nrequires = 'b'",
            "relocatable = true",
            "requires = 'a,a'",
            "trusted = true",
        ] {
            assert!(
                parse_control_overlay(invalid, primary.clone(), true).is_err(),
                "{invalid}"
            );
        }
    }

    #[test]
    fn native_source_requires_explicit_module_and_version() {
        for text in [
            "install_source = 'native'",
            "install_source = 'other'",
            "install_source = true",
            "native_module = 'org.test'\ninstall_source = 'native'",
            "default_version = '1'\ninstall_source = 'native'",
        ] {
            assert!(parse_control(text).is_err(), "{text}");
        }
        let control = parse_control(
            "install_source = 'native'\ndefault_version = '1'\nnative_module = 'org.test'",
        )
        .unwrap();
        assert!(control.native_install);
        assert!(
            !parse_control("install_source = 'sql'")
                .unwrap()
                .native_install
        );
    }
    #[test]
    fn invalid_controls_fail_closed() {
        for text in [
            "default_version = '../x'",
            "trusted = true",
            "default_version = '1' garbage",
            "requires = 'a, a'",
            "requires = 'a,'",
            "relocatable = 'false'",
            "schema = 'x'\nrelocatable = true",
            "schema = 'x'\nschema = 'y'",
            "default_version = 'unclosed",
            "schema = 'x\0y'",
            "default_version = 1",
        ] {
            assert!(parse_control(text).is_err(), "{text}");
        }
        assert!(parse_control(&" ".repeat(CONTROL_LIMIT + 1)).is_err());
    }

    #[test]
    fn filename_components_cannot_change_layout() {
        for name in ["", "..", "../x", "/tmp/x", "x/y", "x\\y", "x--1", ".hidden"] {
            assert!(!component(name), "{name}");
        }
        for name in ["text_ops", "org.seekdb.ai", "1.0-beta"] {
            assert!(component(name));
        }
    }

    #[test]
    fn native_module_is_a_catalog_identity_not_a_filename() {
        for name in ["seekdb.gis", "custom--module", "_internal"] {
            let control = parse_control(&format!("native_module = '{name}'")).unwrap();
            assert_eq!(control.native_module, name);
        }
        for name in ["", "GIS", "lib/gis", "lib\\gis", "géométrie"] {
            assert!(parse_control(&format!("native_module = '{name}'")).is_err());
        }
    }

    #[test]
    fn borrowed_views_are_bounded_and_invalid_access_clears_outputs() {
        let package = Package {
            name: "text".to_owned(),
            from: String::new(),
            version: "1".to_owned(),
            control: parse_control("requires = 'a, b'\nrelocatable = true").unwrap(),
            prerequisites: vec!["temporary".to_owned()],
            scripts: vec![Script {
                from: String::new(),
                to: "1".to_owned(),
                sql: "SELECT 1;".to_owned(),
            }],
        };
        let mut count = 99;
        let mut relocatable = 99;
        assert_eq!(
            unsafe { seekdb_runtime_package_info(&package, &mut count, &mut relocatable) },
            OK
        );
        assert_eq!((count, relocatable), (2, 1));
        assert_eq!(
            unsafe { seekdb_runtime_package_prerequisite_count(&package, &mut count) },
            OK
        );
        assert_eq!(count, 1);
        let mut data = ptr::null();
        let mut length = 99;
        assert_eq!(
            unsafe { seekdb_runtime_package_text(&package, 6, 1, &mut data, &mut length) },
            OK
        );
        assert_eq!(
            unsafe { slice::from_raw_parts(data, length as usize) },
            b"b"
        );
        assert_eq!(
            unsafe { seekdb_runtime_package_text(&package, 11, 0, &mut data, &mut length) },
            OK
        );
        assert_eq!(
            unsafe { slice::from_raw_parts(data, length as usize) },
            b"temporary"
        );
        assert_eq!(
            unsafe { seekdb_runtime_package_prerequisite_count(ptr::null(), &mut count) },
            INVALID
        );
        assert_eq!(count, 0);
        assert_eq!(
            unsafe { seekdb_runtime_package_prerequisite_count(&package, ptr::null_mut()) },
            INVALID
        );
        for (field, index) in [
            (0, 0),
            (11, 1),
            (12, 0),
            (10, 1),
            (1, 1),
            (6, 2),
            (6, u32::MAX),
            (7, 1),
            (8, u32::MAX),
        ] {
            data = ptr::dangling();
            length = 99;
            assert_eq!(
                unsafe {
                    seekdb_runtime_package_text(&package, field, index, &mut data, &mut length)
                },
                INVALID
            );
            assert!(data.is_null());
            assert_eq!(length, 0);
        }
        assert_eq!(
            unsafe { seekdb_runtime_package_info(ptr::null(), &mut count, &mut relocatable) },
            INVALID
        );
        assert_eq!((count, relocatable), (0, 0));
    }

    #[test]
    fn ffi_invalid_outputs_are_cleared() {
        let mut output = ptr::dangling_mut();
        let mut error = [0 as c_char; 8];
        assert_eq!(
            unsafe {
                seekdb_runtime_package_read(
                    ptr::null(),
                    1,
                    ptr::null(),
                    0,
                    ptr::null(),
                    0,
                    &mut output,
                    error.as_mut_ptr(),
                    error.len() as u32,
                )
            },
            INVALID
        );
        assert!(output.is_null());
        assert_eq!(error[7], 0);
    }

    #[test]
    fn update_ffi_rejects_invalid_source_before_filesystem_access_and_clears_output() {
        for source in [
            b"".as_slice(),
            b"../version",
            b"v\0x",
            b"\xff",
            &[b'x'; 256],
        ] {
            let mut output = ptr::dangling_mut();
            let mut error = [1 as c_char; 8];
            assert_eq!(
                unsafe {
                    seekdb_runtime_package_read_update(
                        b"/unused".as_ptr(),
                        7,
                        b"demo".as_ptr(),
                        4,
                        source.as_ptr(),
                        source.len() as u32,
                        b"tip".as_ptr(),
                        3,
                        &mut output,
                        error.as_mut_ptr(),
                        error.len() as u32,
                    )
                },
                INVALID
            );
            assert!(output.is_null());
            assert!(error.contains(&0));
        }
        let mut output = ptr::dangling_mut();
        let mut error = [0 as c_char; 1];
        assert_eq!(
            unsafe {
                seekdb_runtime_package_read_update(
                    ptr::null(),
                    0,
                    ptr::null(),
                    0,
                    ptr::null(),
                    1,
                    ptr::null(),
                    0,
                    &mut output,
                    error.as_mut_ptr(),
                    1,
                )
            },
            INVALID
        );
        assert!(output.is_null());
        assert_eq!(error, [0]);
        assert_eq!(
            unsafe {
                seekdb_runtime_package_read_update(
                    ptr::null(),
                    1,
                    ptr::null(),
                    1,
                    ptr::null(),
                    1,
                    ptr::null(),
                    1,
                    &mut output,
                    ptr::null_mut(),
                    0,
                )
            },
            INVALID
        );
        assert!(output.is_null());
    }

    #[test]
    fn same_version_plan_exposes_source_identity_without_inventing_sql() {
        let package = Package {
            name: "demo".into(),
            from: "v1".into(),
            version: "v1".into(),
            control: Control::default(),
            prerequisites: Vec::new(),
            scripts: Vec::new(),
        };
        let mut count = 99;
        assert_eq!(
            unsafe { seekdb_runtime_package_script_count(&package, &mut count) },
            OK
        );
        assert_eq!(count, 0);
        let mut data = ptr::null();
        let mut length = 0;
        assert_eq!(
            unsafe { seekdb_runtime_package_text(&package, 10, 0, &mut data, &mut length) },
            OK
        );
        assert_eq!(
            unsafe { slice::from_raw_parts(data, length as usize) },
            b"v1"
        );
        for (field, index) in [(5, 0), (7, 0), (8, 0), (9, 0), (10, 1)] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_package_text(&package, field, index, &mut data, &mut length)
                },
                INVALID
            );
            assert!(data.is_null() && length == 0);
        }
    }

    #[test]
    fn multi_script_ffi_preserves_boundaries_and_never_returns_partial_legacy_sql() {
        let package = Package {
            name: "demo".into(),
            from: String::new(),
            version: "2".into(),
            control: Control::default(),
            prerequisites: Vec::new(),
            scripts: vec![
                Script {
                    from: "".into(),
                    to: "1".into(),
                    sql: "SELECT 1; -- tail".into(),
                },
                Script {
                    from: "1".into(),
                    to: "2".into(),
                    sql: "SELECT 2;".into(),
                },
            ],
        };
        let mut count = 99;
        assert_eq!(
            unsafe { seekdb_runtime_package_script_count(&package, &mut count) },
            OK
        );
        assert_eq!(count, 2);
        let mut data = ptr::dangling();
        let mut length = 99;
        assert_eq!(
            unsafe { seekdb_runtime_package_text(&package, 5, 0, &mut data, &mut length) },
            INVALID
        );
        assert!(data.is_null() && length == 0);
        for (field, index, expected) in [
            (7, 0, "SELECT 1; -- tail"),
            (7, 1, "SELECT 2;"),
            (8, 0, ""),
            (8, 1, "1"),
            (9, 0, "1"),
            (9, 1, "2"),
        ] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_package_text(&package, field, index, &mut data, &mut length)
                },
                OK
            );
            assert_eq!(
                unsafe { slice::from_raw_parts(data, length as usize) },
                expected.as_bytes()
            );
        }
        assert_eq!(
            unsafe { seekdb_runtime_package_script_count(ptr::null(), &mut count) },
            INVALID
        );
        assert_eq!(count, 0);
        assert_eq!(
            unsafe { seekdb_runtime_package_script_count(&package, ptr::null_mut()) },
            INVALID
        );
    }
}
