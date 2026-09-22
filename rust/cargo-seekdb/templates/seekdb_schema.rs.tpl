//! Executed by cargo seekdb schema; no plugin lifecycle or server connection.
use seekdb_extension::schema::{scalar_wrapper, Package, Script, SqlAccess, SqlType};

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len() != 1 { return Err("expected a CLI-owned staging directory".into()); }
    let sql = scalar_wrapper("@@NAME@@_length", &seekdb_@@NAME@@::chars::DEFINITION,
        &[("input_text", SqlType::Text)], SqlType::BigInt, SqlAccess::NoSql)?;
    Package {
        name: "@@NAME@@_ops", default_version: "1.0", native_module: Some("@@PLUGIN_ID@@"),
        scripts: &[Script { from: None, to: "1.0", sql: &sql }],
    }.write_to(std::path::Path::new(&args[0]))
}
