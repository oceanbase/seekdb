/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

const koffi = require('koffi');

// Load the shared library
// Must set SEEKDB_LIB_PATH environment variable to the absolute path of libseekdb.so
const libPath = process.env.SEEKDB_LIB_PATH;
if (!libPath) {
    throw new Error('SEEKDB_LIB_PATH environment variable is not set. Please set it to the absolute path of libseekdb.so');
}
const lib = koffi.load(libPath);

// Define output parameter types using koffi.out()
// This is the correct way to handle output parameters (pointers that are written to)
const outVoidPtr = koffi.out(koffi.pointer('void*'));  // void** for output
const outInt64 = koffi.out(koffi.pointer('int64'));    // int64_t* for output
const outDouble = koffi.out(koffi.pointer('double'));  // double* for output
const outBool = koffi.out(koffi.pointer('bool'));      // bool* for output

// Define function signatures using koffi
const seekdb_open = lib.func('seekdb_open', 'int', ['str']);
const seekdb_close = lib.func('seekdb_close', 'void', []);
const seekdb_connect = lib.func('seekdb_connect', 'int', [outVoidPtr, 'str', 'bool']);
const seekdb_connect_close = lib.func('seekdb_connect_close', 'void', ['void*']);
const seekdb_begin = lib.func('seekdb_begin', 'int', ['void*']);
const seekdb_commit = lib.func('seekdb_commit', 'int', ['void*']);
const seekdb_rollback = lib.func('seekdb_rollback', 'int', ['void*']);
const seekdb_autocommit = lib.func('seekdb_autocommit', 'int', ['void*', 'bool']);
const seekdb_query = lib.func('seekdb_query', 'int', ['void*', 'str', outVoidPtr]);
const seekdb_store_result = lib.func('seekdb_store_result', 'void*', ['void*']);
const seekdb_num_rows = lib.func('seekdb_num_rows', 'uint64', ['void*']);
const seekdb_num_fields = lib.func('seekdb_num_fields', 'uint32', ['void*']);
const seekdb_result_column_name = lib.func('seekdb_result_column_name', 'int', ['void*', 'int32', 'void*', 'size_t']);
const seekdb_fetch_row = lib.func('seekdb_fetch_row', 'void*', ['void*']);
const seekdb_row_get_string = lib.func('seekdb_row_get_string', 'int', ['void*', 'int32', 'void*', 'size_t']);
const seekdb_row_get_int64 = lib.func('seekdb_row_get_int64', 'int', ['void*', 'int32', outInt64]);
const seekdb_row_get_double = lib.func('seekdb_row_get_double', 'int', ['void*', 'int32', outDouble]);
const seekdb_row_get_bool = lib.func('seekdb_row_get_bool', 'int', ['void*', 'int32', outBool]);
const seekdb_row_is_null = lib.func('seekdb_row_is_null', 'bool', ['void*', 'int32']);
const seekdb_result_free = lib.func('seekdb_result_free', 'void', ['void*']);
const seekdb_error = lib.func('seekdb_error', 'str', ['void*']);
const seekdb_errno = lib.func('seekdb_errno', 'uint32', ['void*']);
const seekdb_last_error = lib.func('seekdb_last_error', 'str', []);
const seekdb_last_error_code = lib.func('seekdb_last_error_code', 'int', []);
const seekdb_affected_rows = lib.func('seekdb_affected_rows', 'uint64', ['void*']);

// Error codes
const SEEKDB_SUCCESS = 0;
const SEEKDB_ERROR_INVALID_PARAM = -1;
const SEEKDB_ERROR_CONNECTION_FAILED = -2;
const SEEKDB_ERROR_QUERY_FAILED = -3;
const SEEKDB_ERROR_MEMORY_ALLOC = -4;
const SEEKDB_ERROR_NOT_INITIALIZED = -5;

class SeekdbConnection {
    constructor() {
        this.handle = null;
        this.connected = false;
    }

    connect(database, autocommit = false) {
        // Use array as output parameter container for koffi
        const handleOut = [null];
        const ret = seekdb_connect(handleOut, database, autocommit);

        if (ret !== SEEKDB_SUCCESS) {
            throw new Error(`Connection failed: ${ret}`);
        }

        this.handle = handleOut[0];
        this.connected = true;
    }

    begin() {
        if (!this.connected || !this.handle) {
            throw new Error('Not connected');
        }
        const ret = seekdb_begin(this.handle);
        if (ret !== SEEKDB_SUCCESS) {
            throw new Error(`Begin transaction failed: ${ret}`);
        }
    }

    commit() {
        if (!this.connected || !this.handle) {
            throw new Error('Not connected');
        }
        const ret = seekdb_commit(this.handle);
        if (ret !== SEEKDB_SUCCESS) {
            throw new Error(`Commit transaction failed: ${ret}`);
        }
    }

    rollback() {
        if (!this.connected || !this.handle) {
            throw new Error('Not connected');
        }
        const ret = seekdb_rollback(this.handle);
        if (ret !== SEEKDB_SUCCESS) {
            throw new Error(`Rollback transaction failed: ${ret}`);
        }
    }

    execute(sql) {
        if (!this.connected || !this.handle) {
            throw new Error('Not connected');
        }

        // Use array as output parameter container for koffi
        const resultOut = [null];
        const ret = seekdb_query(this.handle, sql, resultOut);

        if (ret !== SEEKDB_SUCCESS) {
            throw new Error(`Query execution failed: ${ret}`);
        }

        // Get stored result
        const storedResult = seekdb_store_result(this.handle);
        if (!storedResult) {
            throw new Error('Failed to store result');
        }

        return new SeekdbResult(storedResult);
    }

    executeUpdate(sql) {
        if (!this.connected || !this.handle) {
            throw new Error('Not connected');
        }

        // Execute query
        const resultOut = [null];
        const ret = seekdb_query(this.handle, sql, resultOut);

        if (ret !== SEEKDB_SUCCESS) {
            throw new Error(`Update execution failed: ${ret}`);
        }

        if (resultOut[0]) {
            seekdb_result_free(resultOut[0]);
        }

        // Get affected rows
        const affectedRows = seekdb_affected_rows(this.handle);
        return Number(affectedRows);
    }

    getLastError() {
        if (!this.handle) {
            return '';
        }
        const errorMsg = seekdb_error(this.handle);
        return errorMsg ? errorMsg.toString('utf8') : '';
    }

    getLastErrorCode() {
        // Get thread-local error code (no handle required)
        return seekdb_last_error_code();
    }

    getLastErrorThreadLocal() {
        // Get thread-local error message (no handle required)
        const errMsg = seekdb_last_error();
        return errMsg ? errMsg.toString('utf8') : '';
    }

    close() {
        if (this.handle) {
            seekdb_connect_close(this.handle);
            this.handle = null;
            this.connected = false;
        }
    }
}

class SeekdbResult {
    constructor(resultPtr) {
        this.resultPtr = resultPtr;
        this.rowCount = Number(seekdb_num_rows(resultPtr));
        this.columnCount = seekdb_num_fields(resultPtr);
        this.columnNames = [];

        // Get column names
        for (let i = 0; i < this.columnCount; i++) {
            const nameBuf = Buffer.alloc(256);
            seekdb_result_column_name(resultPtr, i, nameBuf, nameBuf.length);
            this.columnNames.push(nameBuf.toString('utf8').replace(/\0/g, ''));
        }
    }

    fetchAll() {
        const rows = [];

        while (true) {
            const rowHandle = seekdb_fetch_row(this.resultPtr);
            if (!rowHandle) {
                break;
            }

            const row = {};

            for (let i = 0; i < this.columnCount; i++) {
                const colName = this.columnNames[i];
                const isNull = seekdb_row_is_null(rowHandle, i);

                if (isNull) {
                    row[colName] = null;
                } else {
                    // Try to get as string first
                    const valueBuf = Buffer.alloc(4096);
                    const ret = seekdb_row_get_string(rowHandle, i, valueBuf, valueBuf.length);
                    if (ret === SEEKDB_SUCCESS) {
                        row[colName] = valueBuf.toString('utf8').replace(/\0/g, '');
                    } else {
                        row[colName] = null;
                    }
                }
            }

            rows.push(row);
        }

        return rows;
    }

    free() {
        if (this.resultPtr) {
            seekdb_result_free(this.resultPtr);
            this.resultPtr = null;  // Clear pointer to prevent double free
        }
    }
}

function open(dbDir) {
    const ret = seekdb_open(dbDir);
    if (ret !== SEEKDB_SUCCESS) {
        throw new Error(`Failed to open database: ${ret}`);
    }
}

function close() {
    seekdb_close();
}

module.exports = {
    open,
    close,
    SeekdbConnection,
    SeekdbResult
};
