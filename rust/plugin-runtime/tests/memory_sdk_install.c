/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
/* Compiled against the privately installed SDK, not the source include tree. */
#include "seekdb/plugin/memory_spi.h"
_Static_assert(offsetof(seekdb_plugin_host_api_v3_t, v2) == 0, "v2 prefix");
_Static_assert(offsetof(seekdb_plugin_host_api_v2_t, host) == 0, "v1 prefix");
_Static_assert(offsetof(seekdb_plugin_host_api_v3_t, memory_spi_major) == sizeof(seekdb_plugin_host_api_v2_t), "owned suffix");
_Static_assert(offsetof(seekdb_plugin_owned_bytes_v1_t, size) == 8, "owned byte size");
