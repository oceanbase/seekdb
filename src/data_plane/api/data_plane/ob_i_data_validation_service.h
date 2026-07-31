/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_DATA_VALIDATION_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_DATA_VALIDATION_SERVICE_H_

namespace oceanbase
{
namespace data_plane
{

class ObIDataValidationService
{
public:
  virtual ~ObIDataValidationService() {}
  virtual void delay_resource_recycle() = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_DATA_VALIDATION_SERVICE_H_
