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

fn runtime() -> ffi::RuntimeConfig {
    ffi::RuntimeConfig {
        server_memory_bytes: 1024 * 1024,
        io_buffer_bytes: 0,
        request_limit: 10000,
        callback_threads: 0,
        statistics_lanes: 4,
    }
}

#[test]
fn read_plan_expands_envelope_and_preserves_requested_window() {
    let plan = plan_request(4097, 4096, 4096, ffi::RequestMode::Read);
    assert_eq!(plan.error, 0);
    assert_eq!((plan.offset, plan.length), (4096, 8192));
    assert_eq!((plan.window_start, plan.window_length), (1, 4096));
    let plan = plan_request(4096, 4096, 4096, ffi::RequestMode::Write);
    assert_eq!(plan.error, 0);
    assert_eq!(
        (plan.offset, plan.length, plan.window_start),
        (4096, 4096, 0)
    );
}

#[test]
fn request_plan_rejects_malformed_or_overflowing_geometry() {
    for (offset, length, alignment, mode) in [
        (1, 4096, 4096, ffi::RequestMode::Write),
        (4096, 4095, 4096, ffi::RequestMode::Write),
        (0, 0, 4096, ffi::RequestMode::Read),
        (0, 4096, 0, ffi::RequestMode::Read),
        (0, 4096, 3, ffi::RequestMode::Read),
        (u64::MAX, 1, 4096, ffi::RequestMode::Read),
        (u64::MAX - 1, 1, 4096, ffi::RequestMode::Read),
        (0, usize::MAX, 1, ffi::RequestMode::Read),
        ((-8192_i64) as u64, 4096, 4096, ffi::RequestMode::Read),
        ((-8192_i64) as u64, 4096, 4096, ffi::RequestMode::Write),
        (i64::MAX as u64 - 1, 1, 4096, ffi::RequestMode::Read),
        (i64::MAX as u64, 1, 1, ffi::RequestMode::Read),
    ] {
        let plan = plan_request(offset, length, alignment, mode);
        assert_eq!(plan.error, libc::EINVAL);
        assert_eq!((plan.offset, plan.length, plan.window_length), (0, 0, 0));
    }
}

#[test]
fn request_plan_accepts_the_last_complete_signed_offset_envelope() {
    let offset = i64::MAX as u64 - 8191;
    let plan = plan_request(offset, 4096, 4096, ffi::RequestMode::Read);
    assert_eq!(plan.error, 0);
    assert_eq!((plan.offset, plan.length), (offset, 4096));
}

#[test]
fn runtime_resource_update_preserves_admission_identity_and_failed_updates() {
    let service = new_service_handle();
    assert_eq!(service.prepare_service(&runtime()), 0);
    let before = service.service().unwrap().snapshot().unwrap();
    assert_eq!(before.config.admission.max_bytes, 1024 * 1024 / 5);
    assert_eq!(before.config.callback_threads, 16);
    let receipt = service.configure_resources(2 * 1024 * 1024, 65536, 2);
    assert_eq!(receipt, 0);
    let after = service.service().unwrap().snapshot().unwrap();
    assert_eq!(after.config.admission.max_records, 10000);
    assert_eq!(after.config.statistics_lanes, 4);
    assert_eq!(after.config.admission.max_bytes, 65536);
    assert_eq!(after.config.callback_threads, 2);
    assert_eq!(service.configure_resources(0, 65536, 2), libc::EINVAL);
    assert_eq!(
        service.service().unwrap().snapshot().unwrap().config,
        after.config
    );
    assert_eq!(service.shutdown_service(), 0);
    assert_eq!(service.shutdown_service(), 0);
    assert_eq!(service.activate_service(), libc::EBADF);
}

#[test]
fn attached_device_ids_do_not_reuse_closed_endpoints() {
    let service = new_service_handle();
    assert_eq!(service.prepare_service(&runtime()), 0);
    let first = service.create_endpoint(false, 0, 1).unwrap();
    let retained = first.clone();
    let first_id = first.id;
    assert_eq!(first.close_device(), 0);
    assert_eq!(first.wait_drained(), 0);
    drop(first);
    let second = service.create_endpoint(false, 0, 1).unwrap();
    assert_ne!(first_id, second.id);
    assert!(retained.is_drained());
    assert_eq!(service.shutdown_service(), 0);
}

#[test]
fn device_close_is_idempotent_after_service_stop_and_shutdown() {
    for shutdown_first in [false, true] {
        let service = new_service_handle();
        assert_eq!(service.prepare_service(&runtime()), 0);
        let device = service.create_endpoint(false, 1, 1).unwrap();
        assert_eq!(service.activate_service(), 0);
        if shutdown_first {
            assert_eq!(service.shutdown_service(), 0);
        } else {
            service.stop_service();
        }
        assert_eq!(device.close_device(), 0);
        assert_eq!(device.close_device(), 0);
        assert_eq!(device.wait_drained(), 0);
        assert_eq!(service.shutdown_service(), 0);
        assert_eq!(device.close_device(), 0);
    }
}

#[test]
fn device_close_succeeds_while_service_stops() {
    for _ in 0..32 {
        let service = Arc::new(*new_service_handle());
        assert_eq!(service.prepare_service(&runtime()), 0);
        let device = service.create_endpoint(false, 1, 1).unwrap();
        assert_eq!(service.activate_service(), 0);
        let barrier = Arc::new(std::sync::Barrier::new(2));
        let other = Arc::clone(&service);
        let start = Arc::clone(&barrier);
        let stopping = std::thread::spawn(move || {
            start.wait();
            other.stop_service();
        });
        barrier.wait();
        assert_eq!(device.close_device(), 0);
        stopping.join().unwrap();
        assert_eq!(device.wait_drained(), 0);
        assert_eq!(service.shutdown_service(), 0);
    }
}

#[test]
fn shared_handle_shutdown_is_idempotent() {
    let service = Arc::new(*new_service_handle());
    assert_eq!(service.prepare_service(&runtime()), 0);
    let other = Arc::clone(&service);
    let stopping = std::thread::spawn(move || other.shutdown_service());
    assert_eq!(service.shutdown_service(), 0);
    assert_eq!(stopping.join().unwrap(), 0);
    assert_eq!(service.activate_service(), libc::EBADF);
}

#[test]
fn shutdown_allows_callback_to_reenter_the_shared_handle() {
    struct Reenter {
        service: Arc<ServiceHandle>,
        entered: std::sync::mpsc::Sender<()>,
    }
    impl Consumer for Reenter {
        fn process(&mut self, _: &[u8]) -> Result<i32, ()> {
            self.entered.send(()).unwrap();
            let until = Instant::now() + Duration::from_secs(5);
            while self.service.activate_service() == 0 {
                assert!(Instant::now() < until);
                std::thread::yield_now();
            }
            assert!(self.service.service().unwrap().snapshot().is_ok());
            Ok(0)
        }
    }
    let handle = Arc::new(*new_service_handle());
    assert_eq!(handle.prepare_service(&runtime()), 0);
    assert_eq!(handle.activate_service(), 0);
    let device = handle.create_endpoint(false, 1, 1).unwrap();
    let file = Arc::new(std::fs::File::open("/dev/zero").unwrap());
    let spec = crate::RequestSpec::new(device.id, Target::File { file, offset: 0 }, 4096);
    let options = RequestOptions {
        device: spec.device,
        execution: Execution::WorkerRequired,
        length: spec.length,
        alignment: spec.alignment,
        window: spec.window,
        policy: spec.policy,
        tag: 0,
        statistics_lane: Some(0),
    };
    let (entered, started) = std::sync::mpsc::channel();
    let request = handle
        .service()
        .unwrap()
        .submit_request(
            options,
            spec.target,
            Direction::Read,
            &[],
            Some(Box::new(Reenter {
                service: Arc::clone(&handle),
                entered,
            })),
            None,
        )
        .unwrap();
    started.recv_timeout(Duration::from_secs(5)).unwrap();
    assert_eq!(handle.shutdown_service(), 0);
    assert!(request.is_finished());
    assert_eq!(handle.shutdown_service(), 0);
}

#[test]
fn drained_handle_can_prepare_a_new_service_without_reopening_old_endpoints() {
    let handle = new_service_handle();
    assert_eq!(handle.prepare_service(&runtime()), 0);
    let old_device = handle.create_endpoint(false, 1, 1).unwrap();
    let old_service = Arc::clone(&old_device.service);
    assert_eq!(handle.shutdown_service(), 0);
    assert_eq!(handle.prepare_service(&runtime()), 0);
    assert_eq!(handle.activate_service(), 0);
    assert!(!Arc::ptr_eq(&old_service, &handle.service().unwrap()));
    assert_eq!(
        old_service.activate().unwrap_err().kind(),
        io::ErrorKind::BrokenPipe
    );
    assert!(old_device.is_drained());
    assert_eq!(handle.shutdown_service(), 0);
}

#[test]
fn worker_reload_accepts_equivalent_counts_and_rejects_resizing() {
    let service = new_service_handle();
    assert_eq!(service.prepare_service(&runtime()), 0);
    assert_eq!(FileSession::default().validate_workers(1), libc::EBADF);
    let endpoint = service.create_endpoint(false, 1, 2).unwrap();
    assert_eq!(endpoint.validate_worker_threads(1), 0);
    assert_eq!(endpoint.validate_worker_threads(2), libc::ENOSYS);
    assert_eq!(endpoint.validate_worker_threads(0), libc::ENOSYS);
    let capped = service.create_endpoint(false, 0, 1).unwrap();
    assert_eq!(capped.validate_worker_threads(0), 0);
    assert_eq!(capped.validate_worker_threads(1), 0);
    assert_eq!(capped.validate_worker_threads(100), 0);
    assert_eq!(service.shutdown_service(), 0);
}

#[test]
fn file_session_can_close_and_prepare_again_with_a_fresh_endpoint() {
    let service = new_service_handle();
    assert_eq!(service.prepare_service(&runtime()), 0);
    assert_eq!(service.activate_service(), 0);
    let session = FileSession::default();
    let directory = std::env::temp_dir();
    let execution = ffi::ExecutionOptions { worker_threads: 1, max_depth: 2 };
    let mut previous = None;
    for _ in 0..8 {
        assert_eq!(session.prepare(&service, directory.as_os_str().as_encoded_bytes(), &execution), 0);
        let id = session.capture(|endpoint| Ok(endpoint.id)).unwrap();
        assert_ne!(Some(id), previous);
        assert_eq!(session.close(|| {}), 0);
        assert_eq!(session.close(|| {}), 0);
        assert!(session.capture(|_| Ok(())).is_err());
        previous = Some(id);
    }
    assert_eq!(service.shutdown_service(), 0);
}
