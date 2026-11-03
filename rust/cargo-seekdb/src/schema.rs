// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use std::{
    ffi::OsString,
    fs,
    io::Read,
    path::{Path, PathBuf},
    process::Command,
};

const INCOMPLETE: &str = ".seekdb-schema-incomplete";
#[derive(Debug, PartialEq)]
pub(crate) struct Spec {
    manifest: PathBuf,
    output: PathBuf,
    example: String,
}

pub(crate) fn parse(args: &[OsString]) -> Result<Option<Spec>, String> {
    if args == [OsString::from("--help")] || args == [OsString::from("-h")] {
        return Ok(None);
    }
    let (mut manifest, mut output, mut example) = (None, None, None);
    let mut options = args.iter();
    while let Some(option) = options.next() {
        let value = options
            .next()
            .ok_or_else(|| format!("missing value for {}", option.to_string_lossy()))?;
        match option.to_str() {
            Some("--manifest-path") if manifest.is_none() => manifest = Some(PathBuf::from(value)),
            Some("--output") if output.is_none() => output = Some(PathBuf::from(value)),
            Some("--example") if example.is_none() => {
                let name = value.to_str().ok_or("example must be ASCII")?;
                if !component(name) || name.contains('.') {
                    return Err(
                        "example must be an ASCII Cargo target name without dots or slashes".into(),
                    );
                }
                example = Some(name.to_owned());
            }
            _ => {
                return Err(format!(
                    "unknown or repeated option {}",
                    option.to_string_lossy()
                ))
            }
        }
    }
    Ok(Some(Spec {
        manifest: manifest.ok_or("--manifest-path is required")?,
        output: output.ok_or("--output is required")?,
        example: example.unwrap_or_else(|| "seekdb_schema".to_owned()),
    }))
}

fn component(text: &str) -> bool {
    !text.is_empty()
        && text.len() <= 255
        && text.as_bytes()[0].is_ascii_alphanumeric()
        && text
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || b"._-".contains(&c))
        && !text.contains("..")
        && !text.contains("--")
}

// First bound all output files, then use the actual runtime package reader for
// control and default-version selection. SQL admission remains server-owned.
fn validate_output(output: &Path) -> Result<(), String> {
    let mut files = Vec::new();
    let mut package = None;
    let mut total_sql = 0;
    for entry in fs::read_dir(output).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        let name = entry
            .file_name()
            .into_string()
            .map_err(|_| "non-UTF-8 schema filename")?;
        if name == INCOMPLETE {
            continue;
        }
        if files.len() >= 4096 || !entry.file_type().map_err(|e| e.to_string())?.is_file() {
            return Err("schema output must contain only bounded regular control/SQL files".into());
        }
        let limit = if let Some(name) = name.strip_suffix(".control") {
            if let Some((owner, version)) = name.split_once("--") {
                if !component(owner) || !component(version) {
                    return Err("invalid secondary control filename".into());
                }
            } else if !component(name) || package.replace(name.to_owned()).is_some() {
                return Err(
                    "schema output must have exactly one valid package control file".into(),
                );
            }
            64 * 1024
        } else if name.ends_with(".sql") {
            4 * 1024 * 1024
        } else {
            return Err("unexpected schema output file".into());
        };
        let mut text = String::new();
        fs::File::open(entry.path())
            .map_err(|e| e.to_string())?
            .take(limit as u64 + 1)
            .read_to_string(&mut text)
            .map_err(|e| format!("cannot read UTF-8 schema file: {e}"))?;
        if text.len() > limit || text.contains('\0') {
            return Err("schema output exceeds file limit or contains NUL".into());
        }
        if name.ends_with(".sql") {
            total_sql += text.len();
            if total_sql > 4 * 1024 * 1024 {
                return Err("schema exceeds 4 MiB total SQL".into());
            }
        }
        files.push((name, text));
    }
    let package = package.ok_or("schema generator did not emit a control file")?;
    let mut bases = 0;
    for (name, text) in &files {
        if name.ends_with(".control") {
            if name == &format!("{package}.control") && text.trim().is_empty() {
                return Err("empty generated control".into());
            }
            if name != &format!("{package}.control") && !name.starts_with(&format!("{package}--")) {
                return Err("secondary control belongs to another package".into());
            }
            continue;
        }
        let suffix = name
            .strip_prefix(&format!("{package}--"))
            .and_then(|s| s.strip_suffix(".sql"))
            .ok_or("SQL file belongs to another package")?;
        let versions: Vec<_> = suffix.split("--").collect();
        if !(1..=2).contains(&versions.len())
            || !versions.iter().all(|v| component(v))
            || (versions.len() == 2 && versions[0] == versions[1])
        {
            return Err("invalid generated SQL version filename".into());
        }
        if versions.len() == 1 {
            if text.trim().is_empty() {
                return Err("empty generated base SQL".into());
            }
            bases += 1;
        }
    }
    let native = seekdb_plugin_runtime::package::inspect_install_source(output, &package)?;
    if (bases == 0) != native {
        return Err("SQL source requires base SQL; native source must not contain base SQL".into());
    }
    Ok(())
}

pub(crate) fn generate(spec: Spec) -> Result<PathBuf, String> {
    let manifest = spec
        .manifest
        .canonicalize()
        .map_err(|e| format!("invalid Cargo manifest: {e}"))?;
    if !manifest.is_file() || manifest.file_name().is_none_or(|n| n != "Cargo.toml") {
        return Err("--manifest-path must name a Cargo.toml file".into());
    }
    let output = super::reserve_directory(
        &spec.output,
        INCOMPLETE,
        b"Schema generation incomplete. Do not install this package.\n",
    )?;
    let work = || -> Result<(), String> {
        // Runs trusted developer code, not a loaded plugin's init or lifecycle.
        // The example receives one absolute staging-directory argument.
        super::run(
            Command::new("cargo")
                .current_dir(manifest.parent().ok_or("missing manifest parent")?)
                .args(["run", "--offline", "--manifest-path"])
                .arg(&manifest)
                .args(["--example", &spec.example, "--"])
                .arg(&output),
        )?;
        validate_output(&output)?;
        fs::remove_file(output.join(INCOMPLETE))
            .map_err(|e| format!("cannot finish schema marker: {e}"))?;
        Ok(())
    };
    work().map_err(|e| format!("{e}\nIncomplete schema retained at {}", output.display()))?;
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn secondary_controls_are_validated_even_off_the_default_path() {
        let root =
            std::env::temp_dir().join(format!("seekdb-secondary-schema-{}", std::process::id()));
        fs::create_dir(&root).unwrap();
        fs::write(
            root.join("p.control"),
            "default_version = '1'\nrequires = 'base'",
        )
        .unwrap();
        fs::write(root.join("p--1.sql"), "SELECT 1;").unwrap();
        for (index, (name, text, valid)) in [
            ("p--1.control", "requires = 'other'", true),
            ("p--2.control", "requires = ''", true),
            ("p--2.control", "", true),
            ("p--2.control", "default_version = '2'", false),
            ("p--2.control", "directory = 'sql'", false),
            ("p--2.control", "requires = 'p'", false),
            ("p--2.control", "trusted = true", false),
            ("q--2.control", "requires = 'base'", false),
            ("p--2--3.control", "", false),
        ]
        .iter()
        .enumerate()
        {
            let path = root.join(name);
            fs::write(&path, text).unwrap();
            assert_eq!(validate_output(&root).is_ok(), *valid, "case {index}");
            fs::remove_file(path).unwrap();
        }
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn native_layout_and_control_use_the_server_source_reader() {
        let root =
            std::env::temp_dir().join(format!("seekdb-native-schema-cli-{}", std::process::id()));
        fs::create_dir(&root).unwrap();
        let native =
            "default_version = '1'\nnative_module = 'org.native'\ninstall_source = 'native'\n";
        let control = root.join("p.control");
        fs::write(&control, native).unwrap();
        assert!(validate_output(&root).is_ok());
        fs::write(root.join("p--1--2.sql"), "").unwrap();
        assert!(validate_output(&root).is_ok());
        fs::write(root.join("p--1.sql"), "SELECT 1;").unwrap();
        assert!(validate_output(&root).is_err());
        fs::remove_file(root.join("p--1.sql")).unwrap();
        for invalid in [
            "default_version = '1'",
            "install_source = 'native'\nnative_module = 'org.native'",
            "default_version = '1'\ninstall_source = 'native'",
            "install_source = 'unknown'",
            "default_version = '1'\ninstall_source = 'native'\nnative_module = '../escape'",
            "# install_source = 'native'\ndefault_version = '1'",
        ] {
            fs::write(&control, invalid).unwrap();
            assert!(validate_output(&root).is_err(), "{invalid}");
        }
        for suffix in [
            "install_source = 'sql'",
            "trusted = true",
            "requires = 'p'",
            "schema = 'fixed'\nrelocatable = true",
        ] {
            fs::write(&control, format!("{native}{suffix}\n")).unwrap();
            assert!(validate_output(&root).is_err(), "{suffix}");
        }
        fs::write(&control, "default_version = 'unreachable'\n").unwrap();
        fs::write(root.join("p--1.sql"), "SELECT 1;").unwrap();
        assert!(validate_output(&root).is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn layout_validation_rejects_incomplete_malformed_and_oversized_outputs() {
        let root = std::env::temp_dir().join(format!("seekdb-schema-test-{}", std::process::id()));
        fs::create_dir(&root).unwrap();
        let valid = [
            ("p.control", b"default_version = '1'\n".as_slice()),
            ("p--1.sql", b"SELECT 1;".as_slice()),
        ];
        let cases = [
            (vec![], false),
            (vec![("p.control", b"".as_slice()), valid[1]], false),
            (vec![valid[0], ("p--1.sql", b" ".as_slice())], false),
            (
                vec![valid[0], ("other--1.sql", b"SELECT 1;".as_slice())],
                false,
            ),
            (
                vec![valid[0], ("p--1--1.sql", b"SELECT 1;".as_slice())],
                false,
            ),
            (vec![valid[0], ("p--1.sql", b"\xff".as_slice())], false),
            (
                vec![valid[0], ("p--1.sql", b"SELECT '\0';".as_slice())],
                false,
            ),
            (
                vec![
                    valid[0],
                    valid[1],
                    ("q.control", b"default_version = '1'".as_slice()),
                ],
                false,
            ),
            (
                vec![valid[0], valid[1], ("p--1--2.sql", b"".as_slice())],
                true,
            ),
            (
                vec![valid[0], valid[1], ("plugin.so", b"not schema".as_slice())],
                false,
            ),
        ];
        for (i, (files, success)) in cases.iter().enumerate() {
            let directory = root.join(i.to_string());
            fs::create_dir(&directory).unwrap();
            for (name, text) in files {
                fs::write(directory.join(name), text).unwrap();
            }
            assert_eq!(validate_output(&directory).is_ok(), *success, "case {i}");
        }
        let large = root.join("large");
        fs::create_dir(&large).unwrap();
        fs::write(large.join("p.control"), valid[0].1).unwrap();
        fs::write(large.join("p--1.sql"), vec![b'x'; 4 * 1024 * 1024 + 1]).unwrap();
        assert!(validate_output(&large).is_err());
        fs::write(large.join("p--1.sql"), vec![b'x'; 3 * 1024 * 1024]).unwrap();
        fs::write(large.join("p--1--2.sql"), vec![b'x'; 2 * 1024 * 1024]).unwrap();
        assert!(validate_output(&large).is_err());
        #[cfg(unix)]
        {
            let links = root.join("links");
            fs::create_dir(&links).unwrap();
            fs::write(links.join("p.control"), valid[0].1).unwrap();
            std::os::unix::fs::symlink(root.join("8/p--1.sql"), links.join("p--1.sql")).unwrap();
            assert!(validate_output(&links).is_err());
        }
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn options_and_file_components_are_explicit() {
        let args = |v: &[&str]| v.iter().map(OsString::from).collect::<Vec<_>>();
        assert!(parse(&args(&["--help"])).unwrap().is_none());
        assert!(parse(&args(&[
            "--manifest-path",
            "a/Cargo.toml",
            "--output",
            "new dir"
        ]))
        .unwrap()
        .is_some());
        for options in [
            vec![],
            vec!["--output", "a"],
            vec!["--manifest-path"],
            vec!["--output", "a", "--output", "b"],
            vec!["--bad", "a"],
        ] {
            assert!(parse(&args(&options)).is_err());
        }
        for invalid in ["", "../x", "a--b", "a/b", "'x'"] {
            assert!(!component(invalid));
        }
        assert!(component("text_ops-1.0"));
        let base = args(&["--manifest-path", "p/Cargo.toml", "--output", "new"]);
        assert_eq!(parse(&base).unwrap().unwrap().example, "seekdb_schema");
        let mut selected = base.clone();
        selected.extend(args(&["--example", "native_schema"]));
        assert_eq!(parse(&selected).unwrap().unwrap().example, "native_schema");
        for name in ["", "../escape", "-flag", "name.rs", "x/y"] {
            let mut invalid = base.clone();
            invalid.extend(args(&["--example", name]));
            assert!(parse(&invalid).is_err());
        }
        selected.extend(args(&["--example", "another"]));
        assert!(parse(&selected).is_err());
    }
}
