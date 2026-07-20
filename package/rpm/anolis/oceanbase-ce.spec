Name: oceanbase-ce
Version: %{VERSION}
Release: %{RELEASE}
Summary: oceanbase-ce
Group: Applications/Databases
License: Mulan PubL v2.
Url: https://open.oceanbase.com/
Vendor: OceanBase Inc.
Prefix: /home/admin/oceanbase
Requires: curl, jq, oceanbase-ce-libs = %{version}
BuildRequires: wget, rpm, cpio, make, glibc-devel, glibc-headers, binutils, m4, python3, python2, libtool, libaio
Autoreq: 0
%systemd_requires
%global daemon_name oceanbase
%define __os_install_post %{nil}
%define _prefix /home/admin/oceanbase
%description
OceanBase is a distributed relational database
%global __requires_exclude ^(/bin/bash|/usr/bin/.*)$
%define _unpackaged_files_terminate_build 0
%global _missing_build_ids_terminate_build 0
%global arch %{_arch}
%global lto_jobs %{?lto_jobs}
%global src_dir %{?src_dir}
%global _find_debuginfo_opts -g
%define __strip %{?src_dir}/deps/3rd/usr/local/oceanbase/devtools/bin/llvm-strip
%undefine __brp_mangle_shebangs
%define buildsubdir %{name}-%{version}-%{release}.%{arch}
%define _debugsource_template %{nil}
%global debug_package %{nil}

%package debuginfo
Summary: Debug information for OceanBase
Group: Development/Debug
%description debuginfo
This package provides debug information for package oceanbase-ce.
Debug information is useful when developing applications that use this
package or when debugging this package.

%package libs
Summary: Libraries for OceanBase
Group: System Environment/Libraries
%description libs
This package contains libraries for OceanBase.

%package libs-debuginfo
Summary: Debug information for OceanBase Libs
Group: Development/Debug
%description libs-debuginfo
This package provides debug information for package oceanbase-ce-libs.
Debug information is useful when developing applications that use this
package or when debugging this package.

%package sql-parser
Summary: Sql Parser Libraries for OceanBase
Group: System Environment/Libraries
%description sql-parser
This package contains sql pareser libraries for OceanBase.

%package utils
Summary: Tools for OceanBase
Group: Development/Tools
%description utils
This package contains tools for OceanBase.

%package utils-debuginfo
Summary: Debug information for OceanBase Utils
Group: Development/Debug
%description utils-debuginfo
This package provides debug information for package oceanbase-ce-utils.
Debug information is useful when developing applications that use this
package or when debugging this package.

%build
rm -rf $RPM_BUILD_ROOT/%{_prefix}
mkdir -p $RPM_BUILD_ROOT/%{_prefix}

RPM_DIR=$OLDPWD;
SRC_DIR=$OLDPWD/../../..;
BUILD_DIR=$OLDPWD/rpmbuild

mkdir -p $BUILD_DIR
cd $BUILD_DIR
rm -rf $BUILD_DIR/*

if [ ! -e "/usr/bin/python" ]; then
    ln -s /usr/bin/python2 /usr/bin/python
fi
pip3 install PyYAML

cd $SRC_DIR
chmod u+x build.sh
bash build.sh clean
bash build.sh release -DOB_BUILD_PACKAGE=ON -DENABLE_AUTO_FDO=ON -DENABLE_THIN_LTO=ON \
   -DOB_STATIC_LINK_LGPL_DEPS=OFF -DCMAKE_INSTALL_PREFIX=%{_prefix} -DLTO_JOBS=%{lto_jobs} --init
cd build_release
make observer ob_sql_proxy_parser_static ob_admin ob_error %{_smp_mflags};
make install DESTDIR=$RPM_BUILD_ROOT

mv %{_buildrootdir}/%{buildsubdir}/%{_prefix}/bin/obshell %{_builddir}/obshell
%{_rpmconfigdir}/find-debuginfo.sh %{?_find_debuginfo_opts} %{_builddir}/%{?buildsubdir}; %{nil}
mv %{_builddir}/obshell %{_buildrootdir}/%{buildsubdir}/%{_prefix}/bin/obshell

echo "/usr/lib/debug/%{_prefix}/bin/observer.debug" > %{buildroot}/debuginfo_filelist

echo "/usr/lib/debug/%{_prefix}/lib/libaio.so.debug" > %{buildroot}/libs_debuginfo_filelist
echo "/usr/lib/debug/%{_prefix}/lib/libaio.so.1.debug" >> %{buildroot}/libs_debuginfo_filelist
echo "/usr/lib/debug/%{_prefix}/lib/libaio.so.1.0.1.debug" >> %{buildroot}/libs_debuginfo_filelist
echo "/usr/lib/debug/%{_prefix}/lib/libmariadb.so.debug" >> %{buildroot}/libs_debuginfo_filelist
echo "/usr/lib/debug/%{_prefix}/lib/libmariadb.so.3.debug" >> %{buildroot}/libs_debuginfo_filelist

echo "/usr/lib/debug/usr/bin/ob_admin.debug" > %{buildroot}/utils_debuginfo_filelist
echo "/usr/lib/debug/usr/bin/ob_error.debug" >> %{buildroot}/utils_debuginfo_filelist

# package infomation
%files
%defattr(-,root,root,0777)
%{_prefix}/admin
%{_prefix}/bin/import_srs_data.py
%{_prefix}/bin/import_time_zone_info.py
%{_prefix}/bin/observer
%{_prefix}/bin/obshell
%{_prefix}/etc/default_parameter.json
%{_prefix}/etc/default_system_variable.json
%{_prefix}/etc/default_srs_data_mysql.sql
%{_prefix}/etc/fill_help_tables-ob.sql
%{_prefix}/etc/oceanbase_upgrade_dep.yml
%{_prefix}/etc/deps_compat.yml
%{_prefix}/etc/timezone_V1.log
%{_prefix}/etc/upgrade_checker.py
%{_prefix}/etc/upgrade_health_checker.py
%{_prefix}/etc/upgrade_post.py
%{_prefix}/etc/upgrade_pre.py
%{_prefix}/profile

%files debuginfo -f %{buildroot}/debuginfo_filelist
%defattr(-,root,root,0777)

%files libs
%defattr(-,root,root,0777)
%{_prefix}/lib/libaio.so
%{_prefix}/lib/libaio.so.1
%{_prefix}/lib/libaio.so.1.0.1
%{_prefix}/lib/libmariadb.so
%{_prefix}/lib/libmariadb.so.3

%files libs-debuginfo -f %{buildroot}/libs_debuginfo_filelist
%defattr(-,root,root,0777)

%files sql-parser
%defattr(-,root,root,0777)
%{_prefix}/include/ob_item_type.h
%{_prefix}/include/ob_sql_mode.h
%{_prefix}/include/ob_sql_parser.h
%{_prefix}/include/parse_malloc.h
%{_prefix}/include/parse_node.h
%{_prefix}/include/parser_proxy_func.h
%{_prefix}/lib/libob_sql_proxy_parser_static.a

%files utils
%defattr(-,root,root,0777)
/usr/bin/ob_admin
/usr/bin/ob_error

%files utils-debuginfo -f %{buildroot}/utils_debuginfo_filelist
%defattr(-,root,root,0777)

%post
echo "execute post install script"
cp -f %{_prefix}/profile/oceanbase.service /etc/systemd/system/oceanbase.service
chmod 644 /etc/systemd/system/oceanbase.service
chmod +x %{_prefix}/profile/oceanbase-service.sh
cp -f %{_prefix}/profile/oceanbase.cnf /etc/oceanbase.cnf
systemctl daemon-reload

# telemetry
/bin/bash %{_prefix}/profile/telemetry.sh $1 >/dev/null 2>&1

%preun
echo "execute pre uninstall script"
systemctl stop %{daemon_name}
systemctl disable %{daemon_name}
/bin/bash %{_prefix}/profile/oceanbase-service.sh destroy
rm -f /etc/systemd/system/oceanbase.service /etc/oceanbase.cnf
systemctl daemon-reload

# telemetry
/bin/bash %{_prefix}/profile/telemetry.sh $1 >/dev/null 2>&1

%postun
echo "execute post uninstall script"
rm -rf %{_prefix}/.meta %{_prefix}/log_obshell

%changelog
* Mon Mar 18 2024 oceanbase 4.3.0
- new features: support for packaging with spec files
- new features: support for packaging on anolis
