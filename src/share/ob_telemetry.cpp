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

#include "lib/utility/utility.h"
#include "lib/time/ob_time_utility.h"
#include "lib/string/ob_sql_string.h"
#include "lib/cpu/ob_cpu_topology.h"
#include "share/config/ob_server_config.h"
#include "share/ob_encryption_util.h"
#include "common/ob_version_def.h"
#ifdef __APPLE__
#include <unistd.h>
#else
#include <curl/curl.h>
#endif

#define USING_LOG_PREFIX SHARE

namespace oceanbase
{
namespace share
{

static const char *TELEMETRY_URL = "https://openwebapi.oceanbase.com/api/web/oceanbase/report";
static const char *TELEMETRY_FILE_NAME = "run/telemetry.json";
static const int64_t TELEMETRY_VERSION = 1;

int get_host_hash(char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  int64_t out_len = 0;
  char ip_buf[MAX_IP_ADDR_LENGTH] = {'\0'};
  char hash_buf[SHA256_DIGEST_LENGTH + 1] = {'\0'};
  ObAddr addr = GCONF.self_addr_;
  if (!addr.ip_to_string(ip_buf, sizeof(ip_buf))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObHashUtil::hash(OB_HASH_SH256, ip_buf, strlen(ip_buf), hash_buf, sizeof(hash_buf), out_len))) {
    LOG_WARN("Failed to get host hash", K(ret));
  } else if (OB_FAIL(to_hex_cstr(hash_buf, out_len, buf, buf_len))) {
    LOG_WARN("Failed to hex host hash", K(ret));
  }
  return ret;
}

int generate_id(char *id, const int64_t id_len)
{
  int ret = OB_SUCCESS;
  int64_t out_len = 0;
  int64_t pos = 0;
  int64_t port = GCONF.mysql_port;
  ObAddr addr = GCONF.self_addr_;
  char buf[MAX_IP_PORT_LENGTH + OB_MAX_TIME_STR_LENGTH] = {'\0'};
  char hash_buf[SHA256_DIGEST_LENGTH + 1] = {'\0'};
  char hex_buf[SHA256_DIGEST_LENGTH * 2 + 1] = {'\0'};
  int64_t ts = ObTimeUtility::fast_current_time();
  addr.ip_to_string(buf, sizeof(buf));
  pos = strlen(buf);
  if (OB_FAIL(databuff_printf(buf, sizeof(buf) - pos, pos, "%ld%ld", port, ts))) {
    LOG_WARN("Failed to concat ip and ts", K(ret));
  } else if (OB_FAIL(ObHashUtil::hash(OB_HASH_SH256, buf, strlen(buf), hash_buf, sizeof(hash_buf), out_len))) {
    LOG_WARN("Failed to hash id", K(ret));
  } else if (OB_FAIL(to_hex_cstr(hash_buf, out_len, id, id_len))) {
    LOG_WARN("Failed to hex id", K(ret));
  }

  return ret;
}

int generate_telemetry_json(const char* reporter, const char* event_name, ObIAllocator *allocator, ObString &json_str)
{
  int ret = OB_SUCCESS;
  const int64_t SHA256_DIGEST_HEX_LEN = 2 * SHA256_DIGEST_LENGTH + 1;
  const int64_t OS_INFO_LEN = 32;
  const int64_t CPU_MODEL_LEN = 64;
  const int64_t SIZE_STR_LEN = 16;
  ObJsonObject root(allocator);
  ObJsonObject host(allocator);
  ObJsonObject instance(allocator);
  ObJsonObject resource(allocator);
  ObJsonObject content(allocator);
  char os_name[OS_INFO_LEN] = {'\0'};
  char os_version[OS_INFO_LEN] = {'\0'};
  char cpu_model[CPU_MODEL_LEN] = {'\0'};
  char host_hash[SHA256_DIGEST_HEX_LEN + 1] = {'\0'};
  char id[SHA256_DIGEST_HEX_LEN + 1] = {'\0'};
  int64_t ts = ObTimeUtility::fast_current_time();
  int64_t cpu_count = common::get_cpu_count();
  int64_t host_cpu_count = common::get_cpu_num();
  int64_t port = GCONF.mysql_port;
  char version[OB_SERVER_VERSION_LENGTH] = {'\0'};
  char memory_limit[SIZE_STR_LEN] = {'\0'};
  char host_memory_size[SIZE_STR_LEN] = {'\0'};
  char log_disk_size[SIZE_STR_LEN] = {'\0'};
  char datafile_size[SIZE_STR_LEN] = {'\0'};

  // construct report content
  double memory_limit_gb = static_cast<double>(lib::get_memory_limit()) / 1024 / 1024 / 1024;
  double host_memory_size_gb = static_cast<double>(common::get_phy_mem_size()) / 1024 / 1024 / 1024;
  double log_disk_size_gb = static_cast<double>(GCONF.log_disk_size) / 1024 / 1024 / 1024;
  double datafile_size_gb = static_cast<double>(GCONF.datafile_size) / 1024 / 1024 / 1024;
  snprintf(memory_limit, sizeof(memory_limit), "%.9gG", memory_limit_gb);
  snprintf(host_memory_size, sizeof(host_memory_size), "%.9gG", host_memory_size_gb);
  snprintf(log_disk_size, sizeof(log_disk_size), "%.9gG", log_disk_size_gb);
  snprintf(datafile_size, sizeof(datafile_size), "%.9gG", datafile_size_gb);
  VersionUtil::print_version_str(version, sizeof(version), CLUSTER_CURRENT_VERSION);
  get_host_hash(host_hash, sizeof(host_hash));
  get_os_info(os_name, sizeof(os_name), os_version, sizeof(os_version));
  get_cpu_model(cpu_model, sizeof(cpu_model));
  generate_id(id, sizeof(id));

  // construct host
  ObJsonString os_json(os_name);
  ObJsonString os_version_json(os_version);
  ObJsonString cpu_json(cpu_model);
  ObJsonInt host_cpu_count_json(host_cpu_count);
  ObJsonString host_memory_size_json(host_memory_size);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(host.add("os", &os_json))) {
  } else if (OB_FAIL(host.add("osVersion", &os_version_json))) {
  } else if (OB_FAIL(host.add("cpu", &cpu_json))) {
  } else if (OB_FAIL(host.add("cpuCount", &host_cpu_count_json))) {
  } else if (OB_FAIL(host.add("memorySize", &host_memory_size_json))) {
  }

  // construct instance
  ObJsonString host_hash_json(host_hash);
  ObJsonInt port_json(port);
  ObJsonInt timestamp_json(ts);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(instance.add("hostHash", &host_hash_json))) {
  } else if (OB_FAIL(instance.add("port", &port_json))) {
  } else if (OB_FAIL(instance.add("createTimestamp", &timestamp_json))) {
  }

  // construct resource
  ObJsonInt cpu_count_json(cpu_count);
  ObJsonString memory_limit_json(memory_limit);
  ObJsonString log_disk_size_json(log_disk_size);
  ObJsonString datafile_size_json(datafile_size);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(resource.add("cpuCount", &cpu_count_json))) {
  } else if (OB_FAIL(resource.add("memoryLimit", &memory_limit_json))) {
  } else if (OB_FAIL(resource.add("logDiskSize", &log_disk_size_json))) {
  } else if (OB_FAIL(resource.add("dataFileSize", &datafile_size_json))) {
  }

  // construct content
  ObJsonString id_json(id);
  ObJsonString version_json(version);
  ObJsonString reporter_json(reporter);
  ObJsonString event_json(event_name);
  ObJsonInt telemetry_version_json(TELEMETRY_VERSION);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(content.add("id", &id_json))) {
  } else if (OB_FAIL(content.add("version", &version_json))) {
  } else if (OB_FAIL(content.add("reporter", &reporter_json))) {
  } else if (OB_FAIL(content.add("host", &host))) {
  } else if (OB_FAIL(content.add("instance", &instance))) {
  } else if (OB_FAIL(content.add("resource", &resource))) {
  } else if (OB_FAIL(content.add("event", &event_json))) {
  } else if (OB_FAIL(content.add("telemetryVersion", &telemetry_version_json))) {
  }

  // construct root
  ObJsonString component_json(OB_SEEKDB_NAME);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(root.add("content", &content))) {
  } else if (OB_FAIL(root.add("component", &component_json))) {
  }

  ObJsonBuffer j_buf(allocator);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(root.print(j_buf, false))) {
    LOG_WARN("Failed to get json string", K(ret));
  } else {
    json_str.assign_ptr(j_buf.ptr(), j_buf.length());
  }

  if (OB_SUCC(ret)) {
    FILE *fp = fopen(TELEMETRY_FILE_NAME, "w");
    if (OB_NOT_NULL(fp)) {
      if (json_str.length() != fwrite(json_str.ptr(), 1, json_str.length(), fp)) {
        ret = OB_IO_ERROR;
        LOG_WARN("Failed to write telemetry to file", K(ret));
      }
      fclose(fp);
    }
  }

  return ret;
}

// macOS: Use system curl command to avoid libcurl ssl issues
int send_telemetry_by_curl_cmd(const char *url)
{
  int ret = OB_SUCCESS;
  // Use existing run/telemetry.json file which is already written by generate_telemetry_json()
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "curl -s -X POST -H \"Content-Type: application/json\" "
           "-d @%s --connect-timeout 10 -m 15 \"%s\" >/dev/null 2>&1",
           TELEMETRY_FILE_NAME, url);

  int sys_ret = system(cmd);
  if (sys_ret != 0) {
    ret = OB_CURL_ERROR;
    LOG_ERROR("Failed to execute curl command for telemetry", K(ret), K(sys_ret), K(url));
  }
  return ret;
}
// Linux: Use libcurl
static size_t discard(void* ptr, size_t size, size_t nmemb, void* userdata) {
  return size * nmemb;
}

int send_telemetry_by_libcurl(const char *url, const ObString &json_str)
{
  int ret = OB_SUCCESS;
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    LOG_WARN("Failed to init curl");
    ret = OB_CURL_ERROR;
  } else {
    CURLcode cc = CURLE_OK;
    struct curl_slist *list = NULL;
    // set post options
    if (NULL == (list = curl_slist_append(list, "Content-Type: application/json"))) {
      ret = OB_CURL_ERROR;
      LOG_WARN("append list failed", K(ret));
    } else {
      curl_easy_setopt(curl, CURLOPT_URL, url);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_str.length());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.ptr());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);

      // set other options
      const int64_t no_signal = 1;
      const int64_t timeout_ms = 1000; // 1s
      const int64_t no_delay = 1;
      const int64_t max_redirect = 3; // set max redirect
      const int64_t follow_location = 1; // for http redirect 301 302
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, no_signal);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
      curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, no_delay);
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, max_redirect);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_location);

      // send request and do not care about the http code
      if (CURLE_OK != (cc = curl_easy_perform(curl))) {
        LOG_WARN("Failed to perform curl", K(cc));
        ret = OB_CURL_ERROR;
      }
      curl_slist_free_all(list);
    }
    curl_easy_cleanup(curl);
  }
  return ret;
}

int send_telemetry(const char *url, const ObString &json_str)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(url) || json_str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), KP(url), K(json_str));
  } else {
#ifdef __APPLE__
    // macOS uses the telemetry file directly, json_str already written to TELEMETRY_FILE_NAME
    ret = send_telemetry_by_curl_cmd(url);
#else
    ret = send_telemetry_by_libcurl(url, json_str);
#endif
  }
  return ret;
}

bool is_telemetry_enabled()
{
  bool bret = true;
  const char* telemetry_enabled = getenv("TELEMETRY_ENABLED");
  if (NULL != telemetry_enabled && 0 == STRCMP(telemetry_enabled, "false")) {
    bret = false;
  }
  return bret;
}

int report_telemetry(const char *reporter, const char *event_name)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator;
  ObString json_str;
  if (OB_FAIL(generate_telemetry_json(reporter, event_name, &allocator, json_str))) {
    LOG_WARN("Failed to generate telemetry json", K(ret));
  } else if (is_telemetry_enabled()
             && OB_FAIL(send_telemetry(TELEMETRY_URL, json_str))) {
    LOG_WARN("Failed to send telemetry", K(ret));
  }
  return ret;
}

}
}
