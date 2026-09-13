"""GDB-only allocation accounting for a test-owned Linux observer process."""
import json
import os
from pathlib import Path

import gdb


result_path = Path(os.environ['SEEKDB_PALF_AUDIT_RESULT'])
counts = dict(pid=None, reads=0, sql_string_extensions_in_read=0,
              requested_extension_bytes=0)
active_reads = 0


class Main(gdb.Breakpoint):
    def stop(self):
        counts['pid'] = gdb.selected_inferior().pid
        result_path.with_suffix('.pid').write_text(str(counts['pid']))
        return False


class ReadFinish(gdb.FinishBreakpoint):
    def release(self):
        global active_reads
        active_reads -= 1
        extend.enabled = active_reads != 0

    def stop(self):
        self.release()
        return False

    def out_of_scope(self):
        self.release()


class Read(gdb.Breakpoint):
    def stop(self):
        global active_reads
        counts['reads'] += 1
        active_reads += 1
        extend.enabled = True
        ReadFinish(gdb.newest_frame(), internal=True)
        return False


class Extend(gdb.Breakpoint):
    def stop(self):
        frame = gdb.newest_frame()
        size = int(frame.read_var('size'))
        while frame is not None:
            if (frame.name() or '').startswith('oceanbase::palf::LogReader::pread'):
                counts['sql_string_extensions_in_read'] += 1
                counts['requested_extension_bytes'] += size
                break
            frame = frame.older()
        return False


def exited(event):
    counts['exit_code'] = getattr(event, 'exit_code', None)
    result_path.write_text(json.dumps(counts, indent=2) + '\n')


gdb.execute('set pagination off')
gdb.execute('set print thread-events off')
gdb.execute('handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint pass')
Main('main', internal=True)
Read('oceanbase::palf::LogReader::pread', internal=True)
extend = Extend('oceanbase::common::ObSqlString::extend', internal=True)
extend.enabled = False
gdb.events.exited.connect(exited)
gdb.execute('run')
