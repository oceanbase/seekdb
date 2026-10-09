/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef OCEANBASE_ROOTSERVER_NAMESPACE_FREEZE_PREPARATION_H_
#define OCEANBASE_ROOTSERVER_NAMESPACE_FREEZE_PREPARATION_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase {
namespace storage { class InstanceMetaStore; }
namespace rootserver {

// A fresh directory view and local physical state, never a saved readiness
// cache. Background materialization and takeover retain ownership of the work.
class NamespaceFreezePreparation final
{
public:
  struct Status {
    bool ready = true;
    uint64_t namespace_id = 0;
    uint64_t tablet_id = 0;
    int64_t inspected_tablets = 0;
    bool needs_materialization = false;
    TO_STRING_KV(K(ready), K(namespace_id), K(tablet_id), K(inspected_tablets), K(needs_materialization));
  };
  // Stops at the first unprepared binding. A failed read is an error, not
  // evidence that a Namespace/tablet disappeared or finished its baseline.
  static int check(storage::InstanceMetaStore &store, int64_t deadline, Status &status);
};

} // namespace rootserver
} // namespace oceanbase
#endif
