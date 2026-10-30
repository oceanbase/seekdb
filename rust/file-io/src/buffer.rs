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

use std::alloc::{alloc_zeroed, dealloc, Layout};
use std::io;
use std::ptr::NonNull;

/// Exclusively owns zero-initialized bytes with a caller-selected alignment.
#[derive(Debug)]
pub struct AlignedBuffer {
    ptr: NonNull<u8>,
    layout: Layout,
    len: usize,
}

impl AlignedBuffer {
    pub fn zeroed(len: usize, alignment: usize) -> io::Result<Self> {
        // Allocate one byte for an empty buffer so its pointer has the same alignment contract.
        let layout = Layout::from_size_align(len.max(1), alignment).map_err(|_| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid buffer size or alignment",
            )
        })?;
        // SAFETY: layout has nonzero size and valid alignment.
        let ptr = NonNull::new(unsafe { alloc_zeroed(layout) })
            .ok_or_else(|| io::Error::from(io::ErrorKind::OutOfMemory))?;
        Ok(Self { ptr, layout, len })
    }

    pub fn as_slice(&self) -> &[u8] {
        // SAFETY: this owner keeps len initialized bytes alive for the borrow.
        unsafe { std::slice::from_raw_parts(self.ptr.as_ptr(), self.len) }
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        // SAFETY: the exclusive owner borrow excludes all other byte access.
        unsafe { std::slice::from_raw_parts_mut(self.ptr.as_ptr(), self.len) }
    }
}

// SAFETY: ownership moves with the allocation; no thread-local allocator or borrowed state.
unsafe impl Send for AlignedBuffer {}
// SAFETY: shared access only exposes immutable bytes; mutation requires an exclusive borrow.
unsafe impl Sync for AlignedBuffer {}

impl Drop for AlignedBuffer {
    fn drop(&mut self) {
        // SAFETY: ptr came from alloc_zeroed with this layout and is freed exactly once.
        unsafe { dealloc(self.ptr.as_ptr(), self.layout) };
    }
}
