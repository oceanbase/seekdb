/* Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0 */
#ifndef SEEKDB_NIO_MEMORY_H
#define SEEKDB_NIO_MEMORY_H

#include "nio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Available only in sql-nio's memory-transport build (without default features).
 * Start with nio_start(..., tls = NULL, tls_size = 0, disable_tcp = 1).
 * The address is validated for ABI consistency but no endpoint is opened.
 * The existing reactor, MySQL parser, and request/response lifecycle are used.
 * on_connect receives fd = -1 and is_unix = 1 (trusted in-process transport).
 * TLS configuration is rejected with NIO_START_ETLS.
 */
typedef struct MemoryClient nio_memory_client;

/* Enqueue a connection. capacity is the byte limit of EACH direction, in
 * [1, 16 MiB]. Returns NULL on invalid arguments, a stopped reactor, or a full
 * admission queue. A non-NULL result does not guarantee session admission:
 * on_connect can still reject it, in which case read eventually returns EOF.
 * reactor must remain live during this call; destruction must not race it.
 */
nio_memory_client *nio_memory_connect(const nio_reactor *reactor, size_t capacity);

/* Nonblocking, copying byte IO. Positive = bytes transferred (may be partial),
 * -2 = would block, -1 = invalid argument or closed/error. read returns 0 at EOF
 * after draining queued output. A zero-length operation returns 0; its buffer
 * may be NULL. Neither function retains the caller's buffer.
 * Calls can run concurrently while client remains live; ordering concurrent
 * writes, reads, and MySQL packet sequences is the caller's responsibility.
 * A client can outlive nio_wait_destroy and drain already delivered output.
 */
int64_t nio_memory_write(const nio_memory_client *client, const char *buffer, int64_t length);
int64_t nio_memory_read(const nio_memory_client *client, char *buffer, int64_t length);

/* Close and release exactly once. NULL is allowed. No other call using this
 * client may overlap close. Pending worker IO is cancelled by the reactor.
 */
void nio_memory_close(nio_memory_client *client);

#ifdef __cplusplus
}
#endif
#endif
