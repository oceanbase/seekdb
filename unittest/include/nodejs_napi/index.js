/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

const addon = require('./build/Release/seekdb.node');

class SeekdbConnection {
    constructor() {
        this._handle = null;
    }

    connect(database, autocommit = false) {
        this._handle = addon.connect(database, autocommit);
        if (!this._handle) {
            throw new Error('Connection failed');
        }
    }

    execute(sql) {
        if (!this._handle) {
            throw new Error('Not connected');
        }
        return addon.execute(this._handle, sql);
    }

    close() {
        if (this._handle) {
            addon.connectClose(this._handle);
            this._handle = null;
        }
    }
}

module.exports = {
    open: addon.open,
    close: addon.close,
    SeekdbConnection,
    ERROR_CODES: addon.ERROR_CODES
};
