/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef OCEANBASE_STORAGE_RUNTIME_OB_I_SERVER_RUNTIME_H_
#define OCEANBASE_STORAGE_RUNTIME_OB_I_SERVER_RUNTIME_H_

namespace oceanbase
{
namespace omt
{
struct ObServerRuntimeMeta;
}
namespace storage
{
struct ObServerRuntimeSuperBlock;

// Storage's narrow view of the process-owned runtime. Observer owns and
// injects the implementation so Storage never depends on concrete OMT types.
class ObIServerRuntime
{
public:
  virtual ~ObIServerRuntime() = default;

  virtual int create_runtime(
      const omt::ObServerRuntimeMeta &meta, bool write_slog) = 0;
  virtual void set_synced() = 0;
  virtual int get_runtime_meta_for_ckpt(
      omt::ObServerRuntimeMeta &meta, bool &exist) = 0;
  virtual bool has_runtime() const = 0;
  virtual int get_server_log_disk_size(int64_t &log_disk_size) = 0;
  virtual ObServerRuntimeSuperBlock get_super_block() = 0;
  virtual void set_server_super_block(
      const ObServerRuntimeSuperBlock &super_block) = 0;
  virtual bool is_hidden() = 0;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_RUNTIME_OB_I_SERVER_RUNTIME_H_
