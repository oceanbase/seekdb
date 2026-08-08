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
 * Connection to a SeekDB database.
 * Obtain via connect() after Seekdb.open().
 */
public class SeekdbConnection {

    private long handle;

    /**
     * Connect to a database.
     *
     * @param database  Database name
     * @param autocommit Autocommit mode
     * @throws SeekdbException if connection fails
     */
    public native void connect(String database, boolean autocommit);

    /**
     * Close the connection.
     */
    public native void close();

    /**
     * Execute a SELECT query and return the result set.
     *
     * @param sql SQL query
     * @return Result set
     * @throws SeekdbException if query fails
     */
    public native SeekdbResult query(String sql);

    /**
     * Execute an INSERT/UPDATE/DELETE/DDL statement.
     *
     * @param sql SQL statement
     * @return Number of affected rows
     * @throws SeekdbException if execution fails
     */
    public native long executeUpdate(String sql);

    /**
     * Begin a transaction.
     *
     * @throws SeekdbException if begin fails
     */
    public native void begin();

    /**
     * Commit the current transaction.
     *
     * @throws SeekdbException if commit fails
     */
    public native void commit();

    /**
     * Rollback the current transaction.
     *
     * @throws SeekdbException if rollback fails
     */
    public native void rollback();

    /**
     * Get the last error message for this connection.
     *
     * @return Error message or null if none
     */
    public native String getLastError();

    long getHandle() {
        return handle;
    }

    void setHandle(long h) {
        this.handle = h;
    }
}
