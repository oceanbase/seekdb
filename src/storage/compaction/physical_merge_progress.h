/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef OCEANBASE_COMPACTION_PHYSICAL_MERGE_PROGRESS_H_
#define OCEANBASE_COMPACTION_PHYSICAL_MERGE_PROGRESS_H_
#include <stdint.h>

namespace oceanbase
{
namespace storage { class ObLS; }
namespace share { class ObSQLiteConnectionPool; }
namespace compaction
{
// Re-enumerates native physical objects after the replica is readable at target.
// Missing or obsolete reports never count as completed objects.
int check_physical_merge_progress(storage::ObLS &ls, share::ObSQLiteConnectionPool &reports,
    int64_t target, volatile bool &stop, bool &finished);
}
}
#endif
