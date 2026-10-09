#!/usr/bin/env python3
"""Local test only: stop between SSTable metadata and macro-range RPCs."""
import argparse
import re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=('enable','disable'));a=p.parse_args()
target=Path(__file__).resolve().parents[2]/'src/standby/restore/ob_standby_sstable_copier.cpp'
s=target.read_text()
s=re.sub(r'// LOCAL_STANDBY_COPY_PAUSE_BEGIN\n.*?// LOCAL_STANDBY_COPY_PAUSE_END\n','',s,flags=re.S)
if a.action=='enable':
    anchor='  bool tablet_info_exist = false;\n'
    assert s.count(anchor)==1
    hook='''// LOCAL_STANDBY_COPY_PAUSE_BEGIN
  if (const char *control = getenv("SEEKDB_STANDBY_COPY_PAUSE")) {
    FILE *file = fopen(control, "r");
    unsigned long physical = 0;
    if (file) { (void)fscanf(file, "%lu", &physical); fclose(file); }
    if (physical == tablet_id.id()) {
      const std::string ready = std::string(control) + ".ready";
      file = fopen(ready.c_str(), "w");
      if (file) { fprintf(file, "%lu\\n", physical); fclose(file); }
      const int64_t until = ObTimeUtility::current_time() + 240 * 1000 * 1000L;
      while (access(control, F_OK) == 0 && ObTimeUtility::current_time() < until) { usleep(10000); }
      if (access(control, F_OK) == 0) { return OB_TIMEOUT; }
    }
  }
// LOCAL_STANDBY_COPY_PAUSE_END
'''
    s=s.replace(anchor,hook+anchor)
if s!=target.read_text():target.write_text(s)
