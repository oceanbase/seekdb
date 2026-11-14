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

use super::*;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};

pub(super) struct Directory(pub(super) PathBuf);
impl Directory {
    pub(super) fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = std::env::temp_dir().join(format!(
            "typed-io-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir(&path).unwrap();
        Self(path)
    }
    fn bytes(&self) -> &[u8] {
        self.0.as_os_str().as_encoded_bytes()
    }
}
impl Drop for Directory {
    fn drop(&mut self) {
        std::fs::remove_dir_all(&self.0).unwrap();
    }
}
pub(super) fn service() -> Box<ServiceHandle> {
    let service = new_service_handle();
    assert_eq!(
        service.prepare_service(&ffi::RuntimeConfig {
            server_memory_bytes: 1024 * 1024,
            io_buffer_bytes: 0,
            request_limit: 32,
            callback_threads: 1,
            statistics_lanes: 2,
        }),
        0
    );
    assert_eq!(service.activate_service(), 0);
    service
}
fn execution() -> ffi::ExecutionOptions {
    ffi::ExecutionOptions {
        worker_threads: 1,
        max_depth: 4,
    }
}
fn options(mode: ffi::RequestMode, offset: u64, length: usize) -> ffi::RequestOptions {
    ffi::RequestOptions {
        mode,
        worker_required: true,
        offset,
        length,
        timeout_us: -1,
        statistics_lane: 0,
        worker_fault: 0,
    }
}
fn block_file(service: &ServiceHandle, directory: &Directory) -> (Box<BlockOwner>, u64) {
    let owner = new_block_owner();
    assert_eq!(
        owner.prepare_blocks(service, directory.bytes(), &execution()),
        0
    );
    let path = directory.0.join("blocks");
    assert!(
        (owner
            .open_blocks(
                path.as_os_str().as_encoded_bytes(),
                &ffi::BlockOptions {
                    block_size: 8192,
                    initial_size: 32768,
                    reserved_bytes: 0,
                    punch_holes: false,
                }
            )
            .status
            .code)
            == ffi::BlockCode::Ok
    );
    assert!((owner.finish_marking().code) == ffi::BlockCode::Ok);
    let mut indices = Vec::new();
    assert!((owner.allocate_blocks(1, &mut indices).code) == ffi::BlockCode::Ok);
    (owner, indices[0])
}

#[test]
fn data_owner_preserves_logical_read_window_and_rejects_out_of_block_ranges() {
    let directory = Directory::new();
    let service = service();
    let (owner, index) = block_file(&service, &directory);
    let bytes: Vec<u8> = (0..8192).map(|i| (i % 251) as u8).collect();
    let write = owner.submit_block_request(
        index,
        &options(ffi::RequestMode::Write, 0, bytes.len()),
        &bytes,
        None,
    );
    assert_eq!(write.submission_error(), 0);
    assert_eq!(write.wait_micros(5_000_000), 0);
    let read =
        owner.submit_block_request(index, &options(ffi::RequestMode::Read, 17, 4096), &[], None);
    assert_eq!(read.wait_micros(5_000_000), 0);
    assert_eq!(read.data(), &bytes[17..4113]);
    let outside =
        owner.submit_block_request(index, &options(ffi::RequestMode::Read, 8191, 2), &[], None);
    assert_ne!(outside.submission_error(), 0);
    assert_eq!(owner.close_blocks(), 0);
    assert_eq!(read.data(), &bytes[17..4113]);
    let closed =
        owner.submit_block_request(index, &options(ffi::RequestMode::Read, 0, 4096), &[], None);
    assert_ne!(closed.submission_error(), 0);
    assert_eq!(owner.close_blocks(), 0);
    assert_eq!(service.shutdown_service(), 0);
}

#[test]
fn data_owner_can_reopen_after_close_without_old_result_aliasing_new_file() {
    let directory = Directory::new();
    let service = service();
    let (owner, index) = block_file(&service, &directory);
    let input = vec![0x5a; 4096];
    let write = owner.submit_block_request(
        index,
        &options(ffi::RequestMode::Write, 0, 4096),
        &input,
        None,
    );
    assert_eq!(write.wait_micros(5_000_000), 0);
    let read =
        owner.submit_block_request(index, &options(ffi::RequestMode::Read, 0, 4096), &[], None);
    assert_eq!(read.wait_micros(5_000_000), 0);
    assert_eq!(owner.close_blocks(), 0);
    assert_eq!(
        owner.prepare_blocks(&service, directory.bytes(), &execution()),
        0
    );
    let path = directory.0.join("second");
    assert!(
        (owner
            .open_blocks(
                path.as_os_str().as_encoded_bytes(),
                &ffi::BlockOptions {
                    block_size: 8192,
                    initial_size: 32768,
                    reserved_bytes: 0,
                    punch_holes: false,
                }
            )
            .status
            .code)
            == ffi::BlockCode::Ok
    );
    assert_eq!(read.data(), input);
    assert_eq!(owner.close_blocks(), 0);
    assert_eq!(service.shutdown_service(), 0);
}

pub(super) fn named_owner(
    service: &ServiceHandle,
    directory: &Directory,
) -> (Box<NamedOwnerHandle>, Box<file_io::bridge::DirectoryHandle>) {
    let mut owner = new_named_owner();
    assert_eq!(
        owner.begin_named_inventory(service, directory.bytes(), 8192, &execution()),
        0
    );
    assert_eq!(owner.finish_named_inventory(), 0);
    let mut parent = file_io::bridge::new_directory_handle();
    assert_eq!(parent.open_directory(directory.bytes()).error, 0);
    assert_eq!(
        owner.create_and_publish_named_file(&parent, b"temporary", b"log", 8192, false),
        0
    );
    (owner, parent)
}

#[test]
fn named_owned_file_retains_deleted_file_until_pending_request_retires() {
    let directory = Directory::new();
    let service = service();
    let (owner, parent) = named_owner(&service, &directory);
    let mut file = new_named_file();
    let path = directory.0.join("log");
    assert_eq!(
        owner.open_named_file(path.as_os_str().as_encoded_bytes(), false, &mut file),
        0
    );
    let mut write_options = options(ffi::RequestMode::Write, 0, 4096);
    write_options.worker_fault = -100;
    let write = file.submit_named_request(&write_options, &[0x6b; 4096], None);
    assert_eq!(write.submission_error(), 0);
    drop(file);
    assert_eq!(owner.remove_named_file(&parent, b"log", true), 0);
    assert_eq!(owner.named_stats().charged, 8192);
    assert_eq!(write.wait_micros(5_000_000), 0);
    assert_eq!(owner.close_named_owner(), 0);
    drop(write);
    assert!(owner.named_owner_replaceable());
    assert_eq!(service.shutdown_service(), 0);
}

#[test]
fn named_file_rejects_closed_owner_and_owner_replacement_waits_for_file_lease() {
    let directory = Directory::new();
    let service = service();
    let (mut owner, _parent) = named_owner(&service, &directory);
    let mut file = new_named_file();
    let path = directory.0.join("log");
    assert_eq!(
        owner.open_named_file(path.as_os_str().as_encoded_bytes(), true, &mut file),
        0
    );
    assert_eq!(owner.close_named_owner(), 0);
    assert!(!owner.named_owner_replaceable());
    let request = file.submit_named_request(&options(ffi::RequestMode::Read, 0, 4096), &[], None);
    assert_ne!(request.submission_error(), 0);
    assert_eq!(
        owner.begin_named_inventory(&service, directory.bytes(), 8192, &execution()),
        libc::EAGAIN
    );
    drop(file);
    assert!(owner.named_owner_replaceable());
    assert_eq!(
        owner.begin_named_inventory(&service, directory.bytes(), 8192, &execution()),
        0
    );
    assert_eq!(owner.close_named_owner(), 0);
    assert_eq!(service.shutdown_service(), 0);
}

#[test]
fn data_close_drains_callback_without_holding_file_admission_lock() {
    use std::sync::{mpsc, Arc, Mutex};
    use std::time::{Duration, Instant};
    struct Reenter {
        owner: Arc<BlockOwner>,
        index: u64,
        entered: mpsc::Sender<Vec<u8>>,
        proceed: Mutex<mpsc::Receiver<()>>,
    }
    impl crate::service::Consumer for Reenter {
        fn process(&mut self, bytes: &[u8]) -> Result<i32, ()> {
            self.entered.send(bytes.to_vec()).unwrap();
            self.proceed
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(10))
                .map_err(|_| ())?;
            let rejected = self.owner.submit_block_request(
                self.index,
                &options(ffi::RequestMode::Read, 0, 4096),
                &[],
                None,
            );
            assert_ne!(rejected.submission_error(), 0);
            Ok(0)
        }
    }
    let directory = Directory::new();
    let service = service();
    let (owner, index) = block_file(&service, &directory);
    let owner = Arc::<BlockOwner>::from(owner);
    let mut contents = vec![0xa1; 8192];
    contents[17..4113].fill(0x52);
    contents[4113..].fill(0xc3);
    let write = owner.submit_block_request(
        index,
        &options(ffi::RequestMode::Write, 0, contents.len()),
        &contents,
        None,
    );
    assert_eq!(write.wait_micros(5_000_000), 0);
    drop(write);
    let (entered, began) = mpsc::channel();
    let (release, proceed) = mpsc::channel();
    let request = owner.submit_block_request(
        index,
        &options(ffi::RequestMode::Read, 17, 4096),
        &[],
        Some(Box::new(Reenter {
            owner: Arc::clone(&owner),
            index,
            entered,
            proceed: Mutex::new(proceed),
        })),
    );
    assert_eq!(
        began.recv_timeout(Duration::from_secs(5)).unwrap(),
        contents[17..4113]
    );
    let closing = Arc::clone(&owner);
    let (finished, done) = mpsc::channel();
    let closer = std::thread::spawn(move || finished.send(closing.close_blocks()).unwrap());
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        let mut stats = ffi::BlockStats {
            block_size: 0,
            capacity: 0,
            total_blocks: 0,
            free_blocks: 0,
            pending_release_blocks: 0,
            reclamation_failures: 0,
        };
        if owner.block_stats(&mut stats).code == ffi::BlockCode::NotOpen {
            break;
        }
        assert!(Instant::now() < deadline);
        std::thread::yield_now();
    }
    assert!(done.try_recv().is_err());
    release.send(()).unwrap();
    assert_eq!(done.recv_timeout(Duration::from_secs(5)).unwrap(), 0);
    closer.join().unwrap();
    assert_eq!(request.wait_micros(5_000_000), 0);
    assert_eq!(service.shutdown_service(), 0);
}
