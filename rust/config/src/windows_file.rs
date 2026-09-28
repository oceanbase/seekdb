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

use std::ffi::c_void;
use std::fs::File;
use std::io;
use std::mem::size_of;
use std::os::windows::ffi::OsStrExt;
use std::os::windows::io::FromRawHandle;
use std::path::Path;
use std::ptr;
use windows_sys::Win32::Foundation::{
    CloseHandle, LocalFree, ERROR_INSUFFICIENT_BUFFER, GENERIC_WRITE, HANDLE, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Security::Authorization::{
    ConvertSidToStringSidW, ConvertStringSecurityDescriptorToSecurityDescriptorW, SDDL_REVISION_1,
};
use windows_sys::Win32::Security::{
    GetTokenInformation, TokenUser, SECURITY_ATTRIBUTES, TOKEN_QUERY, TOKEN_USER,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, MoveFileExW, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, MOVEFILE_REPLACE_EXISTING,
    MOVEFILE_WRITE_THROUGH,
};
use windows_sys::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};

struct OwnedHandle(HANDLE);

impl Drop for OwnedHandle {
    fn drop(&mut self) {
        unsafe { CloseHandle(self.0) };
    }
}

struct LocalAllocation(*mut c_void);

impl Drop for LocalAllocation {
    fn drop(&mut self) {
        unsafe { LocalFree(self.0) };
    }
}

fn wide_path(path: &Path) -> Vec<u16> {
    path.as_os_str().encode_wide().chain(Some(0)).collect()
}

fn owner_only_descriptor() -> io::Result<LocalAllocation> {
    let mut raw_token = ptr::null_mut();
    if unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut raw_token) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let token = OwnedHandle(raw_token);

    let mut length = 0;
    let result =
        unsafe { GetTokenInformation(token.0, TokenUser, ptr::null_mut(), 0, &mut length) };
    if result != 0 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "token user information has no size",
        ));
    }
    let error = io::Error::last_os_error();
    if error.raw_os_error() != Some(ERROR_INSUFFICIENT_BUFFER as i32) {
        return Err(error);
    }
    if length < size_of::<TOKEN_USER>() as u32 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "token user information is too small",
        ));
    }
    // TOKEN_USER contains a pointer, so the output buffer must be pointer aligned.
    let mut token_info = vec![0usize; (length as usize).div_ceil(size_of::<usize>())];
    if unsafe {
        GetTokenInformation(
            token.0,
            TokenUser,
            token_info.as_mut_ptr().cast(),
            length,
            &mut length,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    let user = unsafe { &*token_info.as_ptr().cast::<TOKEN_USER>() };
    if user.User.Sid.is_null() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "missing user SID",
        ));
    }
    let mut raw_sid = ptr::null_mut();
    if unsafe { ConvertSidToStringSidW(user.User.Sid, &mut raw_sid) } == 0 {
        return Err(io::Error::last_os_error());
    }
    let sid_allocation = LocalAllocation(raw_sid.cast());
    let sid_length = unsafe { (0..).take_while(|&i| *raw_sid.add(i) != 0).count() };
    let sid = String::from_utf16(unsafe { std::slice::from_raw_parts(raw_sid, sid_length) })
        .map_err(|_| io::Error::new(io::ErrorKind::InvalidData, "invalid user SID"))?;
    drop(sid_allocation);

    // Protect the DACL from inheritance and grant file access only to the
    // process user's SID. The descriptor is passed at file creation time.
    let sddl: Vec<u16> = format!("D:P(A;;FA;;;{sid})")
        .encode_utf16()
        .chain(Some(0))
        .collect();
    let mut descriptor = ptr::null_mut();
    if unsafe {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.as_ptr(),
            SDDL_REVISION_1,
            &mut descriptor,
            ptr::null_mut(),
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    Ok(LocalAllocation(descriptor))
}

pub(super) fn create_private(path: &Path) -> io::Result<File> {
    let descriptor = owner_only_descriptor()?;
    let attributes = SECURITY_ATTRIBUTES {
        nLength: size_of::<SECURITY_ATTRIBUTES>() as u32,
        lpSecurityDescriptor: descriptor.0,
        bInheritHandle: 0,
    };
    let path = wide_path(path);
    let handle = unsafe {
        CreateFileW(
            path.as_ptr(),
            GENERIC_WRITE,
            0,
            &attributes,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            ptr::null_mut(),
        )
    };
    if handle == INVALID_HANDLE_VALUE {
        return Err(io::Error::last_os_error());
    }
    Ok(unsafe { File::from_raw_handle(handle) })
}

pub(super) fn replace(from: &Path, to: &Path) -> io::Result<()> {
    let from = wide_path(from);
    let to = wide_path(to);
    if unsafe {
        MoveFileExW(
            from.as_ptr(),
            to.as_ptr(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
        )
    } == 0
    {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}
