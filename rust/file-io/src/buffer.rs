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

use std::alloc::{alloc, alloc_zeroed, dealloc, Layout};
use std::io;
use std::mem::MaybeUninit;
use std::ptr::NonNull;

#[derive(Debug)]
struct RawAllocation {
    ptr: NonNull<u8>,
    layout: Layout,
}
impl Drop for RawAllocation {
    fn drop(&mut self) {
        // SAFETY: ptr was allocated with this layout and is freed exactly once.
        unsafe { dealloc(self.ptr.as_ptr(), self.layout) };
    }
}

/// Exclusively owns initialized bytes with a caller-selected alignment.
#[derive(Debug)]
pub struct AlignedBuffer {
    allocation: RawAllocation,
    len: usize,
    initialized: usize,
}
impl AlignedBuffer {
    fn ptr(&self) -> NonNull<u8> {
        self.allocation.ptr
    }
    fn allocate(layout: Layout) -> io::Result<RawAllocation> {
        // SAFETY: layout has nonzero size and valid alignment.
        let ptr = NonNull::new(unsafe { alloc(layout) })
            .ok_or_else(|| io::Error::from(io::ErrorKind::OutOfMemory))?;
        Ok(RawAllocation { ptr, layout })
    }
    pub fn zeroed(len: usize, alignment: usize) -> io::Result<Self> {
        let layout = Self::allocation_layout(len, alignment)?;
        // SAFETY: layout has nonzero size and valid alignment.
        let ptr = NonNull::new(unsafe { alloc_zeroed(layout) })
            .ok_or_else(|| io::Error::from(io::ErrorKind::OutOfMemory))?;
        Ok(Self {
            allocation: RawAllocation { ptr, layout },
            len,
            initialized: len,
        })
    }
    pub fn uninitialized(len: usize, alignment: usize) -> io::Result<Self> {
        let layout = Self::allocation_layout(len, alignment)?;
        Ok(Self {
            allocation: Self::allocate(layout)?,
            len,
            initialized: 0,
        })
    }
    pub fn from_slice(bytes: &[u8], alignment: usize) -> io::Result<Self> {
        let mut buffer = Self::uninitialized(bytes.len(), alignment)?;
        // SAFETY: the fresh exclusive allocation has room for bytes, and cannot overlap it.
        unsafe {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), buffer.ptr().as_ptr(), bytes.len())
        };
        buffer.initialized = bytes.len();
        Ok(buffer)
    }
    fn allocation_layout(len: usize, alignment: usize) -> io::Result<Layout> {
        Layout::from_size_align(len.max(1), alignment).map_err(|_| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid buffer size or alignment",
            )
        })
    }
    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn as_slice(&self) -> &[u8] {
        // SAFETY: this owner keeps the initialized prefix alive for the borrow.
        unsafe { std::slice::from_raw_parts(self.ptr().as_ptr(), self.initialized) }
    }
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        // SAFETY: the exclusive owner borrow excludes all other access to this prefix.
        unsafe { std::slice::from_raw_parts_mut(self.ptr().as_ptr(), self.initialized) }
    }
    pub fn uninit_mut_slice(&mut self) -> &mut [MaybeUninit<u8>] {
        // SAFETY: MaybeUninit permits every byte state and this borrow is exclusive.
        unsafe { std::slice::from_raw_parts_mut(self.ptr().as_ptr().cast(), self.len) }
    }
    pub fn initialize_prefix(&mut self, initialized: usize) {
        assert!(initialized <= self.len);
        self.initialized = self.initialized.max(initialized);
    }
    pub fn zero_uninitialized(&mut self) {
        // SAFETY: the uninitialized suffix is contained in the exclusive allocation.
        unsafe {
            std::ptr::write_bytes(
                self.ptr().as_ptr().add(self.initialized),
                0,
                self.len - self.initialized,
            )
        };
        self.initialized = self.len;
    }
}
// SAFETY: ownership moves with the allocation, which has no borrowed or thread-local state.
unsafe impl Send for AlignedBuffer {}
// SAFETY: shared access exposes immutable initialized bytes; mutation requires exclusive access.
unsafe impl Sync for AlignedBuffer {}
#[cfg(test)]
mod tests {
    use super::AlignedBuffer;

    #[test]
    fn uninitialized_buffer_exposes_only_initialized_bytes() {
        let mut buffer = AlignedBuffer::uninitialized(4, 64).unwrap();
        assert_eq!(buffer.len(), 4);
        assert!(buffer.as_slice().is_empty());
        buffer.uninit_mut_slice()[0].write(0x5a);
        buffer.initialize_prefix(1);
        assert_eq!(buffer.as_slice(), &[0x5a]);
        buffer.zero_uninitialized();
        assert_eq!(buffer.as_slice(), &[0x5a, 0, 0, 0]);
    }
}
