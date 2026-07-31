/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_READ_TIMESTAMP_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_READ_TIMESTAMP_SERVICE_H_

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace data_plane
{

class ObIReadTimestampService
{
public:
  virtual ~ObIReadTimestampService() {}
  virtual int latest_read_scn(share::SCN &scn) = 0;
  virtual bool is_external_consistent() = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_READ_TIMESTAMP_SERVICE_H_
