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

use crate::{Error, Mbr, Result};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MbrRelation {
    Covers,
    Intersects,
    CoveredBy,
    DWithin,
    DFullyWithin,
}

pub fn filter(index: Mbr, query: Mbr, relation: MbrRelation) -> Result<bool> {
    validate(index)?;
    validate(query)?;
    if relation == MbrRelation::DFullyWithin {
        return Err(Error::Unsupported("DFullyWithin MBR filtering"));
    }
    if index.geographic != query.geographic {
        return Err(Error::InvalidArgument("MBR coordinate systems differ"));
    }
    if index.geographic {
        let index_rect = index.to_rect();
        let query_rect = query.to_rect();
        if index.is_point && query.is_point && index_rect.approx_eq(query_rect) {
            return Ok(false);
        }
        Ok(match relation {
            MbrRelation::Covers => !query_rect.contains(index_rect),
            MbrRelation::Intersects | MbrRelation::DWithin => !index_rect.intersects(query_rect),
            MbrRelation::CoveredBy => !index_rect.contains(query_rect),
            MbrRelation::DFullyWithin => unreachable!(),
        })
    } else {
        Ok(match relation {
            MbrRelation::Covers => !contains(query, index),
            MbrRelation::Intersects | MbrRelation::DWithin => !intersects(index, query),
            MbrRelation::CoveredBy => !contains(index, query),
            MbrRelation::DFullyWithin => unreachable!(),
        })
    }
}

fn validate(mbr: Mbr) -> Result<()> {
    if mbr.is_empty() {
        return Err(Error::InvalidArgument("cannot filter an empty MBR"));
    }
    if !mbr.x_min.is_finite()
        || !mbr.x_max.is_finite()
        || !mbr.y_min.is_finite()
        || !mbr.y_max.is_finite()
        || mbr.y_min > mbr.y_max
        || (!mbr.geographic && mbr.x_min > mbr.x_max)
        || (mbr.geographic
            && (mbr.x_min.abs() > 180.0
                || mbr.x_max.abs() > 180.0
                || mbr.y_min.abs() > 90.0
                || mbr.y_max.abs() > 90.0))
    {
        return Err(Error::InvalidArgument("invalid MBR"));
    }
    Ok(())
}

fn contains(outer: Mbr, inner: Mbr) -> bool {
    outer.x_min <= inner.x_min
        && outer.x_max >= inner.x_max
        && outer.y_min <= inner.y_min
        && outer.y_max >= inner.y_max
}

fn intersects(left: Mbr, right: Mbr) -> bool {
    left.x_min <= right.x_max
        && left.x_max >= right.x_min
        && left.y_min <= right.y_max
        && left.y_max >= right.y_min
}
