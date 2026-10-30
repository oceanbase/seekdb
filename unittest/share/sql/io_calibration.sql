-- Copyright (c) 2026 OceanBase.
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

ALTER SYSTEM REFRESH IO CALIBRATION CALIBRATION_INFO=('WRITE:32K:17us:50.9','READ:32K:7us:100.8','READ:4K:1us:200.2','WRITE:4K:2us:150.3');
SELECT storage_name, mode, size, iops, latency FROM oceanbase.__all_virtual_io_benchmark ORDER BY mode, size;
ALTER SYSTEM REFRESH IO CALIBRATION;
SELECT COUNT(*), SUM(size), SUM(iops), SUM(latency) FROM oceanbase.__all_virtual_io_benchmark;
ALTER SYSTEM REFRESH IO CALIBRATION CALIBRATION_INFO=('');
SELECT COUNT(*) FROM oceanbase.__all_virtual_io_benchmark;
