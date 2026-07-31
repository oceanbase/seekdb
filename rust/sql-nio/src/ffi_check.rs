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

use std::ffi::c_char;

pub(crate) fn checked_bytes_len(data: *const c_char, len: i64) -> Option<usize> {
    let len = usize::try_from(len).ok()?;
    if len > isize::MAX as usize
        || (len != 0 && (data.is_null() || (data as usize).checked_add(len).is_none()))
    {
        return None;
    }
    Some(len)
}

pub(crate) fn checked_array_len<T>(data: *const T, len: i64) -> Option<usize> {
    let len = usize::try_from(len).ok()?;
    let byte_len = len.checked_mul(std::mem::size_of::<T>())?;
    if byte_len > isize::MAX as usize
        || (len != 0
            && (data.is_null()
                || !(data as usize).is_multiple_of(std::mem::align_of::<T>())
                || (data as usize).checked_add(byte_len).is_none()))
    {
        return None;
    }
    Some(len)
}

pub(crate) fn checked_out_range<T>(data: *mut T) -> Option<(usize, usize)> {
    let start = data as usize;
    if data.is_null() || !start.is_multiple_of(std::mem::align_of::<T>()) {
        return None;
    }
    Some((start, start.checked_add(std::mem::size_of::<T>())?))
}

pub(crate) fn ranges_overlap(left: (usize, usize), right: (usize, usize)) -> bool {
    left.0 < right.1 && right.0 < left.1
}
