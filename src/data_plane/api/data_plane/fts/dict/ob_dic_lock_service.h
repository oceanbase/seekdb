/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_FTS_DICT_OB_DIC_LOCK_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_FTS_DICT_OB_DIC_LOCK_SERVICE_H_

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace storage
{
class ObDicLoader;
}
namespace data_plane
{

// Public dictionary-lock capability.  Lock modes and table-lock machinery are
// intentionally kept inside the data plane.
class ObDictionaryLockService
{
public:
  static int lock_tables_shared_in_transaction(
      const storage::ObDicLoader &loader,
      common::ObMySQLTransaction &trans);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_FTS_DICT_OB_DIC_LOCK_SERVICE_H_
