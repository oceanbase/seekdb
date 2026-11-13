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
 * Exception thrown when SeekDB operations fail.
 */
public class SeekdbException extends RuntimeException {

    public SeekdbException(String message) {
        super(message);
    }

    public SeekdbException(String message, Throwable cause) {
        super(message, cause);
    }
}
