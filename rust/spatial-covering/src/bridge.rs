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

use crate::{spatial_cover, spatial_filter_mbr};

#[cxx::bridge(namespace = "oceanbase::common::spatial")]
pub mod ffi {
    #[repr(u8)]
    enum CoveringKind {
        Index = 0,
        Query = 1,
        CoveredBy = 2,
    }

    #[repr(u8)]
    enum MbrRelation {
        Covers = 0,
        Intersects = 1,
        CoveredBy = 2,
        DWithin = 3,
        DFullyWithin = 4,
    }

    #[repr(u8)]
    enum ErrorCode {
        None = 0,
        InvalidWkb = 1,
        InvalidArgument = 2,
        Unsupported = 3,
        Internal = 4,
    }

    struct Bounds {
        min_x: f64,
        max_x: f64,
        min_y: f64,
        max_y: f64,
    }

    struct Mbr {
        x_min: f64,
        x_max: f64,
        y_min: f64,
        y_max: f64,
        is_point: bool,
        geographic: bool,
    }

    struct CellRange {
        start: u64,
        end: u64,
    }

    struct CoveringResult {
        code: ErrorCode,
        message: String,
        cells: Vec<u64>,
        ancestors: Vec<u64>,
        ranges: Vec<CellRange>,
        mbr: Mbr,
    }

    struct MbrFilterResult {
        code: ErrorCode,
        message: String,
        pass_through: bool,
    }

    extern "Rust" {
        fn spatial_cover(
            wkb: &[u8],
            geographic: bool,
            has_projected_bounds: bool,
            projected_bounds: Bounds,
            kind: CoveringKind,
            distance_radians: f64,
        ) -> CoveringResult;
        fn spatial_filter_mbr(index: Mbr, query: Mbr, relation: MbrRelation) -> MbrFilterResult;
    }
}
