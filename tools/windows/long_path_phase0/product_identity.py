# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Inspect the manifest embedded in the built product and its instance copy."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import subprocess
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


def package_imports(exe, tool, output):
    """Check every packaged PE against its bundle and this VM's System32."""
    distribution = exe.parent
    system = Path(os.environ['SystemRoot']) / 'System32'
    images = [exe] + sorted(distribution.glob('*.dll'))
    bundled = {image.name.lower(): image for image in images}
    records = []
    for image in images:
        completed = subprocess.run([str(tool), '--coff-imports', str(image)],
                                   capture_output=True, text=True, encoding='utf-8', check=True)
        (output / (image.name + '.imports.txt')).write_text(completed.stdout, encoding='utf-8')
        if 'Format: COFF-x86-64' not in completed.stdout:
            raise AssertionError(f'Expected an x64 PE image: {image}')
        imports, kind, named = [], None, False
        for line in completed.stdout.splitlines():
            if line in ('Import {', 'DelayImport {'):
                kind, named = line.split()[0], False
            elif kind and line.strip().startswith('Name: '):
                name = line.strip()[6:]
                if Path(name).name != name or not name.lower().endswith('.dll'):
                    raise AssertionError(f'Unexpected PE import name: {name!r}')
                target = bundled.get(name.lower())
                if target is not None:
                    resolution = dict(kind='package', path=str(target), sha256=digest(target))
                elif name.lower().startswith(('api-ms-win-', 'ext-ms-win-')):
                    # API-set contracts are virtual DLL names, resolved by Windows.
                    resolution = dict(kind='windows-api-set')
                elif (system / name).is_file():
                    target = system / name
                    resolution = dict(kind='system32', path=str(target), sha256=digest(target))
                else:
                    raise AssertionError(f'Unresolved package import: {image.name} -> {name}')
                imports.append(dict(name=name, table=kind, resolution=resolution))
                named = True
            elif kind and line == '}':
                if not named:
                    raise AssertionError(f'Import table has no name: {image.name}')
                kind = None
        if kind:
            raise AssertionError(f'Incomplete import table: {image.name}')
        records.append(dict(image=image.name, sha256=digest(image), imports=imports))
    return dict(tool=str(tool), tool_sha256=digest(tool), images=records,
                scope='All bundled PEs; System32 dependencies belong to this VM; '
                      'API sets and dynamic loads also require product lifecycle validation')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--exe', type=Path, help='Inspect the extracted package product')
    parser.add_argument('--instance-result', type=Path)
    parser.add_argument('--imports-tool', type=Path, help='Configured LLVM readobj for package dependency checks')
    args = parser.parse_args()
    source = args.source_root.resolve()
    exe = args.exe.resolve(strict=True) if args.exe else source / 'build_phase0_nio/src/observer/seekdb.exe'
    distribution = exe.parent
    output = source / 'build_phase0' / ('product-identity-' + uuid.uuid4().hex)
    output.mkdir()
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
    if args.imports_tool:
        dependencies = package_imports(exe, args.imports_tool.resolve(strict=True), output)
        (output / 'dependencies.json').write_text(json.dumps(dependencies, indent=2), encoding='utf-8')
        report['dependencies'] = dict(file=str(output / 'dependencies.json'),
                                    sha256=digest(output / 'dependencies.json'),
                                    images=len(dependencies['images']))
    (output / 'identity.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PRODUCT_IDENTITY=' + json.dumps(report), flush=True)
    print('PRODUCT_IDENTITY_PASS LOG_ROOT=' + str(output), flush=True)


if __name__ == '__main__':
    main()
