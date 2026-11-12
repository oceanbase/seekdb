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

use crate::bridge::ffi::{Frame, NodeAddress as BridgeAddress, SourceHop as BridgeHop};
use crate::protocol::*;
use tonic::Status;
const MAX_HOPS: usize = 16;

fn check_address(ip: &str, port: u32) -> Result<String, Status> {
    if port == 0 || port > u16::MAX as u32 {
        return Err(Status::invalid_argument("invalid node port"));
    }
    ip.parse::<std::net::IpAddr>()
        .map(|ip| ip.to_string())
        .map_err(|_| Status::invalid_argument("invalid node IP address"))
}
fn decode_address(value: NodeAddress) -> Result<BridgeAddress, Status> {
    Ok(BridgeAddress {
        ip: check_address(&value.ip, value.port)?,
        port: value.port,
    })
}
fn encode_address(value: BridgeAddress) -> Result<NodeAddress, Status> {
    Ok(NodeAddress {
        ip: check_address(&value.ip, value.port)?,
        port: value.port,
    })
}
fn check_visited(values: &[BridgeAddress]) -> Result<(), Status> {
    if values.len() > MAX_HOPS {
        return Err(Status::invalid_argument("promotion path is too deep"));
    }
    let mut seen = std::collections::HashSet::new();
    for value in values {
        let ip = check_address(&value.ip, value.port)?;
        if !seen.insert((ip, value.port)) {
            return Err(Status::invalid_argument("promotion path contains a cycle"));
        }
    }
    Ok(())
}
pub(crate) fn count_frame(count: i64) -> Result<Frame, Status> {
    if count <= 0 {
        return Err(Status::invalid_argument("invalid tablet count"));
    }
    let mut frame = Frame::empty();
    frame.control.tablet_count = count;
    Ok(frame)
}
pub(crate) fn count_response(frame: Frame) -> Result<GetLsViewTabletCountRes, Status> {
    let count = frame.control.tablet_count;
    count_frame(count)?;
    Ok(GetLsViewTabletCountRes {
        tablet_count: count,
    })
}
pub(crate) fn precondition_frame(value: CheckRestorePreconditionRes) -> Result<Frame, Status> {
    if value.required_disk_size < 0 || value.total_tablet_size < 0 || value.data_version == 0 {
        return Err(Status::invalid_argument("invalid restore precondition"));
    }
    let mut frame = Frame::empty();
    frame.control.required_disk_size = value.required_disk_size;
    frame.control.total_tablet_size = value.total_tablet_size;
    frame.control.data_version = value.data_version;
    Ok(frame)
}
pub(crate) fn precondition_response(frame: Frame) -> Result<CheckRestorePreconditionRes, Status> {
    let value = CheckRestorePreconditionRes {
        required_disk_size: frame.control.required_disk_size,
        total_tablet_size: frame.control.total_tablet_size,
        data_version: frame.control.data_version,
    };
    if value.required_disk_size < 0 || value.total_tablet_size < 0 || value.data_version == 0 {
        return Err(Status::invalid_argument("invalid restore precondition"));
    }
    Ok(value)
}
pub(crate) fn boundary_request_frame(value: GetPromotionBoundaryReq) -> Result<Frame, Status> {
    if value.visited.len() > MAX_HOPS {
        return Err(Status::invalid_argument("promotion path is too deep"));
    }
    let mut frame = Frame::empty();
    frame.control.visited = value
        .visited
        .into_iter()
        .map(decode_address)
        .collect::<Result<_, _>>()?;
    check_visited(&frame.control.visited)?;
    Ok(frame)
}
pub(crate) fn boundary_request(frame: Frame) -> Result<GetPromotionBoundaryReq, Status> {
    check_visited(&frame.control.visited)?;
    Ok(GetPromotionBoundaryReq {
        visited: frame
            .control
            .visited
            .into_iter()
            .map(encode_address)
            .collect::<Result<_, _>>()?,
    })
}
pub(crate) fn boundary_frame(value: GetPromotionBoundaryRes) -> Result<Frame, Status> {
    if value.source_chain.len() > MAX_HOPS
        || value.cutover_scn == 0
        || value.cutover_scn == u64::MAX
    {
        return Err(Status::invalid_argument("invalid promotion boundary"));
    }
    let mut frame = Frame::empty();
    frame.control.boundary.origin = decode_address(
        value
            .origin
            .ok_or_else(|| Status::invalid_argument("missing boundary origin"))?,
    )?;
    frame.control.boundary.cutover_scn = value.cutover_scn;
    let mut seen = std::collections::HashSet::new();
    for hop in value.source_chain {
        let relay = decode_address(
            hop.relay
                .ok_or_else(|| Status::invalid_argument("missing relay"))?,
        )?;
        let source = decode_address(
            hop.source
                .ok_or_else(|| Status::invalid_argument("missing source"))?,
        )?;
        if (relay.ip == source.ip && relay.port == source.port)
            || !seen.insert((relay.ip.clone(), relay.port))
        {
            return Err(Status::invalid_argument("cyclic boundary source chain"));
        }
        frame
            .control
            .boundary
            .source_chain
            .push(BridgeHop { relay, source });
    }
    Ok(frame)
}
pub(crate) fn boundary_response(frame: Frame) -> Result<GetPromotionBoundaryRes, Status> {
    let value = frame.control.boundary;
    if value.source_chain.len() > MAX_HOPS
        || value.cutover_scn == 0
        || value.cutover_scn == u64::MAX
    {
        return Err(Status::invalid_argument("invalid promotion boundary"));
    }
    let mut seen = std::collections::HashSet::new();
    let source_chain = value
        .source_chain
        .into_iter()
        .map(|hop| {
            let relay = encode_address(hop.relay)?;
            let source = encode_address(hop.source)?;
            if relay == source || !seen.insert((relay.ip.clone(), relay.port)) {
                return Err(Status::invalid_argument("cyclic boundary source chain"));
            }
            Ok(SourceHop {
                relay: Some(relay),
                source: Some(source),
            })
        })
        .collect::<Result<_, _>>()?;
    Ok(GetPromotionBoundaryRes {
        origin: Some(encode_address(value.origin)?),
        cutover_scn: value.cutover_scn,
        source_chain,
    })
}
