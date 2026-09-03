/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

package seekdb;

/**
 * Static interface for SeekDB embedded database lifecycle.
 * Must call open() before creating connections, close() when done.
 */
public final class Seekdb {

    static {
        System.loadLibrary("seekdb_jni");
    }

    private Seekdb() {}

    /**
     * Open the embedded database.
     *
     * @param dbDir Database directory path
     * @throws SeekdbException if open fails
     */
    public static native void open(String dbDir);

    /**
     * Close the embedded database.
     */
    public static native void close();
}
