"""
OceanBase SeekDB Python Embed

OceanBase SeekDB Python Embed provides Python bindings for OceanBase SeekDB, a high-performance embedded database engine.
This package supplies the lightweight interface layer for Python applications, making it easy to interact with SeekDB
databases and execute SQL from Python code.
"""

import os
import sys
import platform
import hashlib
import gzip
import shutil
import importlib.metadata
from pathlib import Path
from typing import Optional
import traceback

try:
  __version__ = importlib.metadata.version("pylibseekdb")
except importlib.metadata.PackageNotFoundError:
  __version__ = "0.0.1.dev1"

__author__ = "OceanBase"

_LIB_FILE_NAME = "libseekdb_python"

# Configuration for downloading the .so file
_SO_DOWNLOAD_CONFIG = {
    "version": f"v{__version__}",  # Tag/version in release assets
    "filename": f"{_LIB_FILE_NAME}.so",
    "checksum": None,  # Will be set after first successful download
}

def _initialize_module():
    try:
        seekdb_module = _load_oblite_module()
        attributes = []
        for attr_name in dir(seekdb_module):
            if not attr_name.startswith('_'):
                setattr(sys.modules[__name__], attr_name, getattr(seekdb_module, attr_name))
                attributes.append(attr_name)
    except Exception as e:
        print(f"Warning: Failed to import seekdb module: {e}")
        attributes = []
    return attributes

def _get_python_version() -> str:
    return f'py{sys.version_info.major}.{sys.version_info.minor}'

def _get_cache_dir() -> Path:
    """Get the cache directory path organized by python_version/version"""
    cache_dir = (
        Path.home()
        / ".seekdb"
        / "cache"
        / _get_python_version()
        / __version__
    )
    cache_dir.mkdir(parents=True, exist_ok=True)
    return cache_dir

def _get_so_path() -> Path:
    """Get the path where the .so file should be stored in user cache directory"""
    return _get_cache_dir() / f"{_LIB_FILE_NAME}.so"

def _checksum_file(file_path: Path) -> str:
    """Calculate the SHA256 checksum of a file"""
    with open(file_path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

def _verify_checksum(file_path: Path, checksum_file_path: Path) -> bool:
    """Verify the SHA256 checksum of a file"""
    with open(checksum_file_path, "r") as f:
        checksum = f.read().strip()
    return _checksum_file(file_path) == checksum

def _merge_so_file() -> bool:
    """
    Merge the libseekdb.so file if it doesn't exist

    Returns:
        True if merge was successful, False otherwise
    """
    so_path = _get_so_path()

    # Check if file already exists
    if so_path.exists():
        return True

    try:
        seekdb_lib_name = 'pylibseekdb_runtime'
        seekdb_name = 'pylibseekdb'
        # locate the seekdb-lib package
        seekdb_lib_package = importlib.metadata.distribution(seekdb_lib_name)
        seekdb_lib_path = seekdb_lib_package.locate_file(f"{seekdb_lib_name}/{_LIB_FILE_NAME}.so.0.gz")
        if not seekdb_lib_path.exists():
            raise FileNotFoundError(f"{seekdb_lib_name} package does not contain {seekdb_lib_name}/{_LIB_FILE_NAME}.so.0.gz, path: {seekdb_lib_path}")

        # locate the seekdb package
        seekdb_core_package = importlib.metadata.distribution(seekdb_name)
        seekdb_core_path = seekdb_core_package.locate_file(f"{seekdb_name}/{_LIB_FILE_NAME}.so.1.gz")
        if not seekdb_core_path.exists():
            raise FileNotFoundError(f"{seekdb_name} package does not contain {seekdb_name}/{_LIB_FILE_NAME}.so.1.gz, path: {seekdb_core_path}")

        libaio_path = seekdb_core_package.locate_file(f"{seekdb_name}/libaio.so.1")
        if not libaio_path.exists():
            raise FileNotFoundError(f"{seekdb_name} package does not contain {seekdb_name}/libaio.so.1, path: {libaio_path}")

        seekdb_core_sha_path = seekdb_core_package.locate_file(f"{seekdb_name}/{_LIB_FILE_NAME}.so.sha.1")
        seekdb_lib_sha_path = seekdb_core_package.locate_file(f"{seekdb_name}/{_LIB_FILE_NAME}.so.sha.0")
        seekdb_sha_path = seekdb_core_package.locate_file(f"{seekdb_name}/{_LIB_FILE_NAME}.so.sha")
        if not seekdb_core_sha_path.exists():
            raise FileNotFoundError(f"{seekdb_name} package does not contain {seekdb_name}/{_LIB_FILE_NAME}.so.sha.1, path: {seekdb_core_sha_path}")
        if not seekdb_lib_sha_path.exists():
            raise FileNotFoundError(f"{seekdb_name} package does not contain {seekdb_name}/{_LIB_FILE_NAME}.so.sha.0, path: {seekdb_lib_sha_path}")
        if not seekdb_sha_path.exists():
            raise FileNotFoundError(f"{seekdb_name} package does not contain {seekdb_name}/{_LIB_FILE_NAME}.so.sha, path: {seekdb_sha_path}")

        # verify the sha of the seekdb-lib package
        if not _verify_checksum(seekdb_lib_path, seekdb_lib_sha_path):
            raise ValueError(f"{seekdb_lib_name} package sha mismatch: seekdb lib = {seekdb_lib_path}, sha = {seekdb_lib_sha_path}, path: {seekdb_lib_path}")
        if not _verify_checksum(seekdb_core_path, seekdb_core_sha_path):
            raise ValueError(f"{seekdb_name} package sha mismatch: seekdb core = {seekdb_core_path}, sha = {seekdb_core_sha_path}, path: {seekdb_core_path}")

        # copy the seekdb-lib and seekdb-core to the cache directory
        tmp_lib_path = so_path.with_suffix('.tmp')
        tmp_lib_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(seekdb_lib_path, tmp_lib_path)
        with open(tmp_lib_path, 'wb') as out_f, gzip.open(seekdb_lib_path, 'rb') as in_f:
            shutil.copyfileobj(in_f, out_f)

        with open(tmp_lib_path, 'ab') as out_f, gzip.open(seekdb_core_path, 'rb') as in_f:
            shutil.copyfileobj(in_f, out_f)

        # check the checksum of the tmp file
        if not _verify_checksum(tmp_lib_path, seekdb_sha_path):
            raise ValueError(f"{seekdb_name} package sha mismatch: seekdb = {tmp_lib_path}, sha = {seekdb_sha_path}")

        # rename the tmp file to the final name
        so_path.unlink(missing_ok=True)
        tmp_lib_path.rename(so_path)

        #copy libaio
        cache_libaio_path = _get_cache_dir() / "libaio.so.1"
        shutil.copy(libaio_path, cache_libaio_path)

        print(f"Successfully cached to {so_path}")
        return True

    except Exception as e:
        print(f"Error merging {_LIB_FILE_NAME}.so: {e}")
        traceback.print_exc()
        return False

def _load_oblite_module():
    """Load the oblite module, merging the .so file if necessary"""
    so_path = _get_so_path()

    # Try to merge and cache if file doesn't exist
    if not so_path.exists():
        print("Preparing seekdb cache environment ...")
        if not _merge_so_file():
            raise ImportError(
                f"Failed to merge and cache {_LIB_FILE_NAME}.so."
            )

    # Add the cache directory to sys.path so we can import the .so file
    cache_dir = str(so_path.parent)
    if cache_dir not in sys.path:
        sys.path.insert(0, cache_dir)

    try:
        # Import the module
        import libseekdb_python
        return libseekdb_python
    except ImportError as e:
        raise ImportError(f"Failed to import {_LIB_FILE_NAME} module from {cache_dir}: {e}")

__all__ = ['__version__']
__all__.extend(_initialize_module())
