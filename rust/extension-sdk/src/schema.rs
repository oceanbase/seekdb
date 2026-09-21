// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Build-time SQL package generation, not a runtime catalog or SQL parser.
//! Packages can compose installed SQL routines or registered native functions. Native symbol
//! DDL, custom type DDL and arbitrary transactional DDL require host support.
use crate::{sys, FunctionDefinition};
use std::collections::BTreeSet;
use std::fs::OpenOptions;
use std::io::Write;
use std::path::Path;

type Result<T> = std::result::Result<T, String>;

/// Explicit SQL mapping: bytes do not inherently imply a text character set.
#[derive(Clone, Copy)]
pub enum SqlType {
    BigInt,
    Text,
    LongBlob,
}
impl SqlType {
    fn sql(self) -> &'static str {
        match self {
            Self::BigInt => "BIGINT",
            Self::Text => "TEXT",
            Self::LongBlob => "LONGBLOB",
        }
    }
    fn native(self) -> &'static std::ffi::CStr {
        match self {
            Self::BigInt => c"core.type.int64",
            Self::Text | Self::LongBlob => c"core.type.bytes",
        }
    }
}

/// Author-supplied effect declaration; native metadata cannot infer SQL access.
#[derive(Clone, Copy)]
pub enum SqlAccess {
    NoSql,
    ContainsSql,
    ReadsSqlData,
    ModifiesSqlData,
}
impl SqlAccess {
    fn sql(self) -> &'static str {
        match self {
            Self::NoSql => "NO SQL",
            Self::ContainsSql => "CONTAINS SQL",
            Self::ReadsSqlData => "READS SQL DATA",
            Self::ModifiesSqlData => "MODIFIES SQL DATA",
        }
    }
}

fn identifier(name: &str) -> Result<String> {
    if name.is_empty()
        || name.len() > 64
        || !(name.as_bytes()[0].is_ascii_alphabetic() || name.starts_with('_'))
        || !name.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_')
    {
        return Err("generated SQL names must be 1..64 ASCII letters/digits/underscores, starting with a letter or underscore".into());
    }
    Ok(format!("`{name}`"))
}

/// Generate an INVOKER routine from the same definition used for registration.
/// Names must differ to prevent recursion/shadowing. Explicit SQL types must
/// match the native signature; unsupported custom types are never erased to
/// bytes. Native function admission and installation still belong to the host.
pub fn scalar_wrapper(
    name: &str,
    definition: &FunctionDefinition<'_>,
    arguments: &[(&str, SqlType)],
    result: SqlType,
    access: SqlAccess,
) -> Result<String> {
    let native = definition
        .sql_name
        .to_str()
        .map_err(|_| "native SQL name is not UTF-8")?;
    let wrapper = identifier(name)?;
    let native_quoted = identifier(native)?;
    if name.eq_ignore_ascii_case(native) {
        return Err("SQL wrapper must not shadow its native function".into());
    }
    if arguments.len() != definition.argument_types.len() || arguments.len() > 1024 {
        return Err("SQL wrapper arity does not match native declaration".into());
    }
    if result.native() != definition.result_type {
        return Err("unsupported or mismatched SQL result type".into());
    }
    let mut names = BTreeSet::new();
    let mut parameters = Vec::new();
    let mut values = Vec::new();
    for ((name, sql_type), native_type) in arguments.iter().zip(definition.argument_types) {
        let quoted = identifier(name)?;
        if !names.insert(name.to_ascii_lowercase()) {
            return Err("duplicate SQL parameter name".into());
        }
        if sql_type.native() != *native_type {
            return Err("unsupported or mismatched SQL argument type".into());
        }
        parameters.push(format!("{quoted} {}", sql_type.sql()));
        values.push(quoted);
    }
    let deterministic = if definition.flags & sys::DETERMINISTIC != 0 {
        "DETERMINISTIC"
    } else {
        "NOT DETERMINISTIC"
    };
    Ok(format!(
        "CREATE FUNCTION {wrapper}({})\nRETURNS {}\n{deterministic}\n{}\nSQL SECURITY INVOKER\nRETURN {native_quoted}({});\n",
        parameters.join(", "), result.sql(), access.sql(), values.join(", ")
    ))
}

/// One complete file. Handwritten SQL is preserved, not split on semicolons or
/// reordered. `None` denotes a base install; `Some` denotes an explicit update.
pub struct Script<'a> {
    pub from: Option<&'a str>,
    pub to: &'a str,
    pub sql: &'a str,
}

pub struct Package<'a> {
    pub name: &'a str,
    pub default_version: &'a str,
    pub native_module: Option<&'a str>,
    pub scripts: &'a [Script<'a>],
}

/// Fresh-install source only; version updates always use explicit SQL scripts.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum InstallSource {
    Sql,
    Native,
}

/// Optional metadata for one version, independently overlaid on the primary
/// control by the host. `None` inherits; `Some(&[])` explicitly clears requires.
/// Source selection remains a package option. Native/namespace changes do not
/// imply that the host supports migrating between those contexts.
#[derive(Clone, Copy)]
pub struct VersionControl<'a> {
    pub version: &'a str,
    pub requires: Option<&'a [&'a str]>,
    pub native_module: Option<&'a str>,
    pub schema: Option<&'a str>,
    pub relocatable: Option<bool>,
}

impl<'a> VersionControl<'a> {
    pub const fn new(version: &'a str) -> Self {
        Self {
            version,
            requires: None,
            native_module: None,
            schema: None,
            relocatable: None,
        }
    }
}

/// Build-time declarations only. The host resolves and locks installed
/// providers; generating `requires` neither installs them nor grants privileges.
#[derive(Clone, Copy)]
pub struct PackageOptions<'a> {
    pub install_source: InstallSource,
    pub requires: &'a [&'a str],
    pub version_controls: &'a [VersionControl<'a>],
}

impl Default for PackageOptions<'_> {
    fn default() -> Self {
        Self {
            install_source: InstallSource::Sql,
            requires: &[],
            version_controls: &[],
        }
    }
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

fn module_identity(module: &str) -> Result<()> {
    if module.is_empty()
        || module.len() > 255
        || !module
            .bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || b"._-".contains(&c))
    {
        return Err("invalid native module ID".into());
    }
    Ok(())
}

fn requirement_names(package: &str, requires: &[&str]) -> Result<()> {
    if requires.len() > 64 {
        return Err("packages allow at most 64 Extension dependencies".into());
    }
    let mut names = BTreeSet::new();
    for name in requires {
        if !component(name) || *name == package || !names.insert(*name) {
            return Err("invalid, duplicate or self Extension dependency".into());
        }
    }
    Ok(())
}

impl Package<'_> {
    /// Deterministic file order and content, with host package-source limits.
    /// No automatic upgrade diff: changing an implementation does not reveal
    /// how persistent SQL objects/data should migrate. Authors provide edges.
    pub fn render(&self) -> Result<Vec<(String, String)>> {
        self.render_with_source(InstallSource::Sql)
    }

    /// Native source requires a module and permits zero scripts or update-only
    /// scripts. It never executes the installation callback during generation.
    pub fn render_with_source(&self, source: InstallSource) -> Result<Vec<(String, String)>> {
        self.render_with_options(PackageOptions {
            install_source: source,
            ..PackageOptions::default()
        })
    }

    /// Render SQL or native packages with explicit same-database dependencies.
    /// Declaration order is preserved; invalid/duplicate/self dependencies fail
    /// before any output is written. SQL semantics still belong to the kernel.
    pub fn render_with_options(
        &self,
        options: PackageOptions<'_>,
    ) -> Result<Vec<(String, String)>> {
        let source = options.install_source;
        if !component(self.name) || !component(self.default_version) {
            return Err("invalid package name or default version".into());
        }
        if (source == InstallSource::Sql && self.scripts.is_empty())
            || self.scripts.len() > 4095
            || options.version_controls.len() > 4095 - self.scripts.len()
        {
            return Err(
                "SQL source requires a base; packages allow at most 4096 control/SQL files".into(),
            );
        }
        if source == InstallSource::Native && self.native_module.is_none() {
            return Err("native installation requires a native module".into());
        }
        let mut control = format!("default_version = '{}'\n", self.default_version);
        if let Some(module) = self.native_module {
            module_identity(module)?;
            control.push_str(&format!("native_module = '{module}'\n"));
        }
        if source == InstallSource::Native {
            control.push_str("install_source = 'native'\n");
        }
        requirement_names(self.name, options.requires)?;
        if !options.requires.is_empty() {
            control.push_str(&format!("requires = '{}'\n", options.requires.join(", ")));
        }
        let mut files = vec![(format!("{}.control", self.name), control)];
        let mut names = BTreeSet::new();
        let mut versions = BTreeSet::new();
        let mut reachable = BTreeSet::new();
        if source == InstallSource::Native {
            versions.insert(self.default_version);
            reachable.insert(self.default_version);
        }
        let mut total = 0usize;
        for script in self.scripts {
            if !component(script.to)
                || script
                    .from
                    .is_some_and(|from| !component(from) || from == script.to)
            {
                return Err("invalid script version or self-update".into());
            }
            versions.insert(script.to);
            let suffix = if let Some(from) = script.from {
                versions.insert(from);
                format!("{from}--{}", script.to)
            } else {
                if source == InstallSource::Native {
                    return Err("native installation must not include a base SQL file".into());
                }
                if script.sql.trim().is_empty() {
                    return Err("base SQL must not be blank".into());
                }
                reachable.insert(script.to);
                script.to.to_owned()
            };
            let file = format!("{}--{suffix}.sql", self.name);
            // Actual filesystem components also have limits; fail before writes.
            if file.len() > 255 || !names.insert(file.clone()) {
                return Err("duplicate or overlong SQL filename".into());
            }
            total = total
                .checked_add(script.sql.len())
                .ok_or("SQL size overflow")?;
            if total > 4 * 1024 * 1024 || script.sql.contains('\0') || versions.len() > 1024 {
                return Err("SQL package exceeds host limits or contains NUL".into());
            }
            files.push((file, script.sql.to_owned()));
        }
        loop {
            let before = reachable.len();
            for script in self.scripts {
                if script.from.is_some_and(|from| reachable.contains(from)) {
                    reachable.insert(script.to);
                }
            }
            if reachable.len() == before {
                break;
            }
        }
        if !reachable.contains(self.default_version) {
            return Err("default version is not reachable from a base install".into());
        }
        if files[0].0.len() > 255 {
            return Err("overlong control filename".into());
        }
        for version in options.version_controls {
            if !component(version.version) {
                return Err("invalid secondary control version".into());
            }
            let filename = format!("{}--{}.control", self.name, version.version);
            if filename.len() > 255 || !names.insert(filename.clone()) {
                return Err("duplicate or overlong secondary control filename".into());
            }
            let mut body = String::new();
            if let Some(module) = version.native_module {
                module_identity(module)?;
                body.push_str(&format!("native_module = '{module}'\n"));
            }
            if let Some(schema) = version.schema {
                if schema.is_empty()
                    || schema.len() > 255
                    || schema.chars().any(char::is_control)
                    || version.relocatable == Some(true)
                {
                    return Err("invalid fixed schema or relocatable conflict".into());
                }
                body.push_str(&format!("schema = '{}'\n", schema.replace('\'', "''")));
            }
            if let Some(relocatable) = version.relocatable {
                body.push_str(&format!("relocatable = {relocatable}\n"));
            }
            if let Some(requires) = version.requires {
                requirement_names(self.name, requires)?;
                body.push_str(&format!("requires = '{}'\n", requires.join(", ")));
            }
            // Each supported field is bounded above, well below 64 KiB in total.
            // Do not fold these into primary metadata or sort dependency names:
            // omission, explicit clearing and author order are meaningful.
            files.push((filename, body));
        }
        files.sort_by(|a, b| a.0.cmp(&b.0));
        Ok(files)
    }

    /// Write into a CLI-owned staging directory. Files are create-new only;
    /// failure can leave partial output, which the CLI marks incomplete.
    /// Not atomic publication, not a defense against concurrently mutable inputs.
    pub fn write_to(&self, directory: &Path) -> Result<()> {
        self.write_to_with_source(directory, InstallSource::Sql)
    }

    /// Same create-new ownership contract as `write_to`, with an explicit source.
    pub fn write_to_with_source(&self, directory: &Path, source: InstallSource) -> Result<()> {
        self.write_to_with_options(
            directory,
            PackageOptions {
                install_source: source,
                ..PackageOptions::default()
            },
        )
    }

    /// Same create-new contract, with validated Extension dependency declarations.
    pub fn write_to_with_options(
        &self,
        directory: &Path,
        options: PackageOptions<'_>,
    ) -> Result<()> {
        let files = self.render_with_options(options)?;
        for (name, _) in &files {
            if std::fs::symlink_metadata(directory.join(name)).is_ok() {
                return Err(format!("schema output already exists: {name}"));
            }
        }
        for (name, body) in files {
            let mut file = OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(directory.join(&name))
                .map_err(|e| format!("cannot create {name}: {e}"))?;
            file.write_all(body.as_bytes())
                .and_then(|_| file.sync_all())
                .map_err(|e| format!("cannot write {name}: {e}"))?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn version_controls_preserve_inheritance_clearing_and_deterministic_files() {
        let scripts = [Script {
            from: None,
            to: "1",
            sql: "SELECT 1;",
        }];
        let package = Package {
            name: "p",
            default_version: "1",
            native_module: None,
            scripts: &scripts,
        };
        let versions = [
            VersionControl {
                requires: Some(&["zulu", "alpha"]),
                ..VersionControl::new("2")
            },
            VersionControl {
                requires: Some(&[]),
                ..VersionControl::new("3")
            },
            VersionControl::new("4"),
            VersionControl {
                native_module: Some("org.native"),
                schema: Some("客户's # schema"),
                relocatable: Some(false),
                ..VersionControl::new("1")
            },
        ];
        let options = PackageOptions {
            requires: &["base"],
            version_controls: &versions,
            ..PackageOptions::default()
        };
        let files = package.render_with_options(options).unwrap();
        let body = |name: &str| {
            files
                .iter()
                .find(|(file, _)| file == name)
                .unwrap()
                .1
                .as_str()
        };
        assert_eq!(
            body("p.control"),
            "default_version = '1'\nrequires = 'base'\n"
        );
        assert_eq!(
            body("p--1.control"),
            "native_module = 'org.native'\nschema = '客户''s # schema'\nrelocatable = false\n"
        );
        assert_eq!(body("p--2.control"), "requires = 'zulu, alpha'\n");
        assert_eq!(body("p--3.control"), "requires = ''\n");
        assert_eq!(body("p--4.control"), "");
        assert_eq!(body("p--1.sql"), scripts[0].sql);
        let reversed: Vec<_> = versions.into_iter().rev().collect();
        assert_eq!(
            files,
            package
                .render_with_options(PackageOptions {
                    version_controls: &reversed,
                    ..options
                })
                .unwrap()
        );
        let native = Package {
            native_module: Some("org.native"),
            scripts: &[],
            ..package
        };
        assert!(native
            .render_with_options(PackageOptions {
                install_source: InstallSource::Native,
                ..options
            })
            .is_ok());
    }

    #[test]
    fn invalid_version_metadata_is_rejected_before_writing_any_file() {
        let directory =
            std::env::temp_dir().join(format!("seekdb-version-controls-{}", std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let scripts = [Script {
            from: None,
            to: "1",
            sql: "SELECT 1;",
        }];
        let package = Package {
            name: "p",
            default_version: "1",
            native_module: None,
            scripts: &scripts,
        };
        let too_long = "a".repeat(256);
        let overlong_filename = "v".repeat(250);
        let many_names: Vec<_> = (0..65).map(|i| format!("dep_{i}")).collect();
        let names: Vec<_> = many_names.iter().map(String::as_str).collect();
        let cases = [
            VersionControl::new("../escape"),
            VersionControl::new("1--2"),
            VersionControl::new(&overlong_filename),
            VersionControl {
                requires: Some(&["p"]),
                ..VersionControl::new("2")
            },
            VersionControl {
                requires: Some(&["a", "a"]),
                ..VersionControl::new("2")
            },
            VersionControl {
                requires: Some(&["a'\ntrusted=true"]),
                ..VersionControl::new("2")
            },
            VersionControl {
                requires: Some(&names),
                ..VersionControl::new("2")
            },
            VersionControl {
                native_module: Some("Bad.Module"),
                ..VersionControl::new("2")
            },
            VersionControl {
                schema: Some(""),
                ..VersionControl::new("2")
            },
            VersionControl {
                schema: Some("a\nb"),
                ..VersionControl::new("2")
            },
            VersionControl {
                schema: Some(&too_long),
                ..VersionControl::new("2")
            },
            VersionControl {
                schema: Some("fixed"),
                relocatable: Some(true),
                ..VersionControl::new("2")
            },
        ];
        for control in cases {
            assert!(package
                .write_to_with_options(
                    &directory,
                    PackageOptions {
                        version_controls: &[control],
                        ..PackageOptions::default()
                    }
                )
                .is_err());
            assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 0);
        }
        let duplicates = [VersionControl::new("2"), VersionControl::new("2")];
        assert!(package
            .render_with_options(PackageOptions {
                version_controls: &duplicates,
                ..PackageOptions::default()
            })
            .is_err());
        let excess = vec![VersionControl::new("2"); 4095];
        assert!(package
            .render_with_options(PackageOptions {
                version_controls: &excess,
                ..PackageOptions::default()
            })
            .unwrap_err()
            .contains("4096"));
        let controls = [VersionControl {
            requires: Some(&names[..64]),
            ..VersionControl::new("2")
        }];
        let options = PackageOptions {
            version_controls: &controls,
            ..PackageOptions::default()
        };
        assert!(package.render_with_options(options).is_ok());
        std::fs::write(directory.join("p--2.control"), "preserve me").unwrap();
        assert!(package.write_to_with_options(&directory, options).is_err());
        assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 1);
        assert_eq!(
            std::fs::read_to_string(directory.join("p--2.control")).unwrap(),
            "preserve me"
        );
        std::fs::remove_file(directory.join("p--2.control")).unwrap();
        package.write_to_with_options(&directory, options).unwrap();
        assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 3);
        std::fs::remove_dir_all(directory).unwrap();
    }
    #[test]
    fn dependency_options_preserve_sql_and_native_modes() {
        let sql = [Script {
            from: None,
            to: "1",
            sql: "SELECT 1;",
        }];
        let mut package = Package {
            name: "consumer",
            default_version: "1",
            native_module: None,
            scripts: &sql,
        };
        let mut options = PackageOptions {
            requires: &["zulu", "alpha"],
            ..PackageOptions::default()
        };
        let files = package.render_with_options(options).unwrap();
        assert_eq!(
            files
                .iter()
                .find(|(name, _)| name == "consumer.control")
                .unwrap()
                .1,
            "default_version = '1'\nrequires = 'zulu, alpha'\n"
        );
        assert_eq!(
            package.render().unwrap(),
            package
                .render_with_options(PackageOptions::default())
                .unwrap()
        );
        package.scripts = &[];
        package.native_module = Some("org.native");
        options.install_source = InstallSource::Native;
        let files = package.render_with_options(options).unwrap();
        assert_eq!(files, [("consumer.control".into(),
            "default_version = '1'\nnative_module = 'org.native'\ninstall_source = 'native'\nrequires = 'zulu, alpha'\n".into())]);
    }

    #[test]
    fn invalid_dependencies_fail_before_writes() {
        let directory =
            std::env::temp_dir().join(format!("seekdb-requires-schema-{}", std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let sql = [Script {
            from: None,
            to: "1",
            sql: "SELECT 1;",
        }];
        let package = Package {
            name: "consumer",
            default_version: "1",
            native_module: None,
            scripts: &sql,
        };
        for requires in [
            vec![""],
            vec!["consumer"],
            vec!["same", "same"],
            vec!["../escape"],
            vec!["a'\nkey='x"],
            vec!["a,b"],
            vec!["a\0b"],
            vec!["white space"],
            vec!["a--b"],
        ] {
            assert!(package
                .write_to_with_options(
                    &directory,
                    PackageOptions {
                        requires: &requires,
                        ..PackageOptions::default()
                    }
                )
                .is_err());
            assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 0);
        }
        let names: Vec<_> = (0..65).map(|i| format!("dependency_{i}")).collect();
        let refs: Vec<_> = names.iter().map(String::as_str).collect();
        assert!(package
            .render_with_options(PackageOptions {
                requires: &refs,
                ..PackageOptions::default()
            })
            .is_err());
        assert!(package
            .render_with_options(PackageOptions {
                requires: &refs[..64],
                ..PackageOptions::default()
            })
            .is_ok());
        let too_long = "a".repeat(256);
        assert!(package
            .render_with_options(PackageOptions {
                requires: &[&too_long],
                ..PackageOptions::default()
            })
            .is_err());
        let options = PackageOptions {
            requires: &["provider"],
            ..PackageOptions::default()
        };
        package.write_to_with_options(&directory, options).unwrap();
        let before = std::fs::read(directory.join("consumer.control")).unwrap();
        assert!(package.write_to_with_options(&directory, options).is_err());
        assert_eq!(
            std::fs::read(directory.join("consumer.control")).unwrap(),
            before
        );
        std::fs::remove_dir_all(directory).unwrap();
    }
    #[test]
    fn native_source_requires_a_module_and_preserves_only_explicit_updates() {
        let mut package = Package {
            name: "native_ops",
            default_version: "1",
            native_module: Some("org.native"),
            scripts: &[],
        };
        let files = package.render_with_source(InstallSource::Native).unwrap();
        assert_eq!(
            files,
            [(
                "native_ops.control".into(),
                "default_version = '1'\nnative_module = 'org.native'\ninstall_source = 'native'\n"
                    .into()
            )]
        );
        assert!(package.render().is_err());
        package.native_module = None;
        assert!(package.render_with_source(InstallSource::Native).is_err());
        package.native_module = Some("org.native");
        let updates = [Script {
            from: Some("1"),
            to: "2",
            sql: "-- explicit empty migration",
        }];
        package.scripts = &updates;
        let files = package.render_with_source(InstallSource::Native).unwrap();
        assert_eq!(files.len(), 2);
        assert!(files
            .iter()
            .any(|(name, body)| name == "native_ops--1--2.sql" && body == updates[0].sql));
        let base = [Script {
            from: None,
            to: "1",
            sql: "SELECT 1;",
        }];
        package.scripts = &base;
        assert!(package.render_with_source(InstallSource::Native).is_err());
        assert!(package.render().is_ok()); // Existing callers retain SQL mode.
    }
    #[test]
    fn native_source_write_is_create_new_without_placeholder_sql() {
        let directory =
            std::env::temp_dir().join(format!("seekdb-native-schema-{}", std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let package = Package {
            name: "p",
            default_version: "1",
            native_module: Some("org.native"),
            scripts: &[],
        };
        package
            .write_to_with_source(&directory, InstallSource::Native)
            .unwrap();
        assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 1);
        let before = std::fs::read(directory.join("p.control")).unwrap();
        assert!(package
            .write_to_with_source(&directory, InstallSource::Native)
            .is_err());
        assert_eq!(std::fs::read(directory.join("p.control")).unwrap(), before);
        std::fs::remove_dir_all(directory).unwrap();
    }
    #[test]
    fn output_files_are_create_new_and_validation_precedes_writes() {
        let directory =
            std::env::temp_dir().join(format!("seekdb-sdk-schema-{}", std::process::id()));
        std::fs::create_dir(&directory).unwrap();
        let scripts = [Script {
            from: None,
            to: "1",
            sql: "SELECT 1;",
        }];
        let mut package = Package {
            name: "p",
            default_version: "missing",
            native_module: None,
            scripts: &scripts,
        };
        assert!(package.write_to(&directory).is_err());
        assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 0);
        package.default_version = "1";
        std::fs::write(directory.join("p.control"), b"keep existing").unwrap();
        assert!(package.write_to(&directory).is_err());
        assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 1);
        assert_eq!(
            std::fs::read(directory.join("p.control")).unwrap(),
            b"keep existing"
        );
        std::fs::remove_file(directory.join("p.control")).unwrap();
        #[cfg(unix)]
        {
            std::os::unix::fs::symlink(directory.join("absent"), directory.join("p--1.sql"))
                .unwrap();
            assert!(package.write_to(&directory).is_err());
            assert!(!directory.join("absent").exists());
            assert!(!directory.join("p.control").exists());
            std::fs::remove_file(directory.join("p--1.sql")).unwrap();
        }
        package.write_to(&directory).unwrap();
        assert_eq!(
            std::fs::read_to_string(directory.join("p--1.sql")).unwrap(),
            scripts[0].sql
        );
        std::fs::remove_dir_all(directory).unwrap();
    }
    fn definition() -> FunctionDefinition<'static> {
        FunctionDefinition {
            object_id: c"test.f",
            sql_name: c"native_f",
            argument_types: &[c"core.type.bytes"],
            result_type: c"core.type.int64",
            service_id: c"test.f",
            minimum_version: sys::Version {
                major: 1,
                minor: 0,
                patch: 0,
            },
            maximum_version_exclusive: sys::Version {
                major: 2,
                minor: 0,
                patch: 0,
            },
            required_capabilities: 0,
            flags: sys::DETERMINISTIC,
        }
    }
    #[test]
    fn wrapper_reuses_types_flags_and_quotes_identifiers() {
        let sql = scalar_wrapper(
            "select",
            &definition(),
            &[("from", SqlType::Text)],
            SqlType::BigInt,
            SqlAccess::NoSql,
        )
        .unwrap();
        assert_eq!(sql, "CREATE FUNCTION `select`(`from` TEXT)\nRETURNS BIGINT\nDETERMINISTIC\nNO SQL\nSQL SECURITY INVOKER\nRETURN `native_f`(`from`);\n");
        let mut definition = definition();
        definition.flags = 0;
        let sql = scalar_wrapper(
            "f",
            &definition,
            &[("a", SqlType::LongBlob)],
            SqlType::BigInt,
            SqlAccess::ReadsSqlData,
        )
        .unwrap();
        assert!(sql.contains("LONGBLOB)\nRETURNS BIGINT\nNOT DETERMINISTIC\nREADS SQL DATA"));
    }
    #[test]
    fn wrapper_rejects_shadowing_injection_arity_and_type_erasure() {
        for name in ["NATIVE_F", "x.y", "a`); DROP TABLE t;--", ""] {
            assert!(scalar_wrapper(
                name,
                &definition(),
                &[("a", SqlType::Text)],
                SqlType::BigInt,
                SqlAccess::NoSql
            )
            .is_err());
        }
        assert!(
            scalar_wrapper("f", &definition(), &[], SqlType::BigInt, SqlAccess::NoSql).is_err()
        );
        assert!(scalar_wrapper(
            "f",
            &definition(),
            &[("a", SqlType::BigInt)],
            SqlType::BigInt,
            SqlAccess::NoSql
        )
        .is_err());
        let mut definition = definition();
        definition.result_type = c"custom.type";
        assert!(scalar_wrapper(
            "f",
            &definition,
            &[("a", SqlType::Text)],
            SqlType::LongBlob,
            SqlAccess::NoSql
        )
        .is_err());
        definition.result_type = c"core.type.int64";
        definition.argument_types = &[c"core.type.bytes", c"core.type.bytes"];
        assert!(scalar_wrapper(
            "f",
            &definition,
            &[("a", SqlType::Text), ("A", SqlType::Text)],
            SqlType::BigInt,
            SqlAccess::NoSql
        )
        .is_err());
    }
    #[test]
    fn package_preserves_sql_and_explicit_update_edges() {
        let scripts = [
            Script {
                from: None,
                to: "1.0",
                sql: "CREATE PROCEDURE p() BEGIN SELECT 'a;b'; END; -- tail",
            },
            Script {
                from: Some("1.0"),
                to: "2.0",
                sql: "",
            },
        ];
        let mut package = Package {
            name: "text_ops",
            default_version: "2.0",
            native_module: Some("org.text"),
            scripts: &scripts,
        };
        let files = package.render().unwrap();
        assert_eq!(files[0].1, "");
        assert_eq!(files[1].1, scripts[0].sql);
        assert_eq!(
            files[2].1,
            "default_version = '2.0'\nnative_module = 'org.text'\n"
        );
        package.default_version = "3.0";
        assert!(package.render().is_err());
        package.default_version = "2.0";
        package.native_module = Some("bad'\nkey='x");
        assert!(package.render().is_err());
        package.native_module = None;
        package.name = "../escape";
        assert!(package.render().is_err());
    }
    #[test]
    fn invalid_scripts_fail_before_output() {
        for scripts in [
            vec![Script {
                from: None,
                to: "1",
                sql: " ",
            }],
            vec![Script {
                from: None,
                to: "1",
                sql: "SELECT '\0';",
            }],
            vec![
                Script {
                    from: None,
                    to: "1",
                    sql: "SELECT 1;",
                },
                Script {
                    from: None,
                    to: "1",
                    sql: "SELECT 2;",
                },
            ],
            vec![Script {
                from: Some("1"),
                to: "1",
                sql: "",
            }],
        ] {
            assert!(Package {
                name: "p",
                default_version: "1",
                native_module: None,
                scripts: &scripts
            }
            .render()
            .is_err());
        }
    }
}
