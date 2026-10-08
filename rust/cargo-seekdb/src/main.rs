// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use std::env;
use std::ffi::OsString;
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};
mod scaffold;
mod schema;

const HELP: &str = "cargo seekdb new NAME --seekdb-root DIR [--output NEW_DIR] [--plugin-id ID]\n\
cargo seekdb schema --manifest-path Cargo.toml --output NEW_DIR [--example NAME]\n\
cargo seekdb package --build-dir DIR --target CMAKE_TARGET --output NEW_DIR [--jobs N]\n\n\
Create an independent Rust plugin project, or build/audit and package one.\n\
Build and audit a configured Rust plugin, then copy its library and plugin.toml\n\
using CMake's generated package recipe. Existing outputs are never overwritten.\n\
Failed outputs retain .seekdb-package-incomplete or .seekdb-project-incomplete.\n\
These commands\n\
do not sign packages or install into a server. Schema executes the project's\n\
trusted seekdb_schema Cargo example and retains failed output with a marker.\n";
const INCOMPLETE: &str = ".seekdb-package-incomplete";

#[derive(Debug, PartialEq)]
struct Package {
    build: PathBuf,
    target: String,
    output: PathBuf,
    jobs: u32,
}
#[derive(Debug, PartialEq)]
enum Action {
    Package(Package),
    New(scaffold::Spec),
    Schema(schema::Spec),
}

fn parse(mut args: Vec<OsString>) -> Result<Option<Action>, String> {
    if args.first().is_some_and(|arg| arg == "seekdb") {
        args.remove(0); // Cargo supplies its subcommand as argv[1].
    }
    if args.is_empty() || args == [OsString::from("--help")] || args == [OsString::from("-h")] {
        return Ok(None);
    }
    if args.first().is_some_and(|arg| arg == "new") {
        return scaffold::parse(&args[1..]).map(|spec| spec.map(Action::New));
    }
    if args.first().is_some_and(|arg| arg == "schema") {
        return schema::parse(&args[1..]).map(|spec| spec.map(Action::Schema));
    }
    if args.first().is_none_or(|arg| arg != "package") {
        return Err("expected new, schema or package; use --help".into());
    }
    let (mut build, mut target, mut output, mut jobs) = (None, None, None, None);
    let mut options = args[1..].iter();
    while let Some(option) = options.next() {
        if option == "--help" || option == "-h" {
            return Ok(None);
        }
        let value = options
            .next()
            .ok_or_else(|| format!("missing value for {}", option.to_string_lossy()))?;
        match option.to_str() {
            Some("--build-dir") if build.is_none() => build = Some(PathBuf::from(value)),
            Some("--output") if output.is_none() => output = Some(PathBuf::from(value)),
            Some("--target") if target.is_none() => {
                let text = value.to_str().ok_or("target must be ASCII")?;
                if text.is_empty()
                    || text.starts_with('-')
                    || !text
                        .bytes()
                        .all(|c| c.is_ascii_alphanumeric() || b"_-.+".contains(&c))
                {
                    return Err(
                        "target must be an ASCII CMake name without slashes or a leading dash"
                            .into(),
                    );
                }
                target = Some(text.to_owned());
            }
            Some("--jobs") if jobs.is_none() => {
                let n = value
                    .to_str()
                    .and_then(|v| v.parse::<u32>().ok())
                    .filter(|v| *v > 0)
                    .ok_or("jobs must be a positive integer")?;
                jobs = Some(n);
            }
            _ => {
                return Err(format!(
                    "unknown or repeated option {}",
                    option.to_string_lossy()
                ))
            }
        }
    }
    Ok(Some(Action::Package(Package {
        build: build.ok_or("--build-dir is required")?,
        target: target.ok_or("--target is required")?,
        output: output.ok_or("--output is required")?,
        jobs: jobs.unwrap_or(2),
    })))
}

fn run(command: &mut Command) -> Result<(), String> {
    // No shell: spaces and shell metacharacters in paths remain literal args.
    let status = command
        .status()
        .map_err(|e| format!("could not execute {command:?}: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("command failed ({status}): {command:?}"))
    }
}

fn reserve_output(path: &Path) -> Result<PathBuf, String> {
    reserve_directory(
        path,
        INCOMPLETE,
        b"Packaging in progress or failed. Do not deploy this directory.\n",
    )
}

fn reserve_directory(
    path: &Path,
    marker_name: &str,
    description: &[u8],
) -> Result<PathBuf, String> {
    // Canonicalize only the existing parent. create_dir must reject any final
    // existing file, directory or symlink, including a concurrent packager.
    let name = path.file_name().ok_or("output must name a new directory")?;
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let parent = parent
        .canonicalize()
        .map_err(|e| format!("output parent must exist: {e}"))?;
    let output = parent.join(name);
    fs::create_dir(&output)
        .map_err(|e| format!("cannot reserve new output {}: {e}", output.display()))?;
    let marker = output.join(marker_name);
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&marker)
        .map_err(|e| format!("cannot mark incomplete output {}: {e}", output.display()))?;
    file.write_all(description)
        .and_then(|_| file.sync_all())
        .map_err(|e| format!("cannot write incomplete marker: {e}"))?;
    Ok(output)
}

fn validate_package(output: &Path) -> Result<(), String> {
    let manifest = fs::symlink_metadata(output.join("plugin.toml"))
        .map_err(|e| format!("package recipe did not produce plugin.toml: {e}"))?;
    if !manifest.is_file() || manifest.len() == 0 {
        return Err("package manifest must be a nonempty regular file".into());
    }
    let mut libraries = 0;
    for entry in fs::read_dir(output).map_err(|e| format!("cannot inspect package: {e}"))? {
        let path = entry
            .map_err(|e| format!("cannot inspect package entry: {e}"))?
            .path();
        if matches!(
            path.extension().and_then(|s| s.to_str()),
            Some("so" | "dylib" | "dll")
        ) {
            let metadata =
                fs::symlink_metadata(&path).map_err(|e| format!("cannot inspect library: {e}"))?;
            if !metadata.is_file() || metadata.len() == 0 {
                return Err("package library must be a nonempty regular file".into());
            }
            libraries += 1;
        }
    }
    if libraries != 1 {
        return Err("package recipe must produce exactly one plugin library".into());
    }
    Ok(())
}

fn package(spec: Package) -> Result<PathBuf, String> {
    let build = spec
        .build
        .canonicalize()
        .map_err(|e| format!("invalid build directory: {e}"))?;
    if !build.join("CMakeCache.txt").is_file() {
        return Err(
            "build directory has no CMakeCache.txt; configure seekdb with plugins enabled first"
                .into(),
        );
    }
    let recipe = build
        .join("seekdb-plugin-packages")
        .join(format!("{}.cmake", spec.target));
    if !recipe.is_file() {
        return Err(
            "target has no Rust plugin package recipe; reconfigure the CMake build first".into(),
        );
    }
    if fs::symlink_metadata(&spec.output).is_ok() {
        return Err(format!("output already exists: {}", spec.output.display()));
    }
    // Reserve before invoking any external process; retain it on every failure.
    let output = reserve_output(&spec.output)?;
    let work = || -> Result<(), String> {
        run(Command::new("cmake")
            .arg("--build")
            .arg(&build)
            .arg("--target")
            .arg(&spec.target)
            .arg("--parallel")
            .arg(spec.jobs.to_string()))?;
        let mut destination = OsString::from("-DSEEKDB_PLUGIN_PACKAGE_DIR=");
        destination.push(output.as_os_str());
        run(Command::new("cmake")
            .arg(destination)
            .arg("-P")
            .arg(&recipe)
            .env_remove("DESTDIR"))?;
        validate_package(&output)?;
        fs::remove_file(output.join(INCOMPLETE))
            .map_err(|e| format!("cannot finish package marker: {e}"))?;
        Ok(())
    };
    work().map_err(|e| format!("{e}\nIncomplete output retained at {}", output.display()))?;
    Ok(output)
}

fn main() -> ExitCode {
    let result = parse(env::args_os().skip(1).collect()).and_then(|action| {
        action
            .map(|action| match action {
                Action::Package(spec) => package(spec).map(|path| ("package", path)),
                Action::New(spec) => scaffold::create(spec).map(|path| ("project", path)),
                Action::Schema(spec) => schema::generate(spec).map(|path| ("schema", path)),
            })
            .transpose()
    });
    match result {
        Ok(None) => {
            print!("{HELP}");
            ExitCode::SUCCESS
        }
        Ok(Some((kind, output))) => {
            println!("seekdb plugin {kind} ready: {}", output.display());
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("cargo-seekdb: {error}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    fn args(items: &[&str]) -> Vec<OsString> {
        items.iter().map(OsString::from).collect()
    }
    fn temporary() -> PathBuf {
        let path = env::temp_dir().join(format!(
            "cargo-seekdb-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        path
    }
    #[test]
    fn cargo_and_direct_entry_agree() {
        let direct = args(&[
            "package",
            "--build-dir",
            "build space",
            "--target",
            "seekdb_text",
            "--output",
            "new package",
        ]);
        let mut cargo = vec!["seekdb".into()];
        cargo.extend(direct.clone());
        assert_eq!(parse(cargo), parse(direct));
        assert!(parse(args(&["seekdb", "--help"])).unwrap().is_none());
    }
    #[test]
    fn reject_invalid_or_duplicate_arguments() {
        for tail in [
            vec!["--jobs", "0"],
            vec!["--jobs", "-1"],
            vec!["--target", "../bad"],
            vec!["--target", "--help"],
            vec!["--bogus", "x"],
            vec!["--output"],
        ] {
            let mut input = vec!["package"];
            input.extend(tail);
            assert!(parse(args(&input)).is_err());
        }
        assert!(parse(args(&["package", "--target", "a", "--target", "b"])).is_err());
    }
    #[test]
    fn output_is_reserved_once_and_kept_incomplete() {
        let root = temporary();
        let path = root.join("package with spaces;literal");
        assert_eq!(reserve_output(&path).unwrap(), path);
        assert!(path.join(INCOMPLETE).is_file());
        assert!(reserve_output(&path).is_err());
        fs::write(path.join("owned"), b"keep").unwrap();
        assert!(reserve_output(&path).is_err());
        assert_eq!(fs::read(path.join("owned")).unwrap(), b"keep");
        fs::remove_dir_all(root).unwrap();
    }
    #[cfg(unix)]
    #[test]
    fn output_symlink_is_not_followed() {
        let root = temporary();
        let link = root.join("link");
        std::os::unix::fs::symlink(root.join("absent"), &link).unwrap();
        assert!(reserve_output(&link).is_err());
        assert!(!root.join("absent").exists());
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn incomplete_or_empty_library_is_not_a_package() {
        let root = temporary();
        fs::write(root.join("plugin.toml"), b"manifest").unwrap();
        assert!(validate_package(&root).is_err());
        fs::write(root.join("plugin.so"), b"").unwrap();
        assert!(validate_package(&root).is_err());
        fs::write(root.join("plugin.so"), b"fixture, not an audited binary").unwrap();
        assert!(validate_package(&root).is_ok());
        fs::write(root.join("second.so"), b"unexpected").unwrap();
        assert!(validate_package(&root).is_err());
        fs::remove_dir_all(root).unwrap();
    }
}
