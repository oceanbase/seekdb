// Copyright (c) 2025 OceanBase.
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

use crate::*;

const CONN_SHARD_COUNT: usize = 64;
type ConnMappings = RwLock<HashMap<usize, Weak<Conn>>>;

#[repr(align(64))]
pub(crate) struct ConnShard(ConnMappings);

pub(crate) static CONNS: LazyLock<[ConnShard; CONN_SHARD_COUNT]> =
    LazyLock::new(|| std::array::from_fn(|_| ConnShard(RwLock::new(HashMap::new()))));

fn conn_shard(key: usize) -> &'static ConnMappings {
    let word = key >> 4;
    let mixed = word ^ (word >> 7) ^ (word >> 17);
    &CONNS[mixed & (CONN_SHARD_COUNT - 1)].0
}

pub(crate) fn insert_conn_mapping(conn: &Arc<Conn>) {
    let key = conn.sess() as usize;
    conn_shard(key)
        .write()
        .unwrap()
        .insert(key, Arc::downgrade(conn));
}

pub(crate) fn conn_of_live_or_retired(sess: *mut c_void) -> Option<Arc<Conn>> {
    conn_shard(sess as usize)
        .read()
        .unwrap()
        .get(&(sess as usize))
        .and_then(Weak::upgrade)
}

pub(crate) fn conn_of(sess: *mut c_void) -> Option<Arc<Conn>> {
    conn_of_live_or_retired(sess).filter(|conn| !conn.close_notified.load(Ordering::Acquire))
}

#[repr(C)]
pub struct NioConnectionHandle {
    pub(crate) conn: Arc<Conn>,
}

/// # Safety
/// `sess` is null or points at a live, fully constructed C++ session registered
/// with this SQL-NIO instance.
#[no_mangle]
pub unsafe extern "C" fn nio_connection_handle_acquire(
    sess: *mut c_void,
) -> *mut NioConnectionHandle {
    match conn_of(sess) {
        Some(conn) => Box::into_raw(Box::new(NioConnectionHandle { conn })),
        None => std::ptr::null_mut(),
    }
}

/// # Safety
/// `connection_handle` is null or is an unreleased value returned by
/// `nio_connection_handle_acquire`.
#[no_mangle]
pub unsafe extern "C" fn nio_connection_handle_release(
    connection_handle: *mut NioConnectionHandle,
) {
    if !connection_handle.is_null() {
        drop(unsafe { Box::from_raw(connection_handle) });
    }
}

pub(crate) fn remove_conn_mapping(conn: &Arc<Conn>) {
    let key = conn.sess() as usize;
    let mut conns = conn_shard(key).write().unwrap();
    if conns
        .get(&key)
        .and_then(Weak::upgrade)
        .is_some_and(|mapped| Arc::ptr_eq(&mapped, conn))
    {
        conns.remove(&key);
    }
}
