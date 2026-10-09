#!/usr/bin/env python3
"""Pin enqueue between the last idle worker's empty pop and retirement."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('action', choices=('enable', 'disable'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
changes = (
    ('src/observer/omt/ob_th_worker.cpp',
     '              return runtime_->get_new_request(POLL_INTERVAL, req);',
     '''              const int pop_ret = runtime_->get_new_request(POLL_INTERVAL, req);
              const char *prefix = getenv("SEEKDB_RUNTIME_SHRINK_PROBE");
              if (prefix && pop_ret == OB_ENTRY_NOT_EXIST && idle_since > 0
                  && ObTimeUtility::current_time() - idle_since >= ObServerRuntime::KEEP_ALIVE_TIMEOUT
                  && runtime_->worker_count() == 2 && runtime_->idle_count() == 1) {
                char arm[1024], ready[1024], queued[1024];
                snprintf(arm, sizeof(arm), "%s.arm", prefix);
                snprintf(ready, sizeof(ready), "%s.ready", prefix);
                snprintf(queued, sizeof(queued), "%s.queued", prefix);
                if (0 == access(arm, F_OK) && 0 != access(ready, F_OK)) {
                  FILE *f = fopen(ready, "w");
                  if (f) { fputs("last idle worker paused\\n", f); fclose(f); }
                  const int64_t end = ObTimeUtility::current_time() + 10000000;
                  while (0 != access(queued, F_OK) && ObTimeUtility::current_time() < end) { usleep(1000); }
                  fprintf(stderr, "RUNTIME_SHRINK_RACE workers=%ld idle=%ld queued=%ld\\n",
                      runtime_->worker_count(), runtime_->idle_count(), runtime_->queue_size());
                  fflush(stderr);
                }
              }
              return pop_ret;'''),
    ('src/observer/omt/ob_server_runtime.cpp',
     '  return ret;\n}\n\nint ObServerRuntime::push_retry_queue(',
     '''  const char *prefix = getenv("SEEKDB_RUNTIME_SHRINK_PROBE");
  if (prefix && OB_SUCC(ret) && req.get_type() == ObRequest::OB_MYSQL) {
    char ready[1024], queued[1024];
    snprintf(ready, sizeof(ready), "%s.ready", prefix);
    snprintf(queued, sizeof(queued), "%s.queued", prefix);
    if (0 == access(ready, F_OK)) {
      FILE *f = fopen(queued, "w");
      if (f) { fputs("enqueue and expansion decision finished\\n", f); fclose(f); }
    }
  }
'''),
)
for relative, anchor, code in changes:
    path = root / relative
    text = re.sub(r'^// RUNTIME_SHRINK_PROBE_BEGIN\n.*?^// RUNTIME_SHRINK_PROBE_END\n',
                  '', path.read_text(), flags=re.M | re.S)
    if relative.endswith('ob_th_worker.cpp'):
        # The replacement preserves its original statement for idempotent removal.
        wrapped = ('// RUNTIME_SHRINK_PROBE_BEGIN\n' + code + '\n'
                   '// RUNTIME_SHRINK_PROBE_END\n')
        if a.action == 'enable':
            assert text.count(anchor) == 1, relative
            text = text.replace(anchor, wrapped + anchor)
    elif a.action == 'enable':
        assert text.count(anchor) == 1, relative
        text = text.replace(anchor, '// RUNTIME_SHRINK_PROBE_BEGIN\n' + code +
                            '// RUNTIME_SHRINK_PROBE_END\n' + anchor)
    path.write_text(text)
print('Runtime shrink hooks', a.action)
