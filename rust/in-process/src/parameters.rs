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

use crate::platform;

const PORT_NOT_ZERO: &str = "TCP is not available for the in-process engine; port must be 0";
const PORT_MODE_NOT_DISABLED: &str =
    "TCP is not available for the in-process engine; mysql_port_mode must be disabled";

pub(crate) fn check(key: &str, value: &str) -> Result<(), &'static str> {
    if key == "port" && !is_zero(value) {
        return Err(PORT_NOT_ZERO);
    }
    if key == "mysql_port_mode" && value != "disabled" {
        return Err(PORT_MODE_NOT_DISABLED);
    }
    Ok(())
}

fn is_zero(value: &str) -> bool {
    let number = value.trim_start_matches([' ', '\t', '\n', '\x0b', '\x0c', '\r']);
    let digits = number.strip_prefix(['+', '-']).unwrap_or(number);
    !digits.is_empty() && digits.bytes().all(|digit| digit == b'0')
}

pub(crate) fn startup() -> Vec<String> {
    ["mysql_port_mode", "disabled"]
        .iter()
        .chain(platform::STARTUP_PARAMETERS)
        .map(|value| value.to_string())
        .collect()
}

pub(crate) fn initial(pairs: &[(String, String)], startup: &[String]) -> Vec<String> {
    let mut result: Vec<String> = ["memory_budget", "1G", "log_disk_size", "2G"]
        .iter()
        .chain(platform::INITIAL_DEFAULTS)
        .map(|value| value.to_string())
        .collect();
    let defaults = result.len();
    for (key, value) in pairs {
        let mut overridden = false;
        for index in (0..defaults).step_by(2) {
            if result[index] == *key {
                result[index + 1].clone_from(value);
                overridden = true;
            }
        }
        if !overridden && key != "port" && !startup.iter().step_by(2).any(|name| name == key) {
            result.push(key.clone());
            result.push(value.clone());
        }
    }
    result
}
