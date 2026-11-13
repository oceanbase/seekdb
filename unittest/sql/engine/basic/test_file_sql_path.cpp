/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "sql/engine/basic/file_sql/file_sql_path.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

using namespace oceanbase::sql::file_sql;
namespace {
size_t checks = 0;
void require(bool value, const char *expression, int line)
{
  ++checks;
  if (!value) { std::cerr << "FAIL line " << line << ": " << expression << '\n'; std::exit(1); }
}
}
#define CHECK(x) require((x),#x,__LINE__)

namespace {

std::string make_temp_dir()
{
  char pattern[] = "/tmp/file_sql_path_XXXXXX";
  const char *dir = ::mkdtemp(pattern);
  if (nullptr == dir) { std::cerr << "cannot create temp dir\n"; std::exit(1); }
  return std::string(dir);
}

void write_file(const std::string &path, const std::string &content)
{
  std::ofstream out(path.c_str(), std::ios::binary);
  out << content;
}

} // namespace

int main()
{
  // The boundary of the allowed directory must be a separator.
  CHECK(path_is_under("/tmp/secure", "/tmp/secure"));
  CHECK(path_is_under("/tmp/secure", "/tmp/secure/file.csv"));
  CHECK(path_is_under("/tmp/secure/", "/tmp/secure/file.csv"));
  CHECK(!path_is_under("/tmp/secure", "/tmp/secure_evil/file.csv"));
  CHECK(!path_is_under("/tmp/secure", "/tmp/other/file.csv"));
  CHECK(!path_is_under("/tmp/secure", "/tmp"));
  CHECK(!path_is_under("", "/tmp/secure/file.csv"));

  // Lexical canonicalization collapses the relative parts without touching the
  // file system.
  std::string canonical;
  CHECK(lexical_canonicalize_path("/tmp/./secure/../secure/file.csv", canonical));
  CHECK(canonical == "/tmp/secure/file.csv");
  CHECK(lexical_canonicalize_path("a/b/../../c", canonical));
  CHECK(canonical == "c");
  CHECK(lexical_canonicalize_path("/../../etc/passwd", canonical));
  CHECK(canonical == "/etc/passwd");
  CHECK(!lexical_canonicalize_path("", canonical));

  const std::string base = make_temp_dir();
  const std::string secure_dir = base + "/secure";
  const std::string outside_dir = base + "/outside";
  CHECK(0 == ::mkdir(secure_dir.c_str(), 0700));
  CHECK(0 == ::mkdir(outside_dir.c_str(), 0700));
  const std::string inside_file = secure_dir + "/inside.csv";
  const std::string outside_file = outside_dir + "/secret.csv";
  write_file(inside_file, "id\n1\n");
  write_file(outside_file, "id\n1\n");

  // A file inside the allowed directory is accepted and canonicalized.
  std::string resolved;
  CHECK(check_secure_path(secure_dir, inside_file, resolved) == PathCode::OK);
  CHECK(resolved == inside_file);

  // A file outside of the allowed directory is rejected.
  CHECK(check_secure_path(secure_dir, outside_file, resolved) == PathCode::ACCESS_DENIED);
  CHECK(resolved.empty());

  // A path that does not exist is reported as NOT_FOUND, not as a denial.
  CHECK(check_secure_path(secure_dir, secure_dir + "/missing.csv", resolved) == PathCode::NOT_FOUND);

  // A directory is not a readable table.
  CHECK(check_secure_path(secure_dir, secure_dir, resolved) == PathCode::NOT_REGULAR_FILE);

  // An empty path and an unusable secure_file_priv are rejected.
  CHECK(check_secure_path(secure_dir, "", resolved) == PathCode::EMPTY_PATH);
  CHECK(check_secure_path("", inside_file, resolved) == PathCode::ACCESS_DENIED);
  CHECK(check_secure_path("NULL", inside_file, resolved) == PathCode::ACCESS_DENIED);

  // A symbolic link inside the allowed directory that points outside of it is
  // rejected, the link target decides.
  const std::string link_path = secure_dir + "/link.csv";
  CHECK(0 == ::symlink(outside_file.c_str(), link_path.c_str()));
  CHECK(check_secure_path(secure_dir, link_path, resolved) == PathCode::ACCESS_DENIED);

  // A symbolic link that stays inside the allowed directory is accepted.
  const std::string inner_link = secure_dir + "/inner_link.csv";
  CHECK(0 == ::symlink(inside_file.c_str(), inner_link.c_str()));
  CHECK(check_secure_path(secure_dir, inner_link, resolved) == PathCode::OK);
  CHECK(resolved == inside_file);

  // glob() only reports the matches of the allowed directory. The escaping
  // link makes the whole expansion fail, a partial result would hide it.
  std::vector<std::string> files;
  CHECK(expand_glob_pattern(secure_dir + "/*.csv", secure_dir, files) == PathCode::ACCESS_DENIED);
  CHECK(files.empty());

  const std::string glob_dir = base + "/glob_dir";
  CHECK(0 == ::mkdir(glob_dir.c_str(), 0700));
  write_file(glob_dir + "/a.csv", "id\n1\n");
  write_file(glob_dir + "/b.csv", "id\n2\n");
  CHECK(0 == ::symlink((glob_dir + "/a.csv").c_str(), (glob_dir + "/c.csv").c_str()));
  CHECK(expand_glob_pattern(glob_dir + "/*.csv", glob_dir, files) == PathCode::OK);
  CHECK(files.size() == 3);
  for (const std::string &file : files) {
    CHECK(path_is_under(glob_dir, file));
  }

  // A pattern that matches a file outside of the allowed directory fails as a
  // whole instead of returning a partial result.
  CHECK(expand_glob_pattern(base + "/*/*.csv", secure_dir, files) == PathCode::ACCESS_DENIED);
  CHECK(files.empty());

  // A pattern without a match is reported as NOT_FOUND.
  CHECK(expand_glob_pattern(secure_dir + "/nothing-*.csv", secure_dir, files) == PathCode::NOT_FOUND);
  CHECK(files.empty());

  std::cerr << checks << " checks passed\n";
  return 0;
}
