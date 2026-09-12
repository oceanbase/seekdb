// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "lib/oblog/ob_log_compressor.h"
#include <cstdio>
#include <string>
using namespace oceanbase::common;
int main()
{
  ObSyslogCompareFunctor compare;
  ObSyslogPriorityArray files(compare);
  const std::string prefix = "C:\\日志%\\" + std::string(8000, 'x');
  for (int i = 0; i < 80; ++i) {
    ObSyslogFile candidate;
    candidate.mtime_ = (i * 37) % 80;
    candidate.file_name_ = prefix + std::to_string(candidate.mtime_);
    if (OB_SUCCESS != files.push(candidate) || files.count() > 20) { return 1; }
    candidate.file_name_ = "changed after insertion";
  }
  if (20 != files.count()) { return 2; }
  for (int i = 0; i < 20; ++i) {
    const ObSyslogFile *file = nullptr;
    if (OB_SUCCESS != files.top(file) || nullptr == file || file->mtime_ != i
        || file->name() != prefix + std::to_string(i)) { return 3; }
    if (OB_SUCCESS != files.pop()) { return 4; }
  }
  const ObSyslogFile *file = nullptr;
  if (OB_EMPTY_RESULT != files.top(file) || nullptr != file
      || OB_EMPTY_RESULT != files.pop()) { return 5; }
  ObSyslogFile candidate;
  candidate.file_name_ = prefix;
  if (OB_SUCCESS != files.push(candidate)) { return 6; }
  files.reset();
  if (0 != files.count()) { return 7; }
  std::puts("LOG_CANDIDATE_PASS oldest=20 scanned=80 owned_utf8_bytes=8000 reset=ok (no filesystem operations)");
  return 0;
}
