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

use crate::{new_builder, LayerBuilder};

#[cxx::bridge(namespace = "oceanbase::mvt")]
pub(crate) mod ffi {
    extern "Rust" {
        type LayerBuilder;
        fn new_builder() -> Box<LayerBuilder>;
        fn error_code(self: &LayerBuilder) -> i32;
        fn configure(self: &mut LayerBuilder, name: &[u8], extent: u32);
        fn seed_key(self: &mut LayerBuilder, key: &[u8]);
        fn find_key(self: &mut LayerBuilder, key: &[u8]) -> u32;
        fn intern_key(self: &mut LayerBuilder, key: &[u8]) -> u32;
        fn begin_feature(self: &mut LayerBuilder, kind: u32, geometry: &[u32]);
        fn set_id(self: &mut LayerBuilder, id: u64);
        fn add_string(self: &mut LayerBuilder, key: u32, value: &[u8]);
        fn add_float(self: &mut LayerBuilder, key: u32, value: f32);
        fn add_double(self: &mut LayerBuilder, key: u32, value: f64);
        fn add_int(self: &mut LayerBuilder, key: u32, value: i64);
        fn add_uint(self: &mut LayerBuilder, key: u32, value: u64);
        fn add_sint(self: &mut LayerBuilder, key: u32, value: i64);
        fn add_bool(self: &mut LayerBuilder, key: u32, value: bool);
        fn commit_feature(self: &mut LayerBuilder);
        fn encoded_len(self: &LayerBuilder) -> usize;
        fn encode_to(self: &LayerBuilder, output: &mut [u8]) -> bool;
    }
}
