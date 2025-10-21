/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _OB_TABLET_LOCATION_PROXY_H
#define _OB_TABLET_LOCATION_PROXY_H 1
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "lib/net/ob_addr.h"
#include "share/location_cache/ob_location_struct.h"
namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
namespace sqlclient
{
class ObMySQLResult;
}
} // end namespace common
namespace table
{
class ObITabletLocationGetter
{
public:
  ObITabletLocationGetter() = default;
  virtual ~ObITabletLocationGetter() = default;
  virtual int get_tablet_location(const common::ObString &tenant,
                                  const uint64_t tenant_id,
                                  const common::ObString &db,
                                  const common::ObString &table,
                                  const uint64_t table_id,
                                  const common::ObTabletID tablet_id,
                                  bool force_renew,
                                  share::ObTabletLocation &location) = 0;
};

typedef share::ObLSReplicaLocation ObTabletReplicaLocation;
class ObTabletLocationProxy: public ObITabletLocationGetter
{
public:
  ObTabletLocationProxy()
      :sql_client_(NULL)
  {}
  virtual ~ObTabletLocationProxy() = default;
  int init(common::ObMySQLProxy &proxy);
  virtual int get_tablet_location(const common::ObString &tenant,
                                  const uint64_t tenant_id,
                                  const common::ObString &db,
                                  const common::ObString &table,
                                  const uint64_t table_id,
                                  const common::ObTabletID tablet_id,
                                  bool force_renew,
                                  share::ObTabletLocation &location) override;
private:
  bool inited() const;
  int cons_replica_location(const common::sqlclient::ObMySQLResult &res,
                            ObTabletReplicaLocation &replica_location);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTabletLocationProxy);
private:
  common::ObMySQLProxy *sql_client_;
};

class ObTabletLocationCache: public ObITabletLocationGetter
{
public:
  ObTabletLocationCache()
      :is_inited_(false),
       location_proxy_(NULL),
       location_cache_(),
       sem_()
  {}
  virtual ~ObTabletLocationCache();
  int init(ObTabletLocationProxy &location_proxy);

  virtual int get_tablet_location(const common::ObString &tenant,
                                  const uint64_t tenant_id,
                                  const common::ObString &db,
                                  const common::ObString &table,
                                  const uint64_t table_id,
                                  const common::ObTabletID tablet_id,
                                  bool force_renew,
                                  share::ObTabletLocation &location) override;
private:
  typedef share::ObTabletLSCacheKey ObTabletLocationCacheKey; 
  typedef common::ObKVCache<ObTabletLocationCacheKey, share::ObTabletLocation> KVCache;
  static const int64_t DEFAULT_FETCH_LOCATION_TIMEOUT_US = 3 * 1000 * 1000;    //3s
  static const int64_t LOCATION_RENEW_CONCURRENCY = 10;
  constexpr static const char * const CACHE_NAME = "TABLE_API_LOCATION_CACHE";
  static const int64_t CACHE_PRIORITY = 1000;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTabletLocationCache);
  // function members
  int get_from_cache(const uint64_t tenant_id,
                     const common::ObTabletID tablet_id,
                     share::ObTabletLocation &result);
  int put_to_cache(const uint64_t tenant_id,
                   const common::ObTabletID tablet_id,
                   const share::ObTabletLocation &location);
  int renew_get_tablet_location(const common::ObString &tenant,
                                const uint64_t tenant_id,
                                const common::ObString &db,
                                const common::ObString &table,
                                const uint64_t table_id,
                                const common::ObTabletID tablet_id,
                                share::ObTabletLocation &location);
private:
  // data members
  bool is_inited_;
  ObTabletLocationProxy *location_proxy_;
  KVCache location_cache_;
  share::ObLocationSem sem_;
};

} // end namespace table
} // end namespace oceanbase

#endif /* _OB_TABLET_LOCATION_PROXY_H */
