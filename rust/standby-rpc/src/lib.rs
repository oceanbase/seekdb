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

mod bridge;
mod control;
use control::*;
#[allow(non_camel_case_types)]
mod protocol {
    include!(env!("STANDBY_PROTO_RS"));
}
use bridge::ffi::{
    CallReport, Control, Frame, Method, NodeAddress as BridgeAddress,
    PromotionBoundary as BridgeBoundary, Received, Service, Trace,
};
use protocol::standby_service_client::StandbyServiceClient;
use protocol::standby_service_server::{StandbyService, StandbyServiceServer};
use protocol::*;
use rpc::{runtime, Dispatches, Error, MAX_MESSAGE};
const RPC_ERROR: i32 = -4121;
use std::{
    pin::Pin,
    sync::{
        atomic::{AtomicI32, Ordering},
        Arc, Mutex,
    },
    time::{Duration, Instant},
};
use tokio::{sync::mpsc, task::AbortHandle};
use tokio_stream::{wrappers::ReceiverStream, Stream, StreamExt};
use tonic::{transport::Channel, Request, Response, Status};
impl Frame {
    fn empty() -> Self {
        Self {
            control: Control {
                tablet_count: 0,
                required_disk_size: 0,
                total_tablet_size: 0,
                data_version: 0,
                visited: Vec::new(),
                boundary: BridgeBoundary {
                    origin: BridgeAddress {
                        ip: String::new(),
                        port: 0,
                    },
                    cutover_scn: 0,
                    source_chain: Vec::new(),
                },
            },
            buf: Vec::new(),
            size: 0,
            entry_type: 0,
            start_lsn: 0,
            max_bytes: 0,
            source_lsn: 0,
            end_scn: 0,
        }
    }
}
fn resize_buffer(frame: &mut Frame, size: usize) {
    frame.buf.resize(size, 0);
}
fn status_error(status: &Status) -> i32 {
    if let Some(code) = status
        .message()
        .strip_prefix("OB_ERROR:")
        .and_then(|s| s.parse::<i32>().ok())
        .filter(|code| *code < 0)
    {
        return code;
    }
    match status.code() {
        tonic::Code::Ok => 0,
        tonic::Code::DeadlineExceeded => -4012,
        tonic::Code::Cancelled => -4072,
        tonic::Code::Unimplemented => -4007,
        tonic::Code::InvalidArgument => -4002,
        tonic::Code::NotFound => -4018,
        _ => RPC_ERROR,
    }
}
fn error_status(error: i32) -> Status {
    let code = match error {
        -4012 => tonic::Code::DeadlineExceeded,
        -4072 => tonic::Code::Cancelled,
        -4007 => tonic::Code::Unimplemented,
        -4002 => tonic::Code::InvalidArgument,
        -4018 => tonic::Code::NotFound,
        _ => tonic::Code::Internal,
    };
    Status::new(code, format!("OB_ERROR:{error}"))
}
struct Completion {
    report: CallReport,
    started: Instant,
}
impl Completion {
    fn new(method: Method, address: String, trace_id: String, server: bool) -> Self {
        Self {
            report: CallReport {
                method,
                address,
                trace_id,
                error: -4072,
                elapsed_us: 0,
                server,
            },
            started: Instant::now(),
        }
    }
}
impl Drop for Completion {
    fn drop(&mut self) {
        self.report.elapsed_us = self.started.elapsed().as_micros().min(u64::MAX as u128) as u64;
        bridge::ffi::report_call(&self.report);
    }
}
// Service implementations use the same concurrent dispatch contract as the C++ gRPC server.
unsafe impl Send for Service {}
unsafe impl Sync for Service {}
struct Handler {
    service: cxx::SharedPtr<Service>,
    active: Arc<Dispatches>,
}
pub struct Writer {
    sender: mpsc::Sender<Result<Frame, Status>>,
}
impl Writer {
    fn write(&self, frame: Frame) -> bool {
        self.sender.blocking_send(Ok(frame)).is_ok()
    }
    fn cancelled(&self) -> bool {
        self.sender.is_closed()
    }
}
impl Handler {
    fn stream(
        &self,
        method: Method,
        request: Frame,
        trace: Trace,
    ) -> ReceiverStream<Result<Frame, Status>> {
        let (sender, receiver) = mpsc::channel(2);
        let service = self.service.clone();
        let active = self.active.enter();
        runtime().spawn_blocking(move || {
            let _active = active;
            let writer = Writer { sender };
            let mut completion = Completion::new(method, String::new(), trace.id.clone(), true);
            let error = service.dispatch(method, request, &trace, &writer);
            completion.report.error = error;
            if error != 0 {
                let _ = writer.sender.blocking_send(Err(error_status(error)));
            }
        });
        ReceiverStream::new(receiver)
    }
}
fn request_trace(metadata: &tonic::metadata::MetadataMap) -> Trace {
    Trace {
        id: metadata
            .get("custom-header")
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.split_once(','))
            .map(|(_, id)| id.to_owned())
            .unwrap_or_default(),
    }
}
#[tonic::async_trait]
impl StandbyService for Handler {
    type fetch_ls_viewStream = Pin<Box<dyn Stream<Item = Result<FetchLsViewRes, Status>> + Send>>;
    async fn fetch_ls_view(
        &self,
        request: Request<FetchLsViewReq>,
    ) -> Result<Response<Self::fetch_ls_viewStream>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let stream = self.stream(
            Method::fetch_ls_view,
            Frame {
                buf: request.buf,
                size: request.size,
                ..Frame::empty()
            },
            trace,
        );
        Ok(Response::new(Box::pin(stream.map(|item| {
            item.map(|frame| FetchLsViewRes {
                entry_type: frame.entry_type,
                buf: frame.buf,
                size: frame.size,
            })
        }))))
    }
    type fetch_tablet_infoStream =
        Pin<Box<dyn Stream<Item = Result<FetchTabletInfoRes, Status>> + Send>>;
    async fn fetch_tablet_info(
        &self,
        request: Request<FetchTabletInfoReq>,
    ) -> Result<Response<Self::fetch_tablet_infoStream>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let stream = self.stream(
            Method::fetch_tablet_info,
            Frame {
                buf: request.buf,
                size: request.size,
                ..Frame::empty()
            },
            trace,
        );
        Ok(Response::new(Box::pin(stream.map(|item| {
            item.map(|frame| FetchTabletInfoRes {
                buf: frame.buf,
                size: frame.size,
            })
        }))))
    }
    type fetch_tablet_sstable_infoStream =
        Pin<Box<dyn Stream<Item = Result<FetchTabletSsTableInfoRes, Status>> + Send>>;
    async fn fetch_tablet_sstable_info(
        &self,
        request: Request<FetchTabletSsTableInfoReq>,
    ) -> Result<Response<Self::fetch_tablet_sstable_infoStream>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let stream = self.stream(
            Method::fetch_tablet_sstable_info,
            Frame {
                buf: request.buf,
                size: request.size,
                ..Frame::empty()
            },
            trace,
        );
        Ok(Response::new(Box::pin(stream.map(|item| {
            item.map(|frame| FetchTabletSsTableInfoRes {
                buf: frame.buf,
                size: frame.size,
            })
        }))))
    }
    type fetch_sstable_macro_infoStream =
        Pin<Box<dyn Stream<Item = Result<FetchSsTableMacroInfoRes, Status>> + Send>>;
    async fn fetch_sstable_macro_info(
        &self,
        request: Request<FetchSsTableMacroInfoReq>,
    ) -> Result<Response<Self::fetch_sstable_macro_infoStream>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let stream = self.stream(
            Method::fetch_sstable_macro_info,
            Frame {
                buf: request.buf,
                size: request.size,
                ..Frame::empty()
            },
            trace,
        );
        Ok(Response::new(Box::pin(stream.map(|item| {
            item.map(|frame| FetchSsTableMacroInfoRes {
                buf: frame.buf,
                size: frame.size,
            })
        }))))
    }
    type fetch_macro_blockStream =
        Pin<Box<dyn Stream<Item = Result<FetchMacroBlockRes, Status>> + Send>>;
    async fn fetch_macro_block(
        &self,
        request: Request<FetchMacroBlockReq>,
    ) -> Result<Response<Self::fetch_macro_blockStream>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let stream = self.stream(
            Method::fetch_macro_block,
            Frame {
                buf: request.buf,
                size: request.size,
                ..Frame::empty()
            },
            trace,
        );
        Ok(Response::new(Box::pin(stream.map(|item| {
            item.map(|frame| FetchMacroBlockRes {
                buf: frame.buf,
                size: frame.size,
            })
        }))))
    }
    async fn get_ls_view_tablet_count(
        &self,
        request: Request<GetLsViewTabletCountReq>,
    ) -> Result<Response<GetLsViewTabletCountRes>, Status> {
        let trace = request_trace(request.metadata());
        let mut stream = self.stream(Method::get_ls_view_tablet_count, Frame::empty(), trace);
        let frame = stream
            .next()
            .await
            .ok_or_else(|| Status::internal("missing response"))??;
        if let Some(result) = stream.next().await {
            result?;
        }
        Ok(Response::new(count_response(frame)?))
    }

    async fn check_restore_precondition(
        &self,
        request: Request<CheckRestorePreconditionReq>,
    ) -> Result<Response<CheckRestorePreconditionRes>, Status> {
        let trace = request_trace(request.metadata());
        let mut stream = self.stream(Method::check_restore_precondition, Frame::empty(), trace);
        let frame = stream
            .next()
            .await
            .ok_or_else(|| Status::internal("missing response"))??;
        if let Some(result) = stream.next().await {
            result?;
        }
        Ok(Response::new(precondition_response(frame)?))
    }

    async fn fetch_standby_palf_base_info(
        &self,
        request: Request<FetchStandbyPalfBaseInfoReq>,
    ) -> Result<Response<FetchStandbyPalfBaseInfoRes>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let mut stream = self.stream(
            Method::fetch_standby_palf_base_info,
            Frame {
                buf: request.buf,
                size: request.size,
                ..Frame::empty()
            },
            trace,
        );
        let frame = stream
            .next()
            .await
            .ok_or_else(|| Status::internal("missing response"))??;
        if let Some(result) = stream.next().await {
            result?;
        }
        Ok(Response::new(FetchStandbyPalfBaseInfoRes {
            buf: frame.buf,
            size: frame.size,
        }))
    }
    type fetch_logStream = Pin<Box<dyn Stream<Item = Result<FetchLogRes, Status>> + Send>>;
    async fn fetch_log(
        &self,
        request: Request<FetchLogReq>,
    ) -> Result<Response<Self::fetch_logStream>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let stream = self.stream(
            Method::fetch_log,
            Frame {
                start_lsn: request.start_lsn,
                max_bytes: request.max_bytes,
                ..Frame::empty()
            },
            trace,
        );
        Ok(Response::new(Box::pin(stream.map(|item| {
            item.map(|frame| FetchLogRes {
                buf: frame.buf,
                size: frame.size,
                source_lsn: frame.source_lsn,
                end_scn: frame.end_scn,
            })
        }))))
    }
    async fn get_promotion_boundary(
        &self,
        request: Request<GetPromotionBoundaryReq>,
    ) -> Result<Response<GetPromotionBoundaryRes>, Status> {
        let trace = request_trace(request.metadata());
        let request = request.into_inner();
        let mut stream = self.stream(
            Method::get_promotion_boundary,
            boundary_request_frame(request)?,
            trace,
        );
        let frame = stream
            .next()
            .await
            .ok_or_else(|| Status::internal("missing response"))??;
        if let Some(result) = stream.next().await {
            result?;
        }
        Ok(Response::new(boundary_response(frame)?))
    }
}
pub struct ServerHandle(rpc::RpcServer);
fn start_server(
    port: u16,
    tls: bool,
    service: cxx::SharedPtr<Service>,
) -> Result<Box<ServerHandle>, Error> {
    if service.is_null() {
        return Err("missing standby service".into());
    }
    let active = Arc::new(Dispatches::default());
    let service = StandbyServiceServer::new(Handler {
        service,
        active: active.clone(),
    })
    .max_decoding_message_size(MAX_MESSAGE)
    .max_encoding_message_size(MAX_MESSAGE);
    Ok(Box::new(ServerHandle(rpc::start_server(
        port, tls, service, active,
    )?)))
}
impl ServerHandle {
    fn stop(&mut self) {
        self.0.stop();
    }
    fn cert_expire_time(&self) -> i64 {
        self.0.cert_expire_time()
    }
}
pub struct ClientStream {
    receiver: Mutex<mpsc::Receiver<Result<Frame, Status>>>,
    task: AbortHandle,
    status: AtomicI32,
}
impl ClientStream {
    fn receive(&self) -> Received {
        match self.receiver.lock().unwrap().blocking_recv() {
            Some(Ok(frame)) => Received {
                frame,
                present: true,
                error: 0,
            },
            Some(Err(status)) => {
                let code = status_error(&status);
                self.status.store(code, Ordering::Release);
                Received {
                    frame: Frame::empty(),
                    present: false,
                    error: code,
                }
            }
            None => Received {
                frame: Frame::empty(),
                present: false,
                error: self.status.load(Ordering::Acquire),
            },
        }
    }
    fn cancel(&self) {
        if !self.task.is_finished() {
            let _ = self
                .status
                .compare_exchange(0, -4072, Ordering::AcqRel, Ordering::Acquire);
        }
        self.task.abort();
    }
}
impl Drop for ClientStream {
    fn drop(&mut self) {
        self.cancel();
    }
}
pub struct ClientHandle(rpc::Client);
fn create_client(address: &str, tls: bool) -> Result<Box<ClientHandle>, Error> {
    Ok(Box::new(ClientHandle(rpc::create_client(address, tls)?)))
}
impl ClientHandle {
    fn open_stream(
        &self,
        method: Method,
        request: Frame,
        trace: &str,
        timeout_us: u64,
    ) -> Box<ClientStream> {
        let timeout = Duration::from_micros(timeout_us.max(1));
        let channel = self.0.channel();
        let trace = trace.to_owned();
        let completion = Completion::new(method, self.0.address().to_owned(), trace.clone(), false);
        let (sender, receiver) = mpsc::channel(2);
        let task = runtime().spawn(async move {
            let mut completion = completion;
            let result = rpc::with_timeout(
                timeout,
                client_call(channel, method, request, &trace, timeout, &sender),
            )
            .await;
            completion.report.error = result.as_ref().err().map_or(0, status_error);
            if let Err(status) = result {
                let _ = sender.send(Err(status)).await;
            }
        });
        Box::new(ClientStream {
            receiver: Mutex::new(receiver),
            task: task.abort_handle(),
            status: AtomicI32::new(0),
        })
    }
}
async fn client_call(
    channel: Channel,
    method: Method,
    frame: Frame,
    trace: &str,
    timeout: Duration,
    sender: &mpsc::Sender<Result<Frame, Status>>,
) -> Result<(), Status> {
    let header: tonic::metadata::MetadataValue<_> = format!("1,{trace}")
        .parse()
        .map_err(|_| Status::invalid_argument("invalid trace metadata"))?;
    let mut client =
        StandbyServiceClient::with_interceptor(channel, move |mut request: Request<()>| {
            request
                .metadata_mut()
                .insert("custom-header", header.clone());
            Ok(request)
        })
        .max_decoding_message_size(MAX_MESSAGE)
        .max_encoding_message_size(MAX_MESSAGE);
    match method {
        Method::fetch_ls_view => {
            let mut request = Request::new(FetchLsViewReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let mut stream = client.fetch_ls_view(request).await?.into_inner();
            while let Some(response) = stream.message().await? {
                if sender
                    .send(Ok(Frame {
                        entry_type: response.entry_type,
                        buf: response.buf,
                        size: response.size,
                        ..Frame::empty()
                    }))
                    .await
                    .is_err()
                {
                    return Ok(());
                }
            }
        }
        Method::fetch_tablet_info => {
            let mut request = Request::new(FetchTabletInfoReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let mut stream = client.fetch_tablet_info(request).await?.into_inner();
            while let Some(response) = stream.message().await? {
                if sender
                    .send(Ok(Frame {
                        buf: response.buf,
                        size: response.size,
                        ..Frame::empty()
                    }))
                    .await
                    .is_err()
                {
                    return Ok(());
                }
            }
        }
        Method::fetch_tablet_sstable_info => {
            let mut request = Request::new(FetchTabletSsTableInfoReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let mut stream = client
                .fetch_tablet_sstable_info(request)
                .await?
                .into_inner();
            while let Some(response) = stream.message().await? {
                if sender
                    .send(Ok(Frame {
                        buf: response.buf,
                        size: response.size,
                        ..Frame::empty()
                    }))
                    .await
                    .is_err()
                {
                    return Ok(());
                }
            }
        }
        Method::fetch_sstable_macro_info => {
            let mut request = Request::new(FetchSsTableMacroInfoReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let mut stream = client.fetch_sstable_macro_info(request).await?.into_inner();
            while let Some(response) = stream.message().await? {
                if sender
                    .send(Ok(Frame {
                        buf: response.buf,
                        size: response.size,
                        ..Frame::empty()
                    }))
                    .await
                    .is_err()
                {
                    return Ok(());
                }
            }
        }
        Method::fetch_macro_block => {
            let mut request = Request::new(FetchMacroBlockReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let mut stream = client.fetch_macro_block(request).await?.into_inner();
            while let Some(response) = stream.message().await? {
                if sender
                    .send(Ok(Frame {
                        buf: response.buf,
                        size: response.size,
                        ..Frame::empty()
                    }))
                    .await
                    .is_err()
                {
                    return Ok(());
                }
            }
        }
        Method::get_ls_view_tablet_count => {
            let mut request = Request::new(GetLsViewTabletCountReq {});
            request.set_timeout(timeout);
            let response = client.get_ls_view_tablet_count(request).await?.into_inner();
            if sender
                .send(Ok(count_frame(response.tablet_count)?))
                .await
                .is_err()
            {
                return Ok(());
            }
        }
        Method::check_restore_precondition => {
            let mut request = Request::new(CheckRestorePreconditionReq {});
            request.set_timeout(timeout);
            let response = client
                .check_restore_precondition(request)
                .await?
                .into_inner();
            if sender
                .send(Ok(precondition_frame(response)?))
                .await
                .is_err()
            {
                return Ok(());
            }
        }
        Method::fetch_standby_palf_base_info => {
            let mut request = Request::new(FetchStandbyPalfBaseInfoReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let response = client
                .fetch_standby_palf_base_info(request)
                .await?
                .into_inner();
            if sender
                .send(Ok(Frame {
                    buf: response.buf,
                    size: response.size,
                    ..Frame::empty()
                }))
                .await
                .is_err()
            {
                return Ok(());
            }
        }
        Method::fetch_log => {
            let mut request = Request::new(FetchLogReq {
                start_lsn: frame.start_lsn,
                max_bytes: frame.max_bytes,
            });
            request.set_timeout(timeout);
            let mut stream = client.fetch_log(request).await?.into_inner();
            while let Some(response) = stream.message().await? {
                if sender
                    .send(Ok(Frame {
                        buf: response.buf,
                        size: response.size,
                        source_lsn: response.source_lsn,
                        end_scn: response.end_scn,
                        ..Frame::empty()
                    }))
                    .await
                    .is_err()
                {
                    return Ok(());
                }
            }
        }
        Method::get_promotion_boundary => {
            let mut request = Request::new(boundary_request(frame)?);
            request.set_timeout(timeout);
            let response = client.get_promotion_boundary(request).await?.into_inner();
            if sender.send(Ok(boundary_frame(response)?)).await.is_err() {
                return Ok(());
            }
        }
        _ => return Err(Status::unimplemented("unknown RPC method")),
    }
    Ok(())
}
