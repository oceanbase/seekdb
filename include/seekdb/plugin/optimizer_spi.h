/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_PLUGIN_OPTIMIZER_SPI_H_
#define SEEKDB_PLUGIN_OPTIMIZER_SPI_H_
#include "seekdb/plugin/seekdb_plugin_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Public around-planning hook; no C++ query/plan layout crosses this ABI.
 * This v1 supports observation and veto, not custom path/plan replacement.
 * Those richer capabilities require a separate version-bound server API. */
#define SEEKDB_PLUGIN_OPTIMIZER_HOOK_POINT "optimizer.plan.v1"
enum seekdb_plugin_optimizer_statement {
  SEEKDB_PLUGIN_OPTIMIZER_OTHER = 0, SEEKDB_PLUGIN_OPTIMIZER_SELECT = 1,
  SEEKDB_PLUGIN_OPTIMIZER_INSERT = 2, SEEKDB_PLUGIN_OPTIMIZER_UPDATE = 3,
  SEEKDB_PLUGIN_OPTIMIZER_DELETE = 4, SEEKDB_PLUGIN_OPTIMIZER_EXPLAIN = 5
};
typedef struct seekdb_plugin_optimizer_info_v1 {
  uint32_t struct_size;
  uint32_t statement_kind;
  uint64_t database_id;
  uint64_t user_id;
  uint64_t reserved[4];
} seekdb_plugin_optimizer_info_v1_t;
/* next is valid only during this synchronous callback, on this thread. It may
 * be called once. A failure is sticky; it cannot be swallowed by returning OK.
 * database_error preserves the exact host error (zero on success); callers
 * should propagate the returned plugin status without reinterpreting that code. */
typedef seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *seekdb_plugin_optimizer_next_v1_fn)(
    void *continuation, int32_t *database_error);
typedef struct seekdb_plugin_optimizer_context_v1 {
  uint32_t struct_size;
  const seekdb_plugin_optimizer_info_v1_t *info;
  void *continuation;
  seekdb_plugin_optimizer_next_v1_fn next;
  uint64_t reserved[4];
} seekdb_plugin_optimizer_context_v1_t;
typedef seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *seekdb_plugin_optimizer_invoke_v1_fn)(
    seekdb_plugin_instance_handle_t *, const seekdb_plugin_optimizer_context_v1_t *);
typedef struct seekdb_plugin_optimizer_service_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_optimizer_invoke_v1_fn invoke;
  uint64_t reserved[4];
} seekdb_plugin_optimizer_service_v1_t;

#ifdef __cplusplus
}
#endif
#endif
