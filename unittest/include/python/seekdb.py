#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Copyright (c) 2025 OceanBase.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

SeekDB Python FFI Binding
=========================
Provides Python bindings for the SeekDB embedded database using ctypes.

Example:
    from seekdb import Seekdb, SeekdbConnection
    
    # Open the embedded database
    Seekdb.open("./mydb.db")
    
    # Create a connection
    conn = SeekdbConnection("test")
    
    # Execute queries
    result = conn.execute("SELECT 1 as id, 'hello' as message")
    for row in result:
        print(row)
    
    # Close
    conn.close()
    Seekdb.close()
"""

import ctypes
import os
import sys
from ctypes import c_int, c_int32, c_int64, c_uint32, c_uint64, c_char_p, c_void_p, c_bool, c_double, c_size_t, POINTER, byref
from typing import Optional, List, Any, Iterator

try:
    from seekdb_binding_probe import emit as _hang_probe_emit
except ImportError:

    def _hang_probe_emit(_step: str) -> None:
        pass

# Error codes
SEEKDB_SUCCESS = 0
SEEKDB_ERROR_INVALID_PARAM = -1
SEEKDB_ERROR_CONNECTION_FAILED = -2
SEEKDB_ERROR_QUERY_FAILED = -3
SEEKDB_ERROR_MEMORY_ALLOC = -4
SEEKDB_ERROR_NOT_INITIALIZED = -5


class SeekdbError(Exception):
    """Exception raised for Seekdb errors."""
    
    def __init__(self, code: int, message: str = ""):
        self.code = code
        self.message = message
        super().__init__(f"Seekdb error {code}: {message}")


class SeekdbRow:
    """Represents a row from a query result."""
    
    def __init__(self, lib, row_ptr: c_void_p, column_count: int, column_names: List[str]):
        self._lib = lib
        self._row_ptr = row_ptr
        self._column_count = column_count
        self._column_names = column_names

    def is_null(self, column_index: int) -> bool:
        """Check if a column value is NULL."""
        return self._lib.seekdb_row_is_null(self._row_ptr, column_index)
    
    def get_string(self, column_index: int) -> Optional[str]:
        """Get string value from column.
        
        Handles both text and binary data (e.g., VECTOR type).
        VECTOR type is stored as binary (float array) and returned as binary data.
        Binary data is decoded with errors='replace' to avoid UnicodeDecodeError.
        """
        if self.is_null(column_index):
            return None
        
        buffer = ctypes.create_string_buffer(65536)  # 64KB buffer
        ret = self._lib.seekdb_row_get_string(self._row_ptr, column_index, buffer, len(buffer))
        if ret != SEEKDB_SUCCESS:
            return None
        
        raw_bytes = buffer.value
        if not raw_bytes:
            return None
        
        # Decode bytes to string, using errors='replace' for binary data (e.g., VECTOR type)
        # VECTOR type returns binary float array data, which may contain non-UTF-8 bytes
        # This replaces invalid UTF-8 bytes with replacement character (U+FFFD)
        try:
            return raw_bytes.decode('utf-8')
        except UnicodeDecodeError:
            # For binary data, use errors='replace' to handle invalid bytes gracefully
            return raw_bytes.decode('utf-8', errors='replace')
    
    def get_int64(self, column_index: int) -> Optional[int]:
        """Get integer value from column."""
        if self.is_null(column_index):
            return None
        
        value = c_int64()
        ret = self._lib.seekdb_row_get_int64(self._row_ptr, column_index, byref(value))
        if ret != SEEKDB_SUCCESS:
            return None
        return value.value
    
    def get_double(self, column_index: int) -> Optional[float]:
        """Get double value from column."""
        if self.is_null(column_index):
            return None
        
        value = c_double()
        ret = self._lib.seekdb_row_get_double(self._row_ptr, column_index, byref(value))
        if ret != SEEKDB_SUCCESS:
            return None
        return value.value
    
    def get_bool(self, column_index: int) -> Optional[bool]:
        """Get boolean value from column."""
        if self.is_null(column_index):
            return None
        
        value = c_bool()
        ret = self._lib.seekdb_row_get_bool(self._row_ptr, column_index, byref(value))
        if ret != SEEKDB_SUCCESS:
            return None
        return value.value
    
    def get(self, column_index: int) -> Optional[str]:
        """Get value as string (generic getter)."""
        return self.get_string(column_index)
    
    def __getitem__(self, key):
        """Get value by column index or name."""
        if isinstance(key, int):
            return self.get_string(key)
        elif isinstance(key, str):
            try:
                index = self._column_names.index(key)
                return self.get_string(index)
            except ValueError:
                raise KeyError(f"Column '{key}' not found")
        else:
            raise TypeError(f"Invalid key type: {type(key)}")
    
    def as_dict(self) -> dict:
        """Return row as dictionary."""
        return {name: self.get_string(i) for i, name in enumerate(self._column_names)}
    
    def as_list(self) -> list:
        """Return row as list."""
        return [self.get_string(i) for i in range(self._column_count)]


class SeekdbResult:
    """Represents a query result set."""
    
    def __init__(self, lib, result_ptr: c_void_p):
        self._lib = lib
        self._result_ptr = result_ptr
        self._freed = False
        self._column_names: Optional[List[str]] = None
    
    def __del__(self):
        # Only free if not already freed
        # This prevents double free when free() is called explicitly
        if not self._freed:
            self.free()
    
    def free(self):
        """Free the result handle."""
        if not self._freed and self._result_ptr:
            self._lib.seekdb_result_free(self._result_ptr)
            self._freed = True
            self._result_ptr = None  # Clear pointer to prevent double free
    
    @property
    def row_count(self) -> int:
        """Get number of rows in result."""
        return self._lib.seekdb_num_rows(self._result_ptr)
    
    @property
    def column_count(self) -> int:
        """Get number of columns in result."""
        return self._lib.seekdb_num_fields(self._result_ptr)
    
    @property
    def column_names(self) -> List[str]:
        """Get column names."""
        if self._column_names is None:
            self._column_names = []
            for i in range(self.column_count):
                buffer = ctypes.create_string_buffer(256)
                ret = self._lib.seekdb_result_column_name(self._result_ptr, i, buffer, len(buffer))
                if ret == SEEKDB_SUCCESS:
                    self._column_names.append(buffer.value.decode('utf-8'))
                else:
                    self._column_names.append(f"col_{i}")
        return self._column_names
    
    def fetch_row(self) -> Optional[SeekdbRow]:
        """Fetch next row from result."""
        row_ptr = self._lib.seekdb_fetch_row(self._result_ptr)
        if not row_ptr:
            return None
        return SeekdbRow(self._lib, row_ptr, self.column_count, self.column_names)
    
    def fetch_all(self) -> List[dict]:
        """Fetch all rows as list of dictionaries."""
        rows = []
        while True:
            row = self.fetch_row()
            if row is None:
                break
            rows.append(row.as_dict())
        return rows
    
    def __iter__(self) -> Iterator[SeekdbRow]:
        """Iterate over rows."""
        while True:
            row = self.fetch_row()
            if row is None:
                break
            yield row


class SeekdbConnection:
    """Represents a database connection."""
    
    def __init__(self, database: str = "test", autocommit: bool = False):
        """
        Create a new connection.
        
        Args:
            database: Database name (default: "test")
            autocommit: Enable autocommit mode (default: False)
        """
        self._lib = Seekdb._get_lib()
        self._handle = c_void_p()
        self._database = database
        self._closed = False
        
        ret = self._lib.seekdb_connect(byref(self._handle), database.encode('utf-8'), autocommit)
        if ret != SEEKDB_SUCCESS:
            error_code = int(self._lib.seekdb_last_error_code())
            if error_code == SEEKDB_SUCCESS:
                error_code = ret
            error_msg = self._lib.seekdb_last_error()
            detail = error_msg.decode('utf-8') if error_msg else ""
            raise SeekdbError(
                error_code,
                detail or f"Failed to connect to database '{database}'")
    
    def __del__(self):
        self.close()
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False
    
    def close(self):
        """Close the connection."""
        if not self._closed and self._handle.value:
            self._lib.seekdb_connect_close(self._handle)
            self._closed = True
    
    def execute(self, sql: str) -> SeekdbResult:
        """
        Execute a SQL query.
        
        Args:
            sql: SQL query string
            
        Returns:
            SeekdbResult object
        """
        if self._closed:
            raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED, "Connection is closed")
        
        result_ptr = c_void_p()
        ret = self._lib.seekdb_query(self._handle, sql.encode('utf-8'), byref(result_ptr))
        if ret != SEEKDB_SUCCESS:
            error_msg = self.get_last_error()
            raise SeekdbError(self._last_error_code() or ret,
                              error_msg or f"Failed to execute query: {sql}")
        
        # Get stored result
        stored_result = self._lib.seekdb_store_result(self._handle)
        if not stored_result:
            raise SeekdbError(SEEKDB_ERROR_QUERY_FAILED, "Failed to store result")
        
        return SeekdbResult(self._lib, stored_result)
    
    def execute_update(self, sql: str) -> int:
        """
        Execute an UPDATE/INSERT/DELETE query.
        
        Args:
            sql: SQL query string
            
        Returns:
            Number of affected rows
        """
        if self._closed:
            raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED, "Connection is closed")
        
        result_ptr = c_void_p()
        ret = self._lib.seekdb_query(self._handle, sql.encode('utf-8'), byref(result_ptr))
        if ret != SEEKDB_SUCCESS:
            error_msg = self.get_last_error()
            raise SeekdbError(self._last_error_code() or ret,
                              error_msg or f"Failed to execute update: {sql}")
        
        if result_ptr.value:
            self._lib.seekdb_result_free(result_ptr)
        
        # Get affected rows
        affected_rows = self._lib.seekdb_affected_rows(self._handle)
        return affected_rows
    
    def begin(self):
        """Begin a transaction."""
        if self._closed:
            raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED, "Connection is closed")
        
        ret = self._lib.seekdb_begin(self._handle)
        if ret != SEEKDB_SUCCESS:
            raise SeekdbError(
                self._last_error_code() or ret,
                self.get_last_error() or "Failed to begin transaction")
    
    def commit(self):
        """Commit the current transaction."""
        if self._closed:
            raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED, "Connection is closed")
        
        ret = self._lib.seekdb_commit(self._handle)
        if ret != SEEKDB_SUCCESS:
            raise SeekdbError(
                self._last_error_code() or ret,
                self.get_last_error() or "Failed to commit transaction")
    
    def rollback(self):
        """Rollback the current transaction."""
        if self._closed:
            raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED, "Connection is closed")
        
        ret = self._lib.seekdb_rollback(self._handle)
        if ret != SEEKDB_SUCCESS:
            raise SeekdbError(
                self._last_error_code() or ret,
                self.get_last_error() or "Failed to rollback transaction")
    
    def set_autocommit(self, autocommit: bool):
        """Set autocommit mode."""
        if self._closed:
            raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED, "Connection is closed")
        
        ret = self._lib.seekdb_autocommit(self._handle, autocommit)
        if ret != SEEKDB_SUCCESS:
            raise SeekdbError(
                self._last_error_code() or ret,
                self.get_last_error() or "Failed to set autocommit")
    
    def get_last_error(self) -> str:
        """Get the last error message."""
        error_msg = self._lib.seekdb_error(self._handle)
        if error_msg:
            return error_msg.decode('utf-8')
        return ""

    def _last_error_code(self) -> int:
        """Get the real OB error code recorded by the library (0 if none)."""
        error_code = self._lib.seekdb_last_error_code()
        return int(error_code) if error_code != SEEKDB_SUCCESS else 0


class Seekdb:
    """
    SeekDB embedded database manager.
    
    Usage:
        Seekdb.open("./mydb.db")
        conn = SeekdbConnection("test")
        # ... use connection ...
        conn.close()
        Seekdb.close()
    """
    
    _lib = None
    _opened = False
    # Windows: os.add_dll_directory() handles must live until process exit (see _get_lib).
    _dll_dir_handles: List[Any] = []

    @classmethod
    def _get_lib(cls):
        """Get or load the shared library."""
        if cls._lib is None:
            # Must use SEEKDB_LIB_PATH environment variable
            lib_path = os.environ.get('SEEKDB_LIB_PATH')
            
            if not lib_path:
                raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED,
                    "SEEKDB_LIB_PATH environment variable is not set. "
                    "Set it to the absolute path of seekdb DLL or libseekdb.so.")

            lib_path_abs = os.path.abspath(lib_path)
            if not os.path.exists(lib_path_abs):
                raise SeekdbError(SEEKDB_ERROR_NOT_INITIALIZED,
                    f"SeekDB library not found at {lib_path_abs}. "
                    "Check SEEKDB_LIB_PATH.")

            # Windows (Python 3.8+): dependent DLLs (vcpkg, OpenSSL) are not always beside seekdb.dll.
            # Register search dirs via add_dll_directory and PATH; keep handles alive — discarding the
            # return value lets CPython GC remove dirs before ctypes.CDLL() runs (CI: "or one of its dependencies").
            if sys.platform == "win32":
                _dll_dirs = [os.path.dirname(lib_path_abs)]
                for _env in ("SEEKDB_VCPKG_BIN", "SEEKDB_OPENSSL_BIN", "SEEKDB_VSAG_BIN"):
                    _p = os.environ.get(_env)
                    if _p and os.path.isdir(_p):
                        _dll_dirs.append(os.path.abspath(_p))
                os.environ["PATH"] = ";".join(_dll_dirs) + ";" + os.environ.get("PATH", "")
                if hasattr(os, "add_dll_directory"):
                    cls._dll_dir_handles = [os.add_dll_directory(_d) for _d in _dll_dirs]
                # winmode=8 (LOAD_WITH_ALTERED_SEARCH_PATH): honor PATH for transitive DLLs like Node/koffi.
                cls._lib = ctypes.CDLL(lib_path_abs, winmode=8)
            else:
                cls._lib = ctypes.CDLL(lib_path_abs)
            cls._setup_functions()
        
        return cls._lib
    
    @classmethod
    def _setup_functions(cls):
        """Setup function signatures."""
        lib = cls._lib
        
        # int seekdb_open(const char* db_dir)
        lib.seekdb_open.argtypes = [c_char_p]
        lib.seekdb_open.restype = c_int

        # const char* seekdb_last_error(void)
        lib.seekdb_last_error.argtypes = []
        lib.seekdb_last_error.restype = c_char_p

        # int seekdb_last_error_code(void)
        lib.seekdb_last_error_code.argtypes = []
        lib.seekdb_last_error_code.restype = c_int
        
        # void seekdb_close(void)
        lib.seekdb_close.argtypes = []
        lib.seekdb_close.restype = None
        
        # int seekdb_connect(SeekdbHandle* handle, const char* database, bool autocommit)
        lib.seekdb_connect.argtypes = [POINTER(c_void_p), c_char_p, c_bool]
        lib.seekdb_connect.restype = c_int
        
        # void seekdb_connect_close(SeekdbHandle handle)
        lib.seekdb_connect_close.argtypes = [c_void_p]
        lib.seekdb_connect_close.restype = None
        
        # int seekdb_query(SeekdbHandle handle, const char* query, SeekdbResult* result)
        lib.seekdb_query.argtypes = [c_void_p, c_char_p, POINTER(c_void_p)]
        lib.seekdb_query.restype = c_int
        
        # SeekdbResult seekdb_store_result(SeekdbHandle handle)
        lib.seekdb_store_result.argtypes = [c_void_p]
        lib.seekdb_store_result.restype = c_void_p
        
        # my_ulonglong seekdb_num_rows(SeekdbResult result)
        lib.seekdb_num_rows.argtypes = [c_void_p]
        lib.seekdb_num_rows.restype = c_uint64
        
        # unsigned int seekdb_num_fields(SeekdbResult result)
        lib.seekdb_num_fields.argtypes = [c_void_p]
        lib.seekdb_num_fields.restype = c_uint32
        
        # int seekdb_result_column_name(SeekdbResult result, int32_t column_index, char* name, size_t name_len)
        lib.seekdb_result_column_name.argtypes = [c_void_p, c_int32, c_char_p, c_size_t]
        lib.seekdb_result_column_name.restype = c_int
        
        # SeekdbRow seekdb_fetch_row(SeekdbResult result)
        lib.seekdb_fetch_row.argtypes = [c_void_p]
        lib.seekdb_fetch_row.restype = c_void_p
        
        # int seekdb_row_get_string(SeekdbRow row, int32_t column_index, char* value, size_t value_len)
        lib.seekdb_row_get_string.argtypes = [c_void_p, c_int32, c_char_p, c_size_t]
        lib.seekdb_row_get_string.restype = c_int
        
        # int seekdb_row_get_int64(SeekdbRow row, int32_t column_index, int64_t* value)
        lib.seekdb_row_get_int64.argtypes = [c_void_p, c_int32, POINTER(c_int64)]
        lib.seekdb_row_get_int64.restype = c_int
        
        # int seekdb_row_get_double(SeekdbRow row, int32_t column_index, double* value)
        lib.seekdb_row_get_double.argtypes = [c_void_p, c_int32, POINTER(c_double)]
        lib.seekdb_row_get_double.restype = c_int
        
        # int seekdb_row_get_bool(SeekdbRow row, int32_t column_index, bool* value)
        lib.seekdb_row_get_bool.argtypes = [c_void_p, c_int32, POINTER(c_bool)]
        lib.seekdb_row_get_bool.restype = c_int
        
        # bool seekdb_row_is_null(SeekdbRow row, int32_t column_index)
        lib.seekdb_row_is_null.argtypes = [c_void_p, c_int32]
        lib.seekdb_row_is_null.restype = c_bool
        
        # void seekdb_result_free(SeekdbResult result)
        lib.seekdb_result_free.argtypes = [c_void_p]
        lib.seekdb_result_free.restype = None
        
        # const char* seekdb_error(SeekdbHandle handle)
        lib.seekdb_error.argtypes = [c_void_p]
        lib.seekdb_error.restype = c_char_p
        
        # my_ulonglong seekdb_affected_rows(SeekdbHandle handle)
        lib.seekdb_affected_rows.argtypes = [c_void_p]
        lib.seekdb_affected_rows.restype = c_uint64
        
        # int seekdb_begin(SeekdbHandle handle)
        lib.seekdb_begin.argtypes = [c_void_p]
        lib.seekdb_begin.restype = c_int
        
        # int seekdb_commit(SeekdbHandle handle)
        lib.seekdb_commit.argtypes = [c_void_p]
        lib.seekdb_commit.restype = c_int
        
        # int seekdb_rollback(SeekdbHandle handle)
        lib.seekdb_rollback.argtypes = [c_void_p]
        lib.seekdb_rollback.restype = c_int
        
        # int seekdb_autocommit(SeekdbHandle handle, bool mode)
        lib.seekdb_autocommit.argtypes = [c_void_p, c_bool]
        lib.seekdb_autocommit.restype = c_int
    
    @classmethod
    def open(cls, db_dir: str):
        """
        Open the embedded database.
        
        Args:
            db_dir: Database directory path
        """
        lib = cls._get_lib()
        _hang_probe_emit("seekdb.py:before_ctypes_seekdb_open")
        ret = lib.seekdb_open(db_dir.encode('utf-8'))
        _hang_probe_emit("seekdb.py:after_ctypes_seekdb_open")
        if ret != SEEKDB_SUCCESS:
            # Surface the real OB error (e.g. -4005 the object is initialized twice)
            # instead of only the generic C ABI return code.
            error_code = int(lib.seekdb_last_error_code())
            if error_code == SEEKDB_SUCCESS:
                error_code = ret
            error_msg = lib.seekdb_last_error()
            detail = error_msg.decode('utf-8') if error_msg else ""
            if detail:
                raise SeekdbError(
                    error_code, f"Failed to open database at '{db_dir}': {detail}")
            raise SeekdbError(error_code, f"Failed to open database at '{db_dir}'")
        cls._opened = True
    
    @classmethod
    def close(cls):
        """Close the embedded database."""
        if cls._lib and cls._opened:
            _hang_probe_emit("seekdb.py:before_ctypes_seekdb_close")
            cls._lib.seekdb_close()
            _hang_probe_emit("seekdb.py:after_ctypes_seekdb_close")
            cls._opened = False
    
    @classmethod
    def is_open(cls) -> bool:
        """Check if database is open."""
        return cls._opened


# Convenience functions
def open(db_dir: str):
    """Open the embedded database."""
    Seekdb.open(db_dir)


def close():
    """Close the embedded database."""
    Seekdb.close()


def connect(database: str = "test", autocommit: bool = False) -> SeekdbConnection:
    """Create a new connection."""
    return SeekdbConnection(database, autocommit)