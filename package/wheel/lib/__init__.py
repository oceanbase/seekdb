"""
OceanBase SeekDB Python Embed

OceanBase SeekDB Python Embed provides Python bindings for OceanBase SeekDB, a high-performance embedded database engine.
This package supplies the lightweight interface layer for Python applications, making it easy to interact with SeekDB
databases and execute SQL from Python code.

"""

import os
import sys
from importlib.metadata import version, PackageNotFoundError

try:
  __version__ = version("seekdb_lib")
except PackageNotFoundError:
  __version__ = "0.0.1.dev1"

__author__ = "OceanBase"

__all__ = ['__version__']
