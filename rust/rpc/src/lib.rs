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
#[allow(non_camel_case_types)]
mod protocol;
use bridge::ffi::{CallReport, Frame, Method, Received, Service, Trace};
use protocol::standby_service_client::StandbyServiceClient;
use protocol::standby_service_server::{StandbyService, StandbyServiceServer};
use protocol::*;
use std::{
    collections::VecDeque,
    pin::Pin,
    sync::{
        atomic::{AtomicI32, Ordering},
        Arc, Condvar, Mutex, OnceLock,
    },
    time::{Duration, Instant},
};
use tokio::{
    runtime::Runtime,
    sync::{mpsc, oneshot},
    task::{AbortHandle, JoinHandle},
};
use tokio_stream::{
    wrappers::{ReceiverStream, TcpListenerStream},
    Stream, StreamExt,
};
use tonic::{
    transport::{
        Certificate, Channel, ClientTlsConfig, Endpoint, Identity, Server, ServerTlsConfig,
    },
    Request, Response, Status,
};
const MAX_MESSAGE: usize = 512 * 1024 * 1024;
const RPC_ERROR: i32 = -4121;
type Error = Box<dyn std::error::Error + Send + Sync>;
fn runtime() -> &'static Runtime {
    static RUNTIME: OnceLock<Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()
            .expect("RPC runtime")
    })
}
impl Frame {
    fn empty() -> Self {
        Self {
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
#[derive(Default)]
struct Dispatches {
    count: Mutex<usize>,
    idle: Condvar,
}
struct ActiveCall(Arc<Dispatches>);
impl Drop for ActiveCall {
    fn drop(&mut self) {
        *self.0.count.lock().unwrap() -= 1;
        self.0.idle.notify_all();
    }
}
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
        *self.active.count.lock().unwrap() += 1;
        let active = ActiveCall(self.active.clone());
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
#[tonic::async_trait]
impl StandbyService for Handler {
    type fetch_ls_viewStream = Pin<Box<dyn Stream<Item = Result<FetchLsViewRes, Status>> + Send>>;
    async fn fetch_ls_view(
        &self,
        request: Request<FetchLsViewReq>,
    ) -> Result<Response<Self::fetch_ls_viewStream>, Status> {
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
        let request = request.into_inner();
        let mut stream = self.stream(
            Method::get_ls_view_tablet_count,
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
        Ok(Response::new(GetLsViewTabletCountRes {
            buf: frame.buf,
            size: frame.size,
        }))
    }
    async fn check_restore_precondition(
        &self,
        request: Request<CheckRestorePreconditionReq>,
    ) -> Result<Response<CheckRestorePreconditionRes>, Status> {
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
        let request = request.into_inner();
        let mut stream = self.stream(
            Method::check_restore_precondition,
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
        Ok(Response::new(CheckRestorePreconditionRes {
            buf: frame.buf,
            size: frame.size,
        }))
    }
    async fn fetch_standby_palf_base_info(
        &self,
        request: Request<FetchStandbyPalfBaseInfoReq>,
    ) -> Result<Response<FetchStandbyPalfBaseInfoRes>, Status> {
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
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
        let trace = Trace {
            id: request
                .metadata()
                .get("custom-header")
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.split_once(','))
                .map(|(_, id)| id.to_owned())
                .unwrap_or_default(),
        };
        let request = request.into_inner();
        let mut stream = self.stream(
            Method::get_promotion_boundary,
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
        Ok(Response::new(GetPromotionBoundaryRes {
            buf: frame.buf,
            size: frame.size,
        }))
    }
}
struct Wallet {
    ca: Certificate,
    identity: Identity,
    expire_time_us: i64,
}
impl Wallet {
    fn load() -> Result<Self, Error> {
        use rustls_pki_types::pem::PemObject;
        let ca = std::fs::read("wallet/ca.pem")?;
        let cert = std::fs::read("wallet/cert.pem")?;
        let key = std::fs::read("wallet/key.pem")?;
        let der = rustls_pki_types::CertificateDer::from_pem_slice(&cert)?;
        let (_, parsed) = x509_parser::parse_x509_certificate(der.as_ref())?;
        let expire_time_us = parsed
            .validity()
            .not_after
            .timestamp()
            .checked_mul(1_000_000)
            .ok_or("RPC certificate expiry overflows microseconds")?;
        Ok(Self {
            ca: Certificate::from_pem(ca),
            identity: Identity::from_pem(cert, key),
            expire_time_us,
        })
    }
}
pub struct ServerHandle {
    cert_expire_time_us: i64,
    active: Arc<Dispatches>,
    stop: Option<oneshot::Sender<()>>,
    task: Option<JoinHandle<Result<(), tonic::transport::Error>>>,
}
fn start_server(
    port: u16,
    tls: bool,
    service: cxx::SharedPtr<Service>,
) -> Result<Box<ServerHandle>, Error> {
    if service.is_null() {
        return Err("missing RPC service".into());
    }
    let mut builder = Server::builder();
    let cert_expire_time_us = if tls {
        let wallet = Wallet::load()?;
        builder = builder.tls_config(
            ServerTlsConfig::new()
                .identity(wallet.identity)
                .client_ca_root(wallet.ca),
        )?;
        wallet.expire_time_us
    } else {
        0
    };
    let listener = std::net::TcpListener::bind(("0.0.0.0", port))?;
    listener.set_nonblocking(true)?;
    let _guard = runtime().enter();
    let listener = tokio::net::TcpListener::from_std(listener)?;
    let (stop, stopped) = oneshot::channel();
    let active = Arc::new(Dispatches::default());
    let service = StandbyServiceServer::new(Handler {
        service,
        active: active.clone(),
    })
    .max_decoding_message_size(MAX_MESSAGE)
    .max_encoding_message_size(MAX_MESSAGE);
    let task = runtime().spawn(builder.add_service(service).serve_with_incoming_shutdown(
        TcpListenerStream::new(listener),
        async {
            let _ = stopped.await;
        },
    ));
    Ok(Box::new(ServerHandle {
        cert_expire_time_us,
        active,
        stop: Some(stop),
        task: Some(task),
    }))
}
impl ServerHandle {
    fn cert_expire_time(&self) -> i64 {
        self.cert_expire_time_us
    }
    fn stop(&mut self) {
        if let Some(stop) = self.stop.take() {
            let _ = stop.send(());
        }
        if let Some(task) = self.task.take() {
            let _ = runtime().block_on(task);
        }
        // A disconnected stream can finish before its C++ producer exits.
        // Wait before the host and role state referenced by that producer die.
        let mut count = self.active.count.lock().unwrap();
        while *count != 0 {
            count = self.active.idle.wait(count).unwrap();
        }
    }
}
impl Drop for ServerHandle {
    fn drop(&mut self) {
        self.stop();
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
pub struct ClientHandle {
    channel: Channel,
    address: String,
}
struct CachedChannel {
    address: String,
    tls: bool,
    channel: Channel,
    used: Instant,
}
fn create_client(address: &str, tls: bool) -> Result<Box<ClientHandle>, Error> {
    // Log polling creates short-lived C++ clients. Bound shared connections by
    // target and security mode rather than retaining every historical source.
    static CHANNELS: OnceLock<Mutex<VecDeque<CachedChannel>>> = OnceLock::new();
    let mut channels = CHANNELS.get_or_init(Default::default).lock().unwrap();
    channels.retain(|entry| entry.used.elapsed() < Duration::from_secs(60));
    let channel = if let Some(index) = channels
        .iter()
        .position(|entry| entry.address == address && entry.tls == tls)
    {
        let mut entry = channels.remove(index).unwrap();
        entry.used = Instant::now();
        let channel = entry.channel.clone();
        channels.push_back(entry);
        channel
    } else {
        let scheme = if tls { "https" } else { "http" };
        let mut endpoint = Endpoint::from_shared(format!("{scheme}://{address}"))?
            .connect_timeout(Duration::from_secs(9));
        if tls {
            let wallet = Wallet::load()?;
            endpoint = endpoint.tls_config(
                ClientTlsConfig::new()
                    .ca_certificate(wallet.ca)
                    .identity(wallet.identity),
            )?;
        }
        let _guard = runtime().enter();
        let channel = endpoint.connect_lazy();
        if channels.len() == 16 {
            channels.pop_front();
        }
        channels.push_back(CachedChannel {
            address: address.to_owned(),
            tls,
            channel: channel.clone(),
            used: Instant::now(),
        });
        channel
    };
    Ok(Box::new(ClientHandle {
        channel,
        address: address.to_owned(),
    }))
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
        let channel = self.channel.clone();
        let trace = trace.to_owned();
        let completion = Completion::new(method, self.address.clone(), trace.clone(), false);
        let (sender, receiver) = mpsc::channel(2);
        let task = runtime().spawn(async move {
            let mut completion = completion;
            let result = tokio::time::timeout(
                timeout,
                client_call(channel, method, request, &trace, timeout, &sender),
            )
            .await
            .unwrap_or_else(|_| Err(Status::deadline_exceeded("RPC deadline elapsed")));
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
            let mut request = Request::new(GetLsViewTabletCountReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let response = client.get_ls_view_tablet_count(request).await?.into_inner();
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
        Method::check_restore_precondition => {
            let mut request = Request::new(CheckRestorePreconditionReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let response = client
                .check_restore_precondition(request)
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
            let mut request = Request::new(GetPromotionBoundaryReq {
                buf: frame.buf,
                size: frame.size,
            });
            request.set_timeout(timeout);
            let response = client.get_promotion_boundary(request).await?.into_inner();
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
        _ => return Err(Status::unimplemented("unknown RPC method")),
    }
    Ok(())
}

#[cfg(test)]
mod tests;
