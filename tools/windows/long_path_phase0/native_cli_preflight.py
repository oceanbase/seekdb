# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Check CLI preflight rejection before child creation or filesystem mutation."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import subprocess
import time
import uuid
import winreg


class StartupInfo(C.Structure):
    _fields_ = [
        ('cb', W.DWORD), ('reserved', W.LPWSTR), ('desktop', W.LPWSTR),
        ('title', W.LPWSTR), ('x', W.DWORD), ('y', W.DWORD),
        ('width', W.DWORD), ('height', W.DWORD), ('chars_x', W.DWORD),
        ('chars_y', W.DWORD), ('fill', W.DWORD), ('flags', W.DWORD),
        ('show', W.WORD), ('reserved_size', W.WORD), ('reserved_data', C.c_void_p),
        ('stdin', W.HANDLE), ('stdout', W.HANDLE), ('stderr', W.HANDLE),
    ]


class ProcessInfo(C.Structure):
    _fields_ = [('process', W.HANDLE), ('thread', W.HANDLE),
                ('pid', W.DWORD), ('tid', W.DWORD)]


class JobAccounting(C.Structure):
    _fields_ = [('user_time', C.c_longlong), ('kernel_time', C.c_longlong),
                ('period_user_time', C.c_longlong), ('period_kernel_time', C.c_longlong),
                ('faults', W.DWORD), ('total', W.DWORD), ('active', W.DWORD),
                ('terminated', W.DWORD)]


class JobCompletionPort(C.Structure):
    _fields_ = [('key', C.c_void_p), ('port', W.HANDLE)]


k = C.WinDLL('kernel32', use_last_error=True)
for name, args, result in [
    ('GetConsoleCP', [], W.UINT),
    ('AllocConsole', [], W.BOOL),
    ('FreeConsole', [], W.BOOL),
    ('CreateJobObjectW', [C.c_void_p, W.LPCWSTR], W.HANDLE),
    ('CreateIoCompletionPort', [W.HANDLE, W.HANDLE, C.c_size_t, W.DWORD], W.HANDLE),
    ('SetInformationJobObject', [W.HANDLE, C.c_int, C.c_void_p, W.DWORD], W.BOOL),
    ('GetQueuedCompletionStatus', [W.HANDLE, C.POINTER(W.DWORD), C.POINTER(C.c_size_t),
                                   C.POINTER(C.c_void_p), W.DWORD], W.BOOL),
    ('OpenProcess', [W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
    ('QueryFullProcessImageNameW', [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)], W.BOOL),
    ('CreateProcessW', [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL,
                        W.DWORD, C.c_void_p, W.LPCWSTR, C.POINTER(StartupInfo),
                        C.POINTER(ProcessInfo)], W.BOOL),
    ('AssignProcessToJobObject', [W.HANDLE, W.HANDLE], W.BOOL),
    ('ResumeThread', [W.HANDLE], W.DWORD),
    ('WaitForSingleObject', [W.HANDLE, W.DWORD], W.DWORD),
    ('GetExitCodeProcess', [W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
    ('QueryInformationJobObject', [W.HANDLE, C.c_int, C.c_void_p, W.DWORD,
                                  C.POINTER(W.DWORD)], W.BOOL),
    ('TerminateJobObject', [W.HANDLE, W.UINT], W.BOOL),
    ('TerminateProcess', [W.HANDLE, W.UINT], W.BOOL),
    ('CloseHandle', [W.HANDLE], W.BOOL),
]:
    api = getattr(k, name)
    api.argtypes, api.restype = args, result


def invoke(exe, args, cwd, output):
    job = k.CreateJobObjectW(None, None)
    if not job:
        raise C.WinError(C.get_last_error())
    process = ProcessInfo()
    completion = None
    try:
        completion = k.CreateIoCompletionPort(C.c_void_p(-1).value, None, 0, 1)
        if not completion:
            raise C.WinError(C.get_last_error())
        association = JobCompletionPort(None, completion)
        if not k.SetInformationJobObject(job, 7, C.byref(association), C.sizeof(association)):
            raise C.WinError(C.get_last_error())
        with open(str(output) + '.stdout.log', 'wb') as out, \
             open(str(output) + '.stderr.log', 'wb') as err:
            for stream in (out, err):
                os.set_handle_inheritable(msvcrt.get_osfhandle(stream.fileno()), True)
            start = StartupInfo()
            start.cb = C.sizeof(start)
            start.flags = 0x100  # STARTF_USESTDHANDLES
            start.stdout = msvcrt.get_osfhandle(out.fileno())
            start.stderr = msvcrt.get_osfhandle(err.fileno())
            command = C.create_unicode_buffer(subprocess.list2cmdline([str(exe)] + args))
            # Assign while suspended so even a short-lived daemon child would
            # be counted. No breakaway flag is enabled for this private job.
            if not k.CreateProcessW(str(exe), command, None, None, True,
                                    0x4, None, str(cwd),
                                    C.byref(start), C.byref(process)):
                raise C.WinError(C.get_last_error())
            if not k.AssignProcessToJobObject(job, process.process):
                raise C.WinError(C.get_last_error())
            if k.ResumeThread(process.thread) == 0xffffffff:
                raise C.WinError(C.get_last_error())
            events = []
            deadline = time.monotonic() + 60
            while True:
                message, key, value = W.DWORD(), C.c_size_t(), C.c_void_p()
                ok = k.GetQueuedCompletionStatus(completion, C.byref(message), C.byref(key),
                                                  C.byref(value), 100)
                if ok:
                    event = dict(message=message.value, pid=value.value)
                    if message.value == 6:  # JOB_OBJECT_MSG_NEW_PROCESS
                        handle = k.OpenProcess(0x1000, False, value.value)
                        if handle:
                            try:
                                image = C.create_unicode_buffer(32768)
                                length = W.DWORD(len(image))
                                if k.QueryFullProcessImageNameW(handle, 0, image, C.byref(length)):
                                    event['image'] = image.value
                                else:
                                    event['image_error'] = C.get_last_error()
                            finally:
                                k.CloseHandle(handle)
                        else:
                            event['open_error'] = C.get_last_error()
                    events.append(event)
                    if message.value == 4:  # JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO
                        break
                elif C.get_last_error() != 258:  # WAIT_TIMEOUT
                    raise C.WinError(C.get_last_error())
                if time.monotonic() > deadline:
                    raise TimeoutError('preflight process tree did not exit in 60 seconds')
            code = W.DWORD()
            accounting = JobAccounting()
            if not k.GetExitCodeProcess(process.process, C.byref(code)):
                raise C.WinError(C.get_last_error())
            if not k.QueryInformationJobObject(job, 1, C.byref(accounting),
                                               C.sizeof(accounting), None):
                raise C.WinError(C.get_last_error())
            return dict(exit=code.value, pid=process.pid,
                        processes=accounting.total, active=accounting.active, events=events)
    finally:
        # Only this test-owned process tree; failed cases and output are kept.
        k.TerminateJobObject(job, 1)
        if process.process:
            k.TerminateProcess(process.process, 1)
            k.WaitForSingleObject(process.process, 1000)
            k.CloseHandle(process.process)
        if process.thread:
            k.CloseHandle(process.thread)
        k.CloseHandle(job)
        if completion:
            k.CloseHandle(completion)


def sized_path(base, size, unicode=False):
    pattern = '目录😀 a%' if unicode else 'a'
    units = lambda text: len(text.encode('utf-16-le')) // 2
    while units(base) < size:
        remaining = size - units(base)
        if remaining == 1:
            base += 'a'
        else:
            budget = min(80, remaining - 1)
            component = pattern * (budget // units(pattern))
            base += '\\' + component + 'a' * (budget - units(component))
    return base


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True)
    args = parser.parse_args()
    source = Path(args.source_root).resolve()
    exe = source / 'build_phase0_nio/src/observer/seekdb.exe'
    root = source / 'build_phase0' / ('cli-preflight-' + uuid.uuid4().hex)
    root.mkdir()
    cwd = root / 'cwd'
    cwd.mkdir()
    sentinel = cwd / 'retained.txt'
    sentinel.write_bytes(b'original cwd must remain intact')
    original = (sentinel.stat().st_ino, sentinel.read_bytes())
    target = root / 'targets'
    base = str(target / 'base')
    cases = [
        ('base2049-ascii', ['--base-dir', sized_path(base, 2049)], 'option=base-dir', '-4019'),
        ('base2049-unicode', ['--base-dir', sized_path(base, 2049, True)], 'option=base-dir', '-4019'),
        ('component256', ['--base-dir', base + '\\' + 'a' * 256], 'option=base-dir', '-4019'),
        ('data4097', ['--base-dir', base, '--data-dir', sized_path(str(target / 'data'), 4097)],
         'option=data-dir', '-4019'),
        ('redo4097', ['--base-dir', base, '--redo-dir', sized_path(str(target / 'redo'), 4097)],
         'option=redo-dir', '-4019'),
        ('reserved-device', ['--base-dir', base + '\\NUL.txt'], 'option=base-dir', '-4002'),
        ('trailing-dot', ['--base-dir', base + '.'], 'option=base-dir', '-4002'),
        ('invalid-utf16', ['--base-dir', base + '\\\ud800'], 'Windows startup input failed', '-4002'),
    ]
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r'SYSTEM\CurrentControlSet\Control\FileSystem') as key:
        policy, _ = winreg.QueryValueEx(key, 'LongPathsEnabled')
    results = dict(exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                   policy=policy, cases=[], passed=False)
    report = root / 'result.json'
    report.write_text(json.dumps(results, indent=2), encoding='utf-8')
    for daemon in (True, False):
        for name, options, diagnostic, error in cases:
            label = name + ('-daemon' if daemon else '-nodaemon')
            output = root / label
            result = invoke(exe, options + ['--embedded'] +
                            ([] if daemon else ['--nodaemon']), cwd, output)
            result['case'] = label
            stderr = Path(str(output) + '.stderr.log').read_text(encoding='utf-8', errors='replace')
            results['cases'].append(result)
            report.write_text(json.dumps(results, indent=2), encoding='utf-8')
            if result['exit'] != 2 or result['processes'] != 1 or result['active'] != 0:
                raise AssertionError(f'{label}: unexpected process result {result}')
            if diagnostic not in stderr or f'ret={error}' not in stderr:
                raise AssertionError(f'{label}: missing specific preflight diagnostic')
            if target.exists() or list(cwd.iterdir()) != [sentinel]:
                raise AssertionError(f'{label}: preflight created files')
            if (sentinel.stat().st_ino, sentinel.read_bytes()) != original:
                raise AssertionError(f'{label}: original cwd target was changed')
            print(f'CLI_PREFLIGHT_CASE_PASS {label} EXIT=2 PROCESSES=1', flush=True)
    results['passed'] = True
    report.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(f'CLI_PREFLIGHT_PASS LOG_ROOT={root}', flush=True)


if __name__ == '__main__':
    # A normal CLI inherits its caller console. CREATE_NO_WINDOW caused
    # Windows to add a conhost.exe to each job in the WinRM environment,
    # falsely counting that OS console host as a product-created child.
    # Establish the caller console before creating any private test job.
    created_console = not k.GetConsoleCP()
    if created_console and not k.AllocConsole():
        raise C.WinError(C.get_last_error())
    try:
        main()
    finally:
        if created_console:
            k.FreeConsole()
