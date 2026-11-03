// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use std::{
    env,
    ffi::OsString,
    fs::{self, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
};

const INCOMPLETE: &str = ".seekdb-project-incomplete";
#[derive(Debug, PartialEq)]
pub(crate) struct Spec {
    name: String,
    plugin_id: String,
    root: PathBuf,
    output: PathBuf,
}
pub(crate) fn parse(args: &[OsString]) -> Result<Option<Spec>, String> {
    if args == [OsString::from("--help")] || args == [OsString::from("-h")] {
        return Ok(None);
    }
    let name = args
        .first()
        .and_then(|s| s.to_str())
        .ok_or("new requires a project NAME")?;
    if name.is_empty()
        || name.len() > 48
        || !name.as_bytes()[0].is_ascii_lowercase()
        || !name
            .bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_')
    {
        return Err(
            "NAME must be 1..48 lowercase ASCII letters/digits/underscores, starting with a letter"
                .into(),
        );
    }
    let (mut root, mut output, mut plugin_id) = (None, None, None);
    let mut options = args[1..].iter();
    while let Some(option) = options.next() {
        let value = options
            .next()
            .ok_or_else(|| format!("missing value for {}", option.to_string_lossy()))?;
        match option.to_str() {
            Some("--seekdb-root") if root.is_none() => root = Some(PathBuf::from(value)),
            Some("--output") if output.is_none() => output = Some(PathBuf::from(value)),
            Some("--plugin-id") if plugin_id.is_none() => {
                plugin_id = Some(value.to_str().ok_or("plugin ID must be ASCII")?.to_owned())
            }
            _ => {
                return Err(format!(
                    "unknown or repeated option {}",
                    option.to_string_lossy()
                ))
            }
        }
    }
    let plugin_id = plugin_id.unwrap_or_else(|| format!("local.{name}"));
    if plugin_id.is_empty()
        || plugin_id.len() > 120
        || !plugin_id
            .bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || b"._-".contains(&c))
    {
        return Err(
            "plugin ID must be 1..120 lowercase ASCII letters/digits/dots/underscores/hyphens"
                .into(),
        );
    }
    Ok(Some(Spec {
        name: name.into(),
        plugin_id,
        root: root.ok_or("--seekdb-root is required")?,
        output: output.unwrap_or_else(|| PathBuf::from(name)),
    }))
}

// Paths become data in generated TOML/CMake, never source-language fragments.
fn toml_string(text: &str) -> String {
    let mut output = String::from("\"");
    for c in text.chars() {
        match c {
            '\\' => output.push_str("\\\\"),
            '"' => output.push_str("\\\""),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            c if c.is_control() => output.push_str(&format!("\\u{:04X}", c as u32)),
            c => output.push(c),
        }
    }
    output.push('"');
    output
}
fn cmake_literal(text: &str) -> String {
    let mut equals = String::from("=");
    while text.contains(&format!("]{equals}]")) {
        equals.push('=');
    }
    format!("[{equals}[{text}]{equals}]")
}
fn render(template: &str, fields: &[(&str, String)]) -> String {
    // Replace only placeholders from the template, never recurse into inserted
    // path text containing another placeholder-looking sequence.
    let mut result = String::new();
    let mut rest = template;
    while let Some(index) = rest.find("@@") {
        result.push_str(&rest[..index]);
        rest = &rest[index..];
        if let Some((key, value)) = fields.iter().find(|(key, _)| rest.starts_with(key)) {
            result.push_str(value);
            rest = &rest[key.len()..];
        } else {
            result.push_str("@@");
            rest = &rest[2..];
        }
    }
    result.push_str(rest);
    result
}
fn write_new(root: &Path, name: &str, bytes: &[u8]) -> Result<(), String> {
    let path = root.join(name);
    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&path)
        .map_err(|e| format!("cannot create {}: {e}", path.display()))?;
    file.write_all(bytes)
        .and_then(|_| file.sync_all())
        .map_err(|e| format!("cannot write {}: {e}", path.display()))
}
pub(crate) fn create(spec: Spec) -> Result<PathBuf, String> {
    let root = spec
        .root
        .canonicalize()
        .map_err(|e| format!("invalid seekdb source root: {e}"))?;
    for file in [
        "cmake/RustPlugin.cmake",
        "rust/extension-sdk/Cargo.toml",
        "rust/rust-toolchain.toml",
    ] {
        if !root.join(file).is_file() {
            return Err(format!("seekdb source root is missing {file}"));
        }
    }
    let root_text = root
        .to_str()
        .ok_or("seekdb source path must be UTF-8 for CMake/Cargo")?;
    let sdk = root.join("rust/extension-sdk");
    let sdk_text = sdk.to_str().ok_or("SDK path must be UTF-8")?;
    let parent = spec
        .output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."))
        .canonicalize()
        .map_err(|e| format!("output parent must exist: {e}"))?;
    if parent.starts_with(&root) && !parent.starts_with(root.join("plugins")) {
        return Err(
            "in-tree plugins must be created under plugins/; use an external directory otherwise"
                .into(),
        );
    }
    let toolchain = fs::read(root.join("rust/rust-toolchain.toml"))
        .map_err(|e| format!("cannot read pinned Rust toolchain: {e}"))?;
    let fields = [
        ("@@NAME@@", spec.name.clone()),
        ("@@PLUGIN_ID@@", spec.plugin_id),
        ("@@SDK_TOML@@", toml_string(sdk_text)),
        ("@@ROOT_CMAKE@@", cmake_literal(root_text)),
        (
            "@@LIBRARY@@",
            format!(
                "{}seekdb_{}{}",
                env::consts::DLL_PREFIX,
                spec.name,
                env::consts::DLL_SUFFIX
            ),
        ),
    ];
    let files = [
        ("Cargo.toml", include_str!("../templates/Cargo.toml.tpl")),
        (
            "CMakeLists.txt",
            include_str!("../templates/CMakeLists.txt.tpl"),
        ),
        ("plugin.toml", include_str!("../templates/plugin.toml.tpl")),
        ("src/lib.rs", include_str!("../templates/lib.rs.tpl")),
        (
            "examples/seekdb_schema.rs",
            include_str!("../templates/seekdb_schema.rs.tpl"),
        ),
        ("README.md", include_str!("../templates/README.md.tpl")),
        (".gitignore", "/target/\n/build/\n/package/\n/schema/\n"),
    ];
    let output = super::reserve_directory(
        &spec.output,
        INCOMPLETE,
        b"Project creation incomplete. Inspect before building.\n",
    )?;
    let work = || -> Result<(), String> {
        fs::create_dir(output.join("src"))
            .map_err(|e| format!("cannot create source directory: {e}"))?;
        fs::create_dir(output.join("examples"))
            .map_err(|e| format!("cannot create examples directory: {e}"))?;
        for (name, template) in files {
            write_new(&output, name, render(template, &fields).as_bytes())?;
        }
        write_new(&output, "rust-toolchain.toml", &toolchain)?;
        fs::remove_file(output.join(INCOMPLETE))
            .map_err(|e| format!("cannot finish project marker: {e}"))?;
        Ok(())
    };
    work().map_err(|e| format!("{e}\nIncomplete project retained at {}", output.display()))?;
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn args(input: &[&str]) -> Vec<OsString> {
        input.iter().map(OsString::from).collect()
    }
    #[test]
    fn names_options_and_defaults_are_explicit() {
        let spec = parse(&args(&["text", "--seekdb-root", "/source tree"]))
            .unwrap()
            .unwrap();
        assert_eq!(spec.plugin_id, "local.text");
        assert_eq!(spec.output, PathBuf::from("text"));
        assert!(parse(&args(&["--help"])).unwrap().is_none());
        for name in [
            "",
            "../escape",
            "bad-name",
            "1text",
            "Upper",
            "fn();evil",
            "中文",
        ] {
            assert!(parse(&args(&[name, "--seekdb-root", "/source"])).is_err());
        }
        for tail in [
            vec!["--plugin-id", "bad\"id"],
            vec!["--output"],
            vec!["--seekdb-root", "duplicate"],
            vec!["--bogus", "x"],
        ] {
            let mut input = vec!["text", "--seekdb-root", "/source"];
            input.extend(tail);
            assert!(parse(&args(&input)).is_err());
        }
    }
    #[test]
    fn source_language_quoting_does_not_reinterpret_paths() {
        assert_eq!(toml_string("a\\b\"c\n"), "\"a\\\\b\\\"c\\n\"");
        assert_eq!(cmake_literal("a]=]${x}"), "[==[a]=]${x}]==]");
        assert_eq!(
            render(
                "@@A@@:@@B@@",
                &[("@@A@@", "@@B@@".into()), ("@@B@@", "value".into())]
            ),
            "@@B@@:value"
        );
    }
}
