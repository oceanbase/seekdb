/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

use libc::{c_char, c_int, c_void, size_t};
use std::ffi::{CStr, CString};
use std::ptr;

#[link(name = "seekdb")]
extern "C" {
    fn seekdb_open(db_dir: *const c_char) -> c_int;
    fn seekdb_close();
    fn seekdb_connect(handle: *mut *mut c_void, database: *const c_char, autocommit: bool) -> c_int;
    fn seekdb_connect_close(handle: *mut c_void);
    fn seekdb_begin(handle: *mut c_void) -> c_int;
    fn seekdb_commit(handle: *mut c_void) -> c_int;
    fn seekdb_rollback(handle: *mut c_void) -> c_int;
    #[allow(dead_code)]
    fn seekdb_autocommit(handle: *mut c_void, mode: bool) -> c_int;
    fn seekdb_query(handle: *mut c_void, query: *const c_char, result: *mut *mut c_void) -> c_int;
    fn seekdb_store_result(handle: *mut c_void) -> *mut c_void;
    fn seekdb_num_rows(result: *mut c_void) -> u64;
    fn seekdb_num_fields(result: *mut c_void) -> u32;
    fn seekdb_result_column_name(
        result: *mut c_void,
        column_index: i32,
        name: *mut c_char,
        name_len: size_t,
    ) -> c_int;
    fn seekdb_fetch_row(result: *mut c_void) -> *mut c_void;
    fn seekdb_row_get_string(
        row: *mut c_void,
        column_index: i32,
        value: *mut c_char,
        value_len: size_t,
    ) -> c_int;
    #[allow(dead_code)]
    fn seekdb_row_get_int64(row: *mut c_void, column_index: i32, value: *mut i64) -> c_int;
    #[allow(dead_code)]
    fn seekdb_row_get_double(row: *mut c_void, column_index: i32, value: *mut f64) -> c_int;
    #[allow(dead_code)]
    fn seekdb_row_get_bool(row: *mut c_void, column_index: i32, value: *mut bool) -> c_int;
    fn seekdb_row_is_null(row: *mut c_void, column_index: i32) -> bool;
    fn seekdb_result_free(result: *mut c_void);
    fn seekdb_error(handle: *mut c_void) -> *const c_char;
    fn seekdb_affected_rows(handle: *mut c_void) -> u64;
}

// Error codes
pub const SEEKDB_SUCCESS: c_int = 0;
pub const SEEKDB_ERROR_INVALID_PARAM: c_int = -1;
pub const SEEKDB_ERROR_CONNECTION_FAILED: c_int = -2;
pub const SEEKDB_ERROR_QUERY_FAILED: c_int = -3;
pub const SEEKDB_ERROR_MEMORY_ALLOC: c_int = -4;
pub const SEEKDB_ERROR_NOT_INITIALIZED: c_int = -5;

#[derive(Debug)]
pub enum SeekdbError {
    InvalidParam,
    ConnectionFailed(String),
    QueryFailed(String),
    MemoryAlloc,
    NotInitialized,
    Unknown(i32),
}

impl From<i32> for SeekdbError {
    fn from(code: i32) -> Self {
        match code {
            SEEKDB_ERROR_INVALID_PARAM => SeekdbError::InvalidParam,
            SEEKDB_ERROR_CONNECTION_FAILED => SeekdbError::ConnectionFailed(String::new()),
            SEEKDB_ERROR_QUERY_FAILED => SeekdbError::QueryFailed(String::new()),
            SEEKDB_ERROR_MEMORY_ALLOC => SeekdbError::MemoryAlloc,
            SEEKDB_ERROR_NOT_INITIALIZED => SeekdbError::NotInitialized,
            _ => SeekdbError::Unknown(code),
        }
    }
}

pub fn open(db_dir: &str) -> Result<(), SeekdbError> {
    let db_dir_c = CString::new(db_dir).unwrap();
    let ret = unsafe { seekdb_open(db_dir_c.as_ptr()) };
    if ret == SEEKDB_SUCCESS {
        Ok(())
    } else {
        Err(SeekdbError::from(ret))
    }
}

pub fn close() {
    unsafe {
        seekdb_close();
    }
}

pub struct SeekdbConnection {
    handle: *mut c_void,
}

impl SeekdbConnection {
    pub fn new() -> Result<Self, SeekdbError> {
        // Allocate handle pointer
        let handle: *mut c_void = ptr::null_mut();
        Ok(SeekdbConnection { handle })
    }

    pub fn connect(&mut self, database: &str, autocommit: bool) -> Result<(), SeekdbError> {
        let database_c = CString::new(database).unwrap();
        let ret = unsafe {
            seekdb_connect(
                &mut self.handle,
                database_c.as_ptr(),
                autocommit,
            )
        };

        if ret == SEEKDB_SUCCESS {
            Ok(())
        } else {
            Err(SeekdbError::ConnectionFailed(String::new()))
        }
    }

    pub fn begin(&self) -> Result<(), SeekdbError> {
        let ret = unsafe { seekdb_begin(self.handle) };
        if ret == SEEKDB_SUCCESS {
            Ok(())
        } else {
            Err(SeekdbError::QueryFailed(self.get_last_error()))
        }
    }

    pub fn commit(&self) -> Result<(), SeekdbError> {
        let ret = unsafe { seekdb_commit(self.handle) };
        if ret == SEEKDB_SUCCESS {
            Ok(())
        } else {
            Err(SeekdbError::QueryFailed(self.get_last_error()))
        }
    }

    pub fn rollback(&self) -> Result<(), SeekdbError> {
        let ret = unsafe { seekdb_rollback(self.handle) };
        if ret == SEEKDB_SUCCESS {
            Ok(())
        } else {
            Err(SeekdbError::QueryFailed(self.get_last_error()))
        }
    }

    #[allow(dead_code)]
    pub fn set_autocommit(&self, autocommit: bool) -> Result<(), SeekdbError> {
        let ret = unsafe { seekdb_autocommit(self.handle, autocommit) };
        if ret == SEEKDB_SUCCESS {
            Ok(())
        } else {
            Err(SeekdbError::QueryFailed(self.get_last_error()))
        }
    }

    pub fn execute(&self, sql: &str) -> Result<SeekdbResult, SeekdbError> {
        let sql_c = CString::new(sql).unwrap();
        let mut result: *mut c_void = ptr::null_mut();
        let ret = unsafe { seekdb_query(self.handle, sql_c.as_ptr(), &mut result) };

        if ret == SEEKDB_SUCCESS {
            // Get stored result
            let stored_result = unsafe { seekdb_store_result(self.handle) };
            if stored_result.is_null() {
                return Err(SeekdbError::QueryFailed("Failed to store result".to_string()));
            }
            Ok(SeekdbResult {
                result_ptr: stored_result,
            })
        } else {
            Err(SeekdbError::QueryFailed(self.get_last_error()))
        }
    }

    pub fn execute_update(&self, sql: &str) -> Result<i64, SeekdbError> {
        let sql_c = CString::new(sql).unwrap();
        let mut result: *mut c_void = ptr::null_mut();
        let ret = unsafe { seekdb_query(self.handle, sql_c.as_ptr(), &mut result) };

        if ret == SEEKDB_SUCCESS {
            if !result.is_null() {
                unsafe { seekdb_result_free(result) };
            }
            // Get affected rows
            let affected_rows = unsafe { seekdb_affected_rows(self.handle) };
            Ok(affected_rows as i64)
        } else {
            Err(SeekdbError::QueryFailed(self.get_last_error()))
        }
    }

    pub fn get_last_error(&self) -> String {
        unsafe {
            let error_ptr = seekdb_error(self.handle);
            if error_ptr.is_null() {
                String::new()
            } else {
                CStr::from_ptr(error_ptr)
                    .to_string_lossy()
                    .into_owned()
            }
        }
    }
}

impl Default for SeekdbConnection {
    fn default() -> Self {
        Self::new().unwrap()
    }
}

impl Drop for SeekdbConnection {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                seekdb_connect_close(self.handle);
            }
        }
    }
}

pub struct SeekdbResult {
    result_ptr: *mut c_void,
}

impl SeekdbResult {
    pub fn row_count(&self) -> i64 {
        unsafe { seekdb_num_rows(self.result_ptr) as i64 }
    }

    pub fn column_count(&self) -> i32 {
        unsafe { seekdb_num_fields(self.result_ptr) as i32 }
    }

    pub fn column_names(&self) -> Vec<String> {
        let count = self.column_count();
        let mut names = Vec::new();
        for i in 0..count {
            let mut buf = vec![0u8; 256];
            unsafe {
                seekdb_result_column_name(
                    self.result_ptr,
                    i,
                    buf.as_mut_ptr() as *mut c_char,
                    buf.len() as size_t,
                );
            }
            unsafe {
                let name = CStr::from_ptr(buf.as_ptr() as *const c_char)
                    .to_string_lossy()
                    .into_owned();
                names.push(name);
            }
        }
        names
    }

    pub fn fetch_all(&self) -> Vec<Vec<Option<String>>> {
        let mut rows = Vec::new();

        loop {
            let row_ptr = unsafe { seekdb_fetch_row(self.result_ptr) };
            if row_ptr.is_null() {
                break;
            }

            let mut row = Vec::new();
            for i in 0..self.column_count() {
                let is_null = unsafe { seekdb_row_is_null(row_ptr, i) };
                if is_null {
                    row.push(None);
                } else {
                    let mut buf = vec![0u8; 4096];
                    let ret = unsafe {
                        seekdb_row_get_string(
                            row_ptr,
                            i,
                            buf.as_mut_ptr() as *mut c_char,
                            buf.len() as size_t,
                        )
                    };
                    if ret == SEEKDB_SUCCESS {
                        unsafe {
                            let value = CStr::from_ptr(buf.as_ptr() as *const c_char)
                                .to_string_lossy()
                                .into_owned();
                            row.push(Some(value));
                        }
                    } else {
                        row.push(None);
                    }
                }
            }
            rows.push(row);
        }

        rows
    }
}

impl Drop for SeekdbResult {
    fn drop(&mut self) {
        if !self.result_ptr.is_null() {
            unsafe {
                seekdb_result_free(self.result_ptr);
            }
        }
    }
}
