/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

package seekdb

/*
#cgo CFLAGS: -I../../../../src/include
#cgo linux LDFLAGS: -L${SRCDIR}/../../../../build_release/src/include -Wl,-rpath,${SRCDIR}/../../../../build_release/src/include -Wl,--allow-shlib-undefined -lseekdb
#cgo darwin CFLAGS: -mmacosx-version-min=15.7
#cgo darwin LDFLAGS: -L${SRCDIR}/../../../../build_release/src/include -Wl,-rpath,${SRCDIR}/../../../../build_release/src/include -lseekdb -mmacosx-version-min=15.7
#cgo windows CFLAGS: -I${SRCDIR}/../../../../src/include
#cgo windows LDFLAGS: -L${SRCDIR}/../../../../build_release/src/include -lseekdb
#include "seekdb.h"
#include <stdlib.h>
*/
import "C"
import (
	"errors"
	"fmt"
	"unsafe"
)

// Error codes
const (
	SeekdbSuccess              = 0
	SeekdbErrorInvalidParam    = -1
	SeekdbErrorConnectionFailed = -2
	SeekdbErrorQueryFailed     = -3
	SeekdbErrorMemoryAlloc     = -4
	SeekdbErrorNotInitialized  = -5
)

// Open the database
func Open(dbDir string) error {
	dbDirC := C.CString(dbDir)
	defer C.free(unsafe.Pointer(dbDirC))
	ret := C.seekdb_open(dbDirC)
	if ret != SeekdbSuccess {
		return fmt.Errorf("failed to open database: error code %d", int(ret))
	}
	return nil
}

// Close the database
func Close() {
	C.seekdb_close()
}

// Connection represents a database connection
type Connection struct {
	handle unsafe.Pointer
}

// NewConnection creates a new connection handle
func NewConnection() (*Connection, error) {
	// Handle will be allocated by seekdb_connect
	return &Connection{handle: nil}, nil
}

// Connect connects to a database
func (c *Connection) Connect(database string, autocommit bool) error {
	databaseC := C.CString(database)
	defer C.free(unsafe.Pointer(databaseC))

	var handle C.SeekdbHandle
	ret := C.seekdb_connect(
		&handle,
		databaseC,
		C.bool(autocommit),
	)

	if ret != SeekdbSuccess {
		return errors.New("connection failed")
	}
	c.handle = unsafe.Pointer(handle)
	return nil
}

// Begin begins a transaction
func (c *Connection) Begin() error {
	ret := C.seekdb_begin(C.SeekdbHandle(c.handle))
	if ret != SeekdbSuccess {
		return errors.New("begin transaction failed")
	}
	return nil
}

// Commit commits a transaction
func (c *Connection) Commit() error {
	ret := C.seekdb_commit(C.SeekdbHandle(c.handle))
	if ret != SeekdbSuccess {
		return errors.New("commit transaction failed")
	}
	return nil
}

// Rollback rolls back a transaction
func (c *Connection) Rollback() error {
	ret := C.seekdb_rollback(C.SeekdbHandle(c.handle))
	if ret != SeekdbSuccess {
		return errors.New("rollback transaction failed")
	}
	return nil
}

// Execute executes a SQL query
func (c *Connection) Execute(sql string) (*Result, error) {
	sqlC := C.CString(sql)
	defer C.free(unsafe.Pointer(sqlC))

	var resultPtr C.SeekdbResult
	ret := C.seekdb_query(C.SeekdbHandle(c.handle), sqlC, &resultPtr)

	if ret != SeekdbSuccess {
		return nil, errors.New(c.GetLastError())
	}

	// Get stored result
	storedResult := C.seekdb_store_result(C.SeekdbHandle(c.handle))
	if storedResult == nil {
		return nil, errors.New("Failed to store result")
	}

	return &Result{resultPtr: unsafe.Pointer(storedResult)}, nil
}

// ExecuteUpdate executes an update SQL statement
func (c *Connection) ExecuteUpdate(sql string) (int64, error) {
	sqlC := C.CString(sql)
	defer C.free(unsafe.Pointer(sqlC))

	var resultPtr C.SeekdbResult
	ret := C.seekdb_query(C.SeekdbHandle(c.handle), sqlC, &resultPtr)

	if ret != SeekdbSuccess {
		return 0, errors.New(c.GetLastError())
	}

	if resultPtr != nil {
		C.seekdb_result_free(resultPtr)
	}

	// Get affected rows
	affectedRows := C.seekdb_affected_rows(C.SeekdbHandle(c.handle))
	return int64(affectedRows), nil
}

// GetLastError returns the last error message
func (c *Connection) GetLastError() string {
	errorMsg := C.seekdb_error(C.SeekdbHandle(c.handle))
	if errorMsg != nil {
		return C.GoString(errorMsg)
	}
	return ""
}

// Close closes the connection
func (c *Connection) Close() {
	if c.handle != nil {
		C.seekdb_connect_close(C.SeekdbHandle(c.handle))
		c.handle = nil
	}
}

// Result represents a query result set
type Result struct {
	resultPtr unsafe.Pointer
}

// RowCount returns the number of rows in the result set
func (r *Result) RowCount() int64 {
	return int64(C.seekdb_num_rows(C.SeekdbResult(r.resultPtr)))
}

// ColumnCount returns the number of columns in the result set
func (r *Result) ColumnCount() int32 {
	return int32(C.seekdb_num_fields(C.SeekdbResult(r.resultPtr)))
}

// ColumnNames returns the column names
func (r *Result) ColumnNames() []string {
	count := r.ColumnCount()
	names := make([]string, count)
	for i := int32(0); i < count; i++ {
		buf := make([]byte, 256)
		C.seekdb_result_column_name(
			C.SeekdbResult(r.resultPtr),
			C.int32_t(i),
			(*C.char)(unsafe.Pointer(&buf[0])),
			C.size_t(len(buf)),
		)
		names[i] = C.GoString((*C.char)(unsafe.Pointer(&buf[0])))
	}
	return names
}

// Row represents a single row in the result set
type Row struct {
	rowPtr    unsafe.Pointer
	columnCount int32
}

// FetchAll fetches all rows from the result set
func (r *Result) FetchAll() [][]string {
	var rows [][]string

	for {
		rowPtr := C.seekdb_fetch_row(C.SeekdbResult(r.resultPtr))
		if rowPtr == nil {
			break
		}

		row := make([]string, r.ColumnCount())
		for i := int32(0); i < r.ColumnCount(); i++ {
			isNull := C.seekdb_row_is_null(rowPtr, C.int32_t(i))
			if isNull {
				row[i] = ""
			} else {
				buf := make([]byte, 4096)
				ret := C.seekdb_row_get_string(
					rowPtr,
					C.int32_t(i),
					(*C.char)(unsafe.Pointer(&buf[0])),
					C.size_t(len(buf)),
				)
				if ret == SeekdbSuccess {
					row[i] = C.GoString((*C.char)(unsafe.Pointer(&buf[0])))
				}
			}
		}
		rows = append(rows, row)
	}

	return rows
}

// Free frees the result set
func (r *Result) Free() {
	if r.resultPtr != nil {
		C.seekdb_result_free(C.SeekdbResult(r.resultPtr))
		r.resultPtr = nil
	}
}

