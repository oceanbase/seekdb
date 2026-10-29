// Copyright (c) 2026 OceanBase.
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

use std::env;

pub(crate) const STARTUP_PARAMETERS: &[&str] = &["sql_net_thread_count", "2", "cpu_count", "2"];

pub(crate) const INITIAL_DEFAULTS: &[&str] =
    &["vector_memory_limit", "128M", "datafile_maxsize", "20G"];

pub(crate) fn socket_alias_patterns() -> Vec<String> {
    let mut patterns = Vec::new();
    if let Ok(home) = env::var("HOME") {
        if home.starts_with('/') {
            let home = home
                .strip_prefix("/private")
                .filter(|rest| rest.starts_with("/var/"))
                .unwrap_or(&home);
            patterns.push(format!("{home}/tmp/.XXXXXX"));
        }
    }
    if cfg!(target_env = "sim") {
        patterns.push("/tmp/seekdb-XXXXXX".to_owned());
    }
    patterns
}
