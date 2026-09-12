# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Inspect the manifest embedded in the built product and its instance copy."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import uuid
import xml.etree.ElementTree as ET


def extended(path):
    return '\\\\?\\' + str(path.resolve())


def digest(path):
    with open(extended(path), 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def embedded_manifest(path):
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [W.LPCWSTR, W.HANDLE, W.DWORD]
    kernel.LoadLibraryExW.restype = W.HMODULE
    kernel.FindResourceW.argtypes = [W.HMODULE, C.c_void_p, C.c_void_p]
    kernel.FindResourceW.restype = W.HANDLE
    kernel.SizeofResource.argtypes = [W.HMODULE, W.HANDLE]
    kernel.SizeofResource.restype = W.DWORD
    kernel.LoadResource.argtypes = [W.HMODULE, W.HANDLE]
    kernel.LoadResource.restype = W.HANDLE
    kernel.LockResource.argtypes = [W.HANDLE]
    kernel.LockResource.restype = C.c_void_p
    kernel.FreeLibrary.argtypes = [W.HMODULE]
    kernel.FreeLibrary.restype = W.BOOL
    # Load resources without executing the image or resolving its imports.
    module = kernel.LoadLibraryExW(extended(path), None, 0x02 | 0x20)
    if not module:
        raise C.WinError(C.get_last_error())
    try:
        resource = kernel.FindResourceW(module, C.c_void_p(1), C.c_void_p(24))
        if not resource:
            raise C.WinError(C.get_last_error())
        size = kernel.SizeofResource(module, resource)
        if not size:
            raise C.WinError(C.get_last_error())
        loaded = kernel.LoadResource(module, resource)
        if not loaded:
            raise C.WinError(C.get_last_error())
        address = kernel.LockResource(loaded)
        if not address:
            raise RuntimeError('Cannot access product manifest resource')
        return C.string_at(address, size)
    finally:
        if not kernel.FreeLibrary(module):
            raise C.WinError(C.get_last_error())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--instance-result', type=Path)
    args = parser.parse_args()
    source = args.source_root.resolve()
    distribution = source / 'build_phase0_nio/src/observer'
    output = source / 'build_phase0' / ('product-identity-' + uuid.uuid4().hex)
    output.mkdir()
    exe = distribution / 'seekdb.exe'
    expected_hash = digest(exe)
    images = [('distribution', exe)]
    if args.instance_result:
        result = json.loads(args.instance_result.read_text(encoding='utf-8'))
        if not result.get('passed') or result.get('exe_sha256') != expected_hash:
            raise AssertionError('Instance evidence must pass on the current product')
        images.append(('instance', Path(result['base']) / 'run/seekdb.exe'))
    records = []
    for label, path in images:
        image_hash = digest(path)
        if image_hash != expected_hash:
            raise AssertionError('Instance executable differs from the distribution')
        manifest = embedded_manifest(path)
        (output / (label + '.manifest')).write_bytes(manifest)
        root = ET.fromstring(manifest)
        values = root.findall('.//{http://schemas.microsoft.com/SMI/2016/WindowsSettings}longPathAware')
        if len(values) != 1 or (values[0].text or '').strip().lower() != 'true':
            raise AssertionError('Product manifest must enable longPathAware exactly once')
        records.append(dict(image=label, path=str(path), sha256=image_hash,
                            manifest_sha256=hashlib.sha256(manifest).hexdigest(),
                            long_path_aware=True))
    report = dict(images=records, sqlite_sha256=digest(distribution / 'sqlite3.dll'))
    (output / 'identity.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PRODUCT_IDENTITY=' + json.dumps(report), flush=True)
    print('PRODUCT_IDENTITY_PASS LOG_ROOT=' + str(output), flush=True)


if __name__ == '__main__':
    main()
