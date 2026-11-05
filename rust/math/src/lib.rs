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

//! Platform-independent binary64 math layer.
//!
//! Every platform links the same pinned pure-Rust `libm` implementation, so a
//! value computed on Linux, macOS, and Windows is bit-identical.  The C++
//! `ob_math_compat` layer must be the only caller of the bridge; SQL and geo
//! code must not call the platform libm directly when the result is
//! observable.

mod bridge;

#[cfg(test)]
mod tests;
