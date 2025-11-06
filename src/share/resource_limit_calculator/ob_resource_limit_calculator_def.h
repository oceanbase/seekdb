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

// DEF_RESOURCE_LIMIT_CALCULATOR(n, type, name, subhandler)
#ifdef DEF_RESOURCE_LIMIT_CALCULATOR
DEF_RESOURCE_LIMIT_CALCULATOR(1, LS, ls, MTL(ObLSService*))
DEF_RESOURCE_LIMIT_CALCULATOR(2, TABLET, tablet, MTL(ObTenantMetaMemMgr*)->get_t3m_limit_calculator())
#endif

// DEF_PHY_RES(n, type, name)
#ifdef DEF_PHY_RES
DEF_PHY_RES(1, MEMSTORE, memstore)
DEF_PHY_RES(2, MEMORY, memory)
DEF_PHY_RES(3, DATA_DISK, data_disk)
DEF_PHY_RES(4, CLOG_DISK, clog_disk)
DEF_PHY_RES(5, CPU, cpu)
#endif

// DEF_RESOURCE_CONSTRAINT(n, type, name)
#ifdef DEF_RESOURCE_CONSTRAINT
DEF_RESOURCE_CONSTRAINT(1, CONFIGURATION, configuration)
DEF_RESOURCE_CONSTRAINT(2, MEMSTORE, memstore)
DEF_RESOURCE_CONSTRAINT(3, MEMORY, memory)
DEF_RESOURCE_CONSTRAINT(4, DATA_DISK, data_disk)
DEF_RESOURCE_CONSTRAINT(5, CLOG_DISK, clog_disk)
DEF_RESOURCE_CONSTRAINT(6, CPU, cpu)
#endif
