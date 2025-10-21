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

class BytesLimiter: public ITCLimiter
{
public:
  BytesLimiter(int id, const char* name): ITCLimiter(id, TCLIMIT_BYTES, name, 0) {}
  BytesLimiter(int id, const char* name, uint64_t storage_key): ITCLimiter(id, TCLIMIT_BYTES, name, storage_key) {}
  ~BytesLimiter() {}
  int64_t get_cost(TCRequest* req) { return (req->norm_bytes_ == 0) ? req->bytes_ : req->norm_bytes_; }
};

class CountLimiter: public ITCLimiter
{
public:
  CountLimiter(int id, const char* name): ITCLimiter(id, TCLIMIT_COUNT, name) {}
  CountLimiter(int id, const char* name, uint64_t storage_key): ITCLimiter(id, TCLIMIT_COUNT, name, storage_key) {}
  ~CountLimiter() {}
  int64_t get_cost(TCRequest* req) { return 1; }
};
