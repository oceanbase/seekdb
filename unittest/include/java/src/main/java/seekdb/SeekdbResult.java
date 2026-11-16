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

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Result set from a SELECT query.
 * Contains row count, column names, and row data.
 */
public class SeekdbResult {

    private long rowCount;
    private int columnCount;
    private String[] columns;
    private String[][] rows;

    void setRowCount(long count) {
        this.rowCount = count;
    }

    void setColumnCount(int count) {
        this.columnCount = count;
    }

    void setColumns(String[] cols) {
        this.columns = cols != null ? cols : new String[0];
    }

    void setRows(String[][] r) {
        this.rows = r != null ? r : new String[0][];
    }

    public long getRowCount() {
        return rowCount;
    }

    public int getColumnCount() {
        return columnCount;
    }

    public String[] getColumns() {
        return columns != null ? columns : new String[0];
    }

    /**
     * Fetch all rows as array of arrays.
     * Each row is String[]; null entry indicates SQL NULL.
     *
     * @return Rows
     */
    public String[][] fetchAll() {
        return rows != null ? rows : new String[0][];
    }

    /**
     * Fetch all rows as list of maps (column name -> value).
     *
     * @return List of row maps
     */
    public List<Map<String, String>> fetchAllAsMaps() {
        if (rows == null || columns == null) {
            return Collections.emptyList();
        }
        List<Map<String, String>> result = new ArrayList<>(rows.length);
        for (String[] row : rows) {
            Map<String, String> map = new HashMap<>();
            for (int i = 0; i < columns.length && i < row.length; i++) {
                map.put(columns[i], row[i]);
            }
            result.add(map);
        }
        return result;
    }
}
