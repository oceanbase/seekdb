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

#[cxx::bridge(namespace = "oceanbase::rpc")]
pub mod ffi {
    #[repr(u8)]
    enum Method {
        fetch_ls_view,
        fetch_tablet_info,
        fetch_tablet_sstable_info,
        fetch_sstable_macro_info,
        fetch_macro_block,
        get_ls_view_tablet_count,
        check_restore_precondition,
        fetch_standby_palf_base_info,
        fetch_log,
        get_promotion_boundary,
    }
    struct NodeAddress {
        ip: String,
        port: u32,
    }
    struct SourceHop {
        relay: NodeAddress,
        source: NodeAddress,
    }
    struct PromotionBoundary {
        origin: NodeAddress,
        cutover_scn: u64,
        source_chain: Vec<SourceHop>,
    }
    struct Control {
        tablet_count: i64,
        required_disk_size: i64,
        total_tablet_size: i64,
        data_version: u64,
        visited: Vec<NodeAddress>,
        boundary: PromotionBoundary,
    }
    struct Frame {
        control: Control,
        buf: Vec<u8>,
        size: u64,
        entry_type: i32,
        start_lsn: u64,
        max_bytes: u64,
        source_lsn: u64,
        end_scn: u64,
    }
    struct Trace {
        id: String,
    }
    struct CallReport {
        method: Method,
        address: String,
        trace_id: String,
        error: i32,
        elapsed_us: u64,
        server: bool,
    }
    struct Received {
        frame: Frame,
        present: bool,
        error: i32,
    }
    unsafe extern "C++" {
        include!("rust/standby-rpc/include/service.h");
        type Service;
        fn dispatch(
            self: &Service,
            method: Method,
            request: Frame,
            trace: &Trace,
            writer: &Writer,
        ) -> i32;
        fn report_call(report: &CallReport);
    }
    extern "Rust" {
        type ClientHandle;
        fn create_client(address: &str, tls: bool) -> Result<Box<ClientHandle>>;
        type ServerHandle;
        type ClientStream;
        type Writer;
        fn start_server(
            port: u16,
            tls: bool,
            service: SharedPtr<Service>,
        ) -> Result<Box<ServerHandle>>;
        fn stop(self: &mut ServerHandle);
        fn cert_expire_time(self: &ServerHandle) -> i64;
        fn open_stream(
            self: &ClientHandle,
            method: Method,
            request: Frame,
            trace: &str,
            timeout_us: u64,
        ) -> Box<ClientStream>;
        fn resize_buffer(frame: &mut Frame, size: usize);
        fn receive(self: &ClientStream) -> Received;
        fn cancel(self: &ClientStream);
        fn write(self: &Writer, frame: Frame) -> bool;
        fn cancelled(self: &Writer) -> bool;
    }
}
use crate::{
    create_client, resize_buffer, start_server, ClientHandle, ClientStream, ServerHandle, Writer,
};
