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

use std::{
    collections::VecDeque,
    sync::{Arc, Condvar, Mutex, OnceLock},
    time::{Duration, Instant},
};
use tokio::{runtime::Runtime, sync::oneshot, task::JoinHandle};
use tokio_stream::wrappers::TcpListenerStream;
use tonic::transport::{
    Certificate, Channel, ClientTlsConfig, Endpoint, Identity, Server, ServerTlsConfig,
};
pub const MAX_MESSAGE: usize = 512 * 1024 * 1024;
pub type Error = Box<dyn std::error::Error + Send + Sync>;
pub fn runtime() -> &'static Runtime {
    static RUNTIME: OnceLock<Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()
            .expect("RPC runtime")
    })
}
#[derive(Default)]
pub struct Dispatches {
    count: Mutex<usize>,
    idle: Condvar,
}
pub struct ActiveCall(Arc<Dispatches>);
impl Drop for ActiveCall {
    fn drop(&mut self) {
        *self.0.count.lock().unwrap() -= 1;
        self.0.idle.notify_all();
    }
}
impl Dispatches {
    pub fn enter(self: &Arc<Self>) -> ActiveCall {
        *self.count.lock().unwrap() += 1;
        ActiveCall(self.clone())
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
pub struct RpcServer {
    cert_expire_time_us: i64,
    active: Arc<Dispatches>,
    stop: Option<oneshot::Sender<()>>,
    task: Option<JoinHandle<Result<(), tonic::transport::Error>>>,
}
pub fn start_server<S>(
    port: u16,
    tls: bool,
    service: S,
    active: Arc<Dispatches>,
) -> Result<RpcServer, Error>
where
    S: tonic::codegen::Service<
            tonic::codegen::http::Request<tonic::body::Body>,
            Response = tonic::codegen::http::Response<tonic::body::Body>,
            Error = std::convert::Infallible,
        > + tonic::server::NamedService
        + Clone
        + Send
        + Sync
        + 'static,
    S::Future: Send + 'static,
{
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
    let task = runtime().spawn(builder.add_service(service).serve_with_incoming_shutdown(
        TcpListenerStream::new(listener),
        async {
            let _ = stopped.await;
        },
    ));
    Ok(RpcServer {
        cert_expire_time_us,
        active,
        stop: Some(stop),
        task: Some(task),
    })
}
impl RpcServer {
    pub fn cert_expire_time(&self) -> i64 {
        self.cert_expire_time_us
    }
    pub fn stop(&mut self) {
        if let Some(stop) = self.stop.take() {
            let _ = stop.send(());
        }
        if let Some(task) = self.task.take() {
            let _ = runtime().block_on(task);
        }
        // A disconnected stream can finish before its blocking producer exits.
        // Keep the state referenced by that producer alive until it returns.
        let mut count = self.active.count.lock().unwrap();
        while *count != 0 {
            count = self.active.idle.wait(count).unwrap();
        }
    }
}
impl Drop for RpcServer {
    fn drop(&mut self) {
        self.stop();
    }
}
pub struct Client {
    channel: Channel,
    address: String,
}
struct CachedChannel {
    address: String,
    tls: bool,
    channel: Channel,
    used: Instant,
}
pub fn create_client(address: &str, tls: bool) -> Result<Client, Error> {
    // Callers may create short-lived clients. Bound shared connections by
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
    Ok(Client {
        channel,
        address: address.to_owned(),
    })
}
impl Client {
    pub fn channel(&self) -> Channel {
        self.channel.clone()
    }
    pub fn address(&self) -> &str {
        &self.address
    }
}
pub async fn with_timeout<T>(
    timeout: Duration,
    operation: impl std::future::Future<Output = Result<T, tonic::Status>>,
) -> Result<T, tonic::Status> {
    tokio::time::timeout(timeout, operation)
        .await
        .unwrap_or_else(|_| Err(tonic::Status::deadline_exceeded("RPC deadline elapsed")))
}

#[cfg(test)]
mod tests;
