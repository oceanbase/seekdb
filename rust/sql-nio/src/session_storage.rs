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

pub(crate) struct CppSessionStorage {
    ptr: NonNull<u8>,
    pub(crate) layout: Layout,
}

impl CppSessionStorage {
    pub(crate) fn new(size: usize) -> Self {
        assert!(size != 0, "ObSqlSockSession storage cannot be empty");
        let layout = Layout::from_size_align(size, 16)
            .expect("ObSqlSockSession storage must have a valid 16-byte-aligned layout");
        let ptr = NonNull::new(unsafe { alloc_zeroed(layout) })
            .unwrap_or_else(|| handle_alloc_error(layout));
        Self { ptr, layout }
    }

    pub(crate) fn as_ptr(&self) -> *mut c_void {
        self.ptr.as_ptr().cast::<c_void>()
    }
}

impl Drop for CppSessionStorage {
    fn drop(&mut self) {
        unsafe { dealloc(self.ptr.as_ptr(), self.layout) };
    }
}

unsafe impl Send for CppSessionStorage {}
unsafe impl Sync for CppSessionStorage {}
