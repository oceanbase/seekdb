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

SELECT COUNT(*) FROM oceanbase.__all_virtual_io_benchmark;
SELECT is_nullable FROM information_schema.columns WHERE table_schema='oceanbase' AND table_name='__all_virtual_group_io_stat' AND column_name='norm_iops';
SELECT column_name, is_nullable FROM information_schema.columns WHERE table_schema='oceanbase' AND table_name='__all_virtual_io_quota' AND column_name IN ('min_iops','max_iops','min_mbps','max_mbps') ORDER BY column_name;
SELECT column_name, is_nullable FROM information_schema.columns WHERE table_schema='oceanbase' AND table_name='__all_virtual_group_io_stat' AND column_name IN ('min_iops','max_iops') ORDER BY column_name;
