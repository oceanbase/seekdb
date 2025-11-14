#!/usr/bin/env python3
"""
Setup script for seekdb package
"""

import os
import sys
from pathlib import Path
from setuptools import setup, find_packages, Extension
from setuptools.command.bdist_wheel import bdist_wheel as _bdist_wheel

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
    return "pylibseekdb_runtime"

def get_package_data():
    """Get package data files"""
    return {
        __package_name(): [
            f"{__library_name()}.so.0.gz"
            ]
    }

class CustomBdistWheel(_bdist_wheel):
    """Custom bdist_wheel to specify non-pure Python wheel"""

    def finalize_options(self):
        super().finalize_options()
        # Mark as not pure Python (contains compiled extensions)
        self.root_is_pure = False

    def get_tag(self):
        # Get platform-specific tags
        return super().get_tag()
        '''
        python, abi, plat = super().get_tag()
        print(f"get tag: {python}, {abi}, {plat}")
        # Ensure we get platform-specific tags
        if sys.platform.startswith('linux'):
            plat = 'manylinux_2_28_x86_64'
        else:
            print(f'unsupported platform: {sys.platform}')
        return python, abi, plat
        '''

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
    package_dir={__package_name(): "."},
    package_data=get_package_data(),
    include_package_data=True,
    ext_modules = [ Extension('seekdb_lib.dummy', ['dummy.c']) ],
    keywords=["database", "oceanbase", "embed", "sql", "AI"],
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
    ],
    license="Apache 2.0",
    cmdclass={
        #'bdist_wheel': CustomBdistWheel,
    },
)
