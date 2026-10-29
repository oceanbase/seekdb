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

#[cfg(target_os = "ios")]
mod ios;

#[cfg(target_os = "ios")]
pub(crate) use ios::{socket_alias_patterns, INITIAL_DEFAULTS, STARTUP_PARAMETERS};

#[cfg(not(target_os = "ios"))]
pub(crate) const STARTUP_PARAMETERS: &[&str] = &[];

#[cfg(not(target_os = "ios"))]
pub(crate) const INITIAL_DEFAULTS: &[&str] = &[];

#[cfg(not(target_os = "ios"))]
pub(crate) fn socket_alias_patterns() -> Vec<String> {
    Vec::new()
}
