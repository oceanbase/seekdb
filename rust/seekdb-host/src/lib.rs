// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// One host staticlib owns the Rust standard library/runtime. C++ entrypoints
// keep their existing C names; algorithm DSOs remain independent artifacts.
#[cfg(feature = "plugins")]
pub use seekdb_plugin_runtime::*;
pub extern crate sql_nio;
