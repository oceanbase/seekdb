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

 #ifndef OCEANBASE_OBSERVER_OB_SHOW_CREATE_LOCATION_
 #define OCEANBASE_OBSERVER_OB_SHOW_CREATE_LOCATION_
 #include "lib/container/ob_se_array.h"
 #include "share/ob_virtual_table_scanner_iterator.h"
 #include "common/ob_range.h"

 namespace oceanbase
 {
 namespace common
 {
 class ObString;
 }
 namespace observer
 {
 class ObShowCreateLocation : public common::ObVirtualTableScannerIterator
 {
 public:
   ObShowCreateLocation();
   virtual ~ObShowCreateLocation();
   virtual int inner_get_next_row(common::ObNewRow *&row);
   virtual void reset();
 private:
   int calc_show_location_id(uint64_t &show_location_id);
   int fill_row_cells(uint64_t show_location_id, const common::ObString &location_name);
 private:
   DISALLOW_COPY_AND_ASSIGN(ObShowCreateLocation);
 };
 }
 }
 #endif /* OCEANBASE_OBSERVER_OB_SHOW_CREATE_LOCATION_ */
