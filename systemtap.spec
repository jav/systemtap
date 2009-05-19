%{!?with_sqlite: %define with_sqlite 1}
%{!?with_docs: %define with_docs 1}
%{!?with_crash: %define with_crash 0}
%{!?with_bundled_elfutils: %define with_bundled_elfutils 0}
%{!?elfutils_version: %define elfutils_version 0.127}
%{!?pie_supported: %define pie_supported 1}

Name: systemtap
Version: 0.9.7
Release: 1%{?dist}
# for version, see also configure.ac
Summary: Instrumentation System
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Source: ftp://sourceware.org/pub/%{name}/releases/%{name}-%{version}.tar.gz

BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires: kernel >= 2.6.9-11
%if %{with_sqlite}
BuildRequires: sqlite-devel
%endif
%if %{with_crash}
BuildRequires: crash-devel zlib-devel
%endif
# Alternate kernel packages kernel-PAE-devel et al have a virtual
# provide for kernel-devel, so this requirement does the right thing.
Requires: kernel-devel
Requires: gcc make
# Suggest: kernel-debuginfo
Requires: systemtap-runtime = %{version}-%{release}
BuildRequires: nss-devel nss-tools pkgconfig

%if %{with_bundled_elfutils}
Source1: elfutils-%{elfutils_version}.tar.gz
Patch1: elfutils-portability.patch
%define setup_elfutils -a1
%else
BuildRequires: elfutils-devel >= %{elfutils_version}
%endif
%if %{with_crash}
Requires: crash
%endif

%if %{with_docs}
BuildRequires: /usr/bin/latex /usr/bin/dvips /usr/bin/ps2pdf latex2html
# On F10, xmlto's pdf support was broken off into a sub-package,
# called 'xmlto-tex'.  To avoid a specific F10 BuildReq, we'll do a
# file-based buildreq on '/usr/share/xmlto/format/fo/pdf'.
BuildRequires: xmlto /usr/share/xmlto/format/fo/pdf
%endif

%description
SystemTap is an instrumentation system for systems running Linux 2.6.
Developers can write instrumentation to collect data on the operation
of the system.

%package runtime
Summary: Instrumentation System Runtime
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: kernel >= 2.6.9-11
Requires(pre): shadow-utils

%description runtime
SystemTap runtime is the runtime component of an instrumentation
system for systems running Linux 2.6.  Developers can write
instrumentation to collect data on the operation of the system.

%package testsuite
Summary: Instrumentation System Testsuite
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap dejagnu

%description testsuite
The testsuite allows testing of the entire SystemTap toolchain
without having to rebuild from sources.

%package client
Summary: Instrumentation System Client
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap-runtime = %{version}-%{release}
Requires: avahi avahi-tools nss nss-tools mktemp

%description client
SystemTap client is the client component of an instrumentation
system for systems running Linux 2.6.  Developers can write
instrumentation to collect data on the operation of the system.

%package server
Summary: Instrumentation System Server
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap
Requires: avahi avahi-tools nss nss-tools mktemp

%description server
SystemTap server is the server component of an instrumentation
system for systems running Linux 2.6.  Developers can write
instrumentation to collect data on the operation of the system.

%package sdt-devel
Summary: Static probe support tools
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap

%description sdt-devel
Support tools to allow applications to use static probes.

%package initscript
Summary: Systemtap Initscript
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap-runtime, initscripts

%description initscript
Initscript for Systemtap scripts.

%prep
%setup -q %{?setup_elfutils}

%if %{with_bundled_elfutils}
cd elfutils-%{elfutils_version}
%patch1 -p1
sleep 1
find . \( -name Makefile.in -o -name aclocal.m4 \) -print | xargs touch
sleep 1
find . \( -name configure -o -name config.h.in \) -print | xargs touch
cd ..
%endif

%build

%if %{with_bundled_elfutils}
# Build our own copy of elfutils.
%define elfutils_config --with-elfutils=elfutils-%{elfutils_version}

# We have to prevent the standard dependency generation from identifying
# our private elfutils libraries in our provides and requires.
%define _use_internal_dependency_generator	0
%define filter_eulibs() /bin/sh -c "%{1} | sed '/libelf/d;/libdw/d;/libebl/d'"
%define __find_provides %{filter_eulibs /usr/lib/rpm/find-provides}
%define __find_requires %{filter_eulibs /usr/lib/rpm/find-requires}

# This will be needed for running stap when not installed, for the test suite.
%define elfutils_mflags LD_LIBRARY_PATH=`pwd`/lib-elfutils
%endif

# Enable/disable the sqlite coverage testing support
%if %{with_sqlite}
%define sqlite_config --enable-sqlite
%else
%define sqlite_config --disable-sqlite
%endif

# Enable/disable the crash extension
%if %{with_crash}
%define crash_config --enable-crash
%else
%define crash_config --disable-crash
%endif

%if %{with_docs}
%define docs_config --enable-docs
%else
%define docs_config --disable-docs
%endif

# Enable pie as configure defaults to disabling it
%if %{pie_supported}
%define pie_config --enable-pie
%else
%define pie_config --disable-pie
%endif


%configure %{?elfutils_config} %{sqlite_config} %{crash_config} %{docs_config} %{pie_config}
make %{?_smp_mflags}

%install
rm -rf ${RPM_BUILD_ROOT}
make DESTDIR=$RPM_BUILD_ROOT install

# We want the examples in the special doc dir, not the build install dir.
# We build it in place and then move it away so it doesn't get installed
# twice. rpm can specify itself where the (versioned) docs go with the
# %doc directive.
mv $RPM_BUILD_ROOT%{_datadir}/doc/systemtap/examples examples

# Fix paths in the example & testsuite scripts
find examples testsuite -type f -name '*.stp' -print0 | xargs -0 sed -i -r -e '1s@^#!.+stap@#!%{_bindir}/stap@'

# Because "make install" may install staprun with mode 04111, the
# post-processing programs rpmbuild runs won't be able to read it.
# So, we change permissions so that they can read it.  We'll set the
# permissions back to 04111 in the %files section below.
chmod 755 $RPM_BUILD_ROOT%{_bindir}/staprun

# Copy over the testsuite
cp -rp testsuite $RPM_BUILD_ROOT%{_datadir}/systemtap

%if %{with_docs}
# We want the manuals in the special doc dir, not the generic doc install dir.
# We build it in place and then move it away so it doesn't get installed
# twice. rpm can specify itself where the (versioned) docs go with the
# %doc directive.
mkdir docs.installed
mv $RPM_BUILD_ROOT%{_datadir}/doc/systemtap/*.pdf docs.installed/
mv $RPM_BUILD_ROOT%{_datadir}/doc/systemtap/tapsets docs.installed/
%endif

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d/
install -m 755 initscript/systemtap $RPM_BUILD_ROOT%{_sysconfdir}/init.d/
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/systemtap
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/systemtap/conf.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/systemtap/script.d
install -m 644 initscript/config $RPM_BUILD_ROOT%{_sysconfdir}/systemtap
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/cache/systemtap
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/run/systemtap

%clean
rm -rf ${RPM_BUILD_ROOT}

%pre runtime
getent group stapdev >/dev/null || groupadd -r stapdev
getent group stapusr >/dev/null || groupadd -r stapusr
exit 0

%post initscript
chkconfig --add systemtap
exit 0

%preun initscript
chkconfig --del systemtap
exit 0


%files
%defattr(-,root,root)

%doc README AUTHORS NEWS COPYING examples
%if %{with_docs}
%doc docs.installed/*.pdf
%doc docs.installed/tapsets
%endif

%{_bindir}/stap
%{_bindir}/stap-report
%{_bindir}/stap-env
%{_bindir}/stap-gen-cert
%{_bindir}/stap-authorize-cert
%{_bindir}/stap-authorize-signing-cert
%{_mandir}/man1/*
%{_mandir}/man3/*

%dir %{_datadir}/%{name}
%{_datadir}/%{name}/runtime
%{_datadir}/%{name}/tapset

%if %{with_bundled_elfutils} || %{with_crash}
%dir %{_libdir}/%{name}
%endif
%if %{with_bundled_elfutils}
%{_libdir}/%{name}/lib*.so*
%endif
%if %{with_crash}
%{_libdir}/%{name}/staplog.so*
%endif

%files runtime
%defattr(-,root,root)
%attr(4111,root,root) %{_bindir}/staprun
%{_bindir}/stap-report
%{_libexecdir}/%{name}
%{_mandir}/man8/staprun.8*

%doc README AUTHORS NEWS COPYING

%files testsuite
%defattr(-,root,root)
%{_datadir}/%{name}/testsuite

%files client
%defattr(-,root,root)
%{_bindir}/stap-client
%{_bindir}/stap-env
%{_bindir}/stap-find-servers
%{_bindir}/stap-authorize-cert
%{_bindir}/stap-authorize-server-cert
%{_bindir}/stap-client-connect
%{_mandir}/man8/stap-server.8*

%files server
%defattr(-,root,root)
%{_bindir}/stap-server
%{_bindir}/stap-serverd
%{_bindir}/stap-env
%{_bindir}/stap-start-server
%{_bindir}/stap-find-servers
%{_bindir}/stap-find-or-start-server
%{_bindir}/stap-stop-server
%{_bindir}/stap-gen-cert
%{_bindir}/stap-authorize-cert
%{_bindir}/stap-authorize-server-cert
%{_bindir}/stap-server-connect
%{_mandir}/man8/stap-server.8*

%files sdt-devel
%defattr(-,root,root)
%{_bindir}/dtrace
%{_includedir}/sys/sdt.h

%files initscript
%defattr(-,root,root)
%{_sysconfdir}/init.d/systemtap
%dir %{_sysconfdir}/systemtap
%dir %{_sysconfdir}/systemtap/conf.d
%dir %{_sysconfdir}/systemtap/script.d
%config(noreplace) %{_sysconfdir}/systemtap/config
%dir %{_localstatedir}/cache/systemtap
%dir %{_localstatedir}/run/systemtap
%doc initscript/README.initscript


%changelog
* Thu Apr 23 2009 Josh Stone <jistone@redhat.com> - 0.9.7-1
- Upstream release.

* Fri Mar 27 2009 Josh Stone <jistone@redhat.com> - 0.9.5-1
- Upstream release.

* Wed Mar 18 2009 Will Cohen <wcohen@redhat.com> - 0.9-2
- Add location of man pages.

* Tue Feb 17 2009 Frank Ch. Eigler <fche@redhat.com> - 0.9-1
- Upstream release.

* Thu Nov 13 2008 Frank Ch. Eigler <fche@redhat.com> - 0.8-1
- Upstream release.

* Tue Jul 15 2008 Frank Ch. Eigler <fche@redhat.com> - 0.7-1
- Upstream release.

* Fri Feb  1 2008 Frank Ch. Eigler <fche@redhat.com> - 0.6.1-3
- Add zlib-devel to buildreq; missing from crash-devel
- Process testsuite .stp files for #!stap->#!/usr/bin/stap

* Fri Jan 18 2008 Frank Ch. Eigler <fche@redhat.com> - 0.6.1-1
- Add crash-devel buildreq to build staplog.so crash(8) module.
- Many robustness & functionality improvements:

* Wed Dec  5 2007 Will Cohen <wcohen@redhat.com> - 0.6-2
- Correct Source to point to location contain code.

* Thu Aug  9 2007 David Smith <dsmith@redhat.com> - 0.6-1
- Bumped version, added libcap-devel BuildRequires.

* Wed Jul 11 2007 Will Cohen <wcohen@redhat.com> - 0.5.14-2
- Fix Requires and BuildRequires for sqlite.

* Tue Jul  2 2007 Frank Ch. Eigler <fche@redhat.com> - 0.5.14-1
- Many robustness improvements: 1117, 1134, 1305, 1307, 1570, 1806,
  2033, 2116, 2224, 2339, 2341, 2406, 2426, 2438, 2583, 3037,
  3261, 3282, 3331, 3428 3519, 3545, 3625, 3648, 3880, 3888, 3911,
  3952, 3965, 4066, 4071, 4075, 4078, 4081, 4096, 4119, 4122, 4127,
  4146, 4171, 4179, 4183, 4221, 4224, 4254, 4281, 4319, 4323, 4326,
  4329, 4332, 4337, 4415, 4432, 4444, 4445, 4458, 4467, 4470, 4471,
  4518, 4567, 4570, 4579, 4589, 4609, 4664

* Mon Mar 26 2007 Frank Ch. Eigler <fche@redhat.com> - 0.5.13-1
- An emergency / preliminary refresh, mainly for compatibility
  with 2.6.21-pre kernels.

* Mon Jan  1 2007 Frank Ch. Eigler <fche@redhat.com> - 0.5.12-1
- Many changes, see NEWS file.

* Tue Sep 26 2006 David Smith <dsmith@redhat.com> - 0.5.10-1
- Added 'systemtap-runtime' subpackage.

* Wed Jul 19 2006 Roland McGrath <roland@redhat.com> - 0.5.9-1
- PRs 2669, 2913

* Fri Jun 16 2006 Roland McGrath <roland@redhat.com> - 0.5.8-1
- PRs 2627, 2520, 2228, 2645

* Fri May  5 2006 Frank Ch. Eigler <fche@redhat.com> - 0.5.7-1
- PRs 2511 2453 2307 1813 1944 2497 2538 2476 2568 1341 2058 2220 2437
  1326 2014 2599 2427 2438 2465 1930 2149 2610 2293 2634 2506 2433

* Tue Apr  4 2006 Roland McGrath <roland@redhat.com> - 0.5.5-1
- Many changes, affected PRs include: 2068, 2293, 1989, 2334,
  1304, 2390, 2425, 953.

* Wed Feb  1 2006 Frank Ch. Eigler <fche@redhat.com> - 0.5.4-1
- PRs 1916, 2205, 2142, 2060, 1379

* Mon Jan 16 2006 Roland McGrath <roland@redhat.com> - 0.5.3-1
- Many changes, affected PRs include: 2056, 1144, 1379, 2057,
  2060, 1972, 2140, 2148

* Mon Dec 19 2005 Roland McGrath <roland@redhat.com> - 0.5.2-1
- Fixed build with gcc 4.1, various tapset changes.

* Wed Dec  7 2005 Roland McGrath <roland@redhat.com> - 0.5.1-1
- elfutils update, build changes

* Fri Dec 02 2005  Frank Ch. Eigler  <fche@redhat.com> - 0.5-1
- Many fixes and improvements: 1425, 1536, 1505, 1380, 1329, 1828, 1271,
  1339, 1340, 1345, 1837, 1917, 1903, 1336, 1868, 1594, 1564, 1276, 1295

* Mon Oct 31 2005 Roland McGrath <roland@redhat.com> - 0.4.2-1
- Many fixes and improvements: PRs 1344, 1260, 1330, 1295, 1311, 1368,
  1182, 1131, 1332, 1366, 1456, 1271, 1338, 1482, 1477, 1194.

* Wed Sep 14 2005 Roland McGrath <roland@redhat.com> - 0.4.1-1
- Many fixes and improvements since 0.2.2; relevant PRs include:
  1122, 1134, 1155, 1172, 1174, 1175, 1180, 1186, 1187, 1191, 1193, 1195,
  1197, 1205, 1206, 1209, 1213, 1244, 1257, 1258, 1260, 1265, 1268, 1270,
  1289, 1292, 1306, 1335, 1257

* Wed Sep  7 2005 Frank Ch. Eigler <fche@redhat.com>
- Bump version.

* Wed Aug 16 2005 Frank Ch. Eigler <fche@redhat.com>
- Bump version.

* Wed Aug  3 2005 Martin Hunt <hunt@redhat.com> - 0.2.2-1
- Add directory /var/cache/systemtap
- Add stp_check to /usr/libexec/systemtap

* Wed Aug  3 2005 Roland McGrath <roland@redhat.com> - 0.2.1-1
- New version 0.2.1, various fixes.

* Fri Jul 29 2005 Roland McGrath <roland@redhat.com> - 0.2-1
- New version 0.2, requires elfutils 0.111

* Mon Jul 25 2005 Roland McGrath <roland@redhat.com>
- Clean up spec file, build bundled elfutils.

* Thu Jul 21 2005 Martin Hunt <hunt@redhat.com>
- Set Version to use version from autoconf.
- Fix up some of the path names.
- Add Requires and BuildRequires.

* Wed Jul 19 2005 Will Cohen <wcohen@redhat.com>
- Initial creation of RPM.
