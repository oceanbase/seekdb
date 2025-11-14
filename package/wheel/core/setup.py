#!/usr/bin/env python3
"""
Setup script for seekdb package
"""

import os
import sys
from pathlib import Path
from setuptools import setup, find_packages, Extension
from wheel.bdist_wheel import bdist_wheel

# Get the current directory
current_dir = Path(__file__).parent

def get_version():
    """Get version from the seekdb module"""
    return os.environ.get('PACKAGE_VERSION', '0.0.1.dev1')

def get_long_description():
    """Get long description from README"""
    readme_file = current_dir / "README.md"
    if readme_file.exists():
        return readme_file.read_text(encoding='utf-8')
    return "OceanBase SeekDB"

def __library_name():
    return "libseekdb_python"

def __package_name():
    return "pylibseekdb"

def get_package_data():
    """Get package data files"""
    return {
        f"{__package_name()}": [
            #"dummy.so",
            "libaio.so.1",
            f"{__library_name()}.so.1.gz",
            f"{__library_name()}.so.sha.0",
            f"{__library_name()}.so.sha.1",
            f"{__library_name()}.so.sha"
        ]
    }

# Setup configuration
setup(
    name=__package_name(),
    version=get_version(),
    description="OceanBase SeekDB",
    long_description=get_long_description(),
    long_description_content_type="text/markdown",
    author="OceanBase",
    author_email="open_oceanbase@oceanbase.com",
    maintainer="OceanBase",
    maintainer_email="open_oceanbase@oceanbase.com",
    url="https://github.com/oceanbase/seekdb",
    project_urls={
        "Homepage": "https://github.com/oceanbase/seekdb",
        "Repository": "https://github.com/oceanbase/seekdb",
        "Documentation": "https://github.com/oceanbase/seekdb",
        "Bug Tracker": "https://github.com/oceanbase/seekdb/issues",
    },
    packages=[__package_name()],
    package_dir={f"{__package_name()}": "."},
    package_data=get_package_data(),
    include_package_data=True,
    ext_modules = [ Extension('seekdb.dummy', ['dummy.c'], extra_link_args=['-nostdlib']) ],
    keywords=["database", "oceanbase", "vector-database", "embed", "sql", "AI"],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: Python :: 3.14",
        "Programming Language :: C++",
        "Topic :: Database",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    python_requires=">=3.8",
    install_requires=[
        f"pylibseekdb_runtime=={get_version()}"
    ],
    platforms=["manylinux"],
    license="Apache 2.0"
)
