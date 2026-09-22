/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_PLUGIN_MEMORY_SPI_H_
#define SEEKDB_PLUGIN_MEMORY_SPI_H_
#include "seekdb/plugin/extension_spi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Exclusive owned bytes, NOT a plugin-code/task lease. The host retains the
 * memory account independently of the originating module. Data and the host's
 * release callback remain valid until release(owner), including after module
 * deinit. Release is thread-safe and must be called exactly once after all byte
 * users end; it consumes owner and must not unwind or invoke plugin code.
 * Do not use host.free/Rust Vec/free/delete on data. Mutations require exclusive
 * byte access. The host code containing release must itself remain mapped until
 * all tokens are released (including when embedding seekdb).
 * A plugin must still drain its code/tasks before deinit: holding bytes alone
 * does not authorize calling an unloaded plugin or retaining query pointers. */
typedef void (SEEKDB_PLUGIN_CALL *seekdb_plugin_owned_bytes_release_fn)(void *owner);
typedef struct seekdb_plugin_owned_bytes_v1 {
  uint32_t struct_size;
  uint32_t alignment;
  uint64_t size;
  uint8_t *data;
  void *owner;
  seekdb_plugin_owned_bytes_release_fn release;
  uint64_t reserved[4];
} seekdb_plugin_owned_bytes_v1_t;

/* Borrow host only for the call, under existing lifecycle/execution authority.
 * A successful allocation is uninitialized, exclusive, and charged to the
 * module's same host allocator quota. Zero/invalid layouts are rejected.
 * A non-null output points to a writable full v1 descriptor; host clears it on
 * failure and produces exactly one owned token on success. */
typedef seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *seekdb_plugin_allocate_owned_bytes_fn)(
    seekdb_plugin_host_handle_t *host, uint64_t size, uint32_t alignment,
    seekdb_plugin_owned_bytes_v1_t *output);
typedef struct seekdb_plugin_host_api_v3 {
  seekdb_plugin_host_api_v2_t v2;
  uint32_t memory_spi_major;
  uint32_t memory_spi_minor;
  seekdb_plugin_allocate_owned_bytes_fn allocate_owned_bytes;
  uint64_t memory_reserved[4];
} seekdb_plugin_host_api_v3_t;

#ifdef __cplusplus
}
#endif
#endif
