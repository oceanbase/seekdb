/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include "seekdb/plugin/seekdb_plugin_abi.h"

/* This test fixture intentionally omits seekdb_plugin_entry_v1. */
SEEKDB_PLUGIN_EXPORT int SEEKDB_PLUGIN_CALL
seekdb_reference_missing_plugin_entry(void)
{
  return 0;
}
