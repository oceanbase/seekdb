"""Report the identity of the DLL actually loaded by this process."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path


def identity(dll):
    lib = C.CDLL(str(dll.resolve()))
    query = C.WinDLL('kernel32', use_last_error=True).GetModuleFileNameW
    query.argtypes = [C.c_void_p, C.c_wchar_p, C.c_uint32]
    query.restype = C.c_uint32
    buffer = C.create_unicode_buffer(32768)
    size = query(lib._handle, buffer, len(buffer))
    if not size or size == len(buffer):
        raise C.WinError(C.get_last_error())
    loaded = Path(buffer.value)
    if not os.path.samefile(dll, loaded):
        raise RuntimeError('Loaded module is not the requested DLL')
    lib.sqlite3_sourceid.restype = C.c_char_p
    lib.sqlite3_compileoption_get.argtypes = [C.c_int]
    lib.sqlite3_compileoption_get.restype = C.c_char_p
    options = []
    while True:
        option = lib.sqlite3_compileoption_get(len(options))
        if option is None:
            break
        options.append(option.decode())
    return {'loaded_path': str(loaded),
                      'sha256': hashlib.sha256(loaded.read_bytes()).hexdigest(),
                      'source_id': lib.sqlite3_sourceid().decode(),
                      'compile_options': options}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', type=Path, nargs='+', required=True)
    args = parser.parse_args()
    reports = [identity(dll) for dll in args.dll]
    print(json.dumps(reports, sort_keys=True), flush=True)
    for report in reports[1:]:
        for field in ('source_id', 'compile_options'):
            if report[field] != reports[0][field]:
                raise RuntimeError('DLL identity differs: ' + field)
    print('SOURCE_AND_OPTIONS_MATCH=' + str(len(reports)))


if __name__ == '__main__':
    main()
