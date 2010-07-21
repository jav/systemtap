%{!?with_sqlite: %global with_sqlite 1}
%{!?with_docs: %global with_docs 1}
%{!?with_crash: %global with_crash 1}
%{!?with_rpm: %global with_rpm 1}
%{!?with_bundled_elfutils: %global with_bundled_elfutils 0}
%{!?elfutils_version: %global elfutils_version 0.127}
%{!?pie_supported: %global pie_supported 1}
%{!?with_grapher: %global with_grapher 1}
%{!?with_boost: %global with_boost 0}
%{!?with_publican: %global with_publican 1}
%{!?publican_brand: %global publican_brand fedora}

Name: systemtap
Version: 1.3
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
# Needed for libstd++ < 4.0, without <tr1/memory>
%if %{with_boost}
BuildRequires: boost-devel
%endif
%if %{with_crash}
BuildRequires: crash-devel zlib-devel
%endif
%if %{with_rpm}
BuildRequires: rpm-devel glibc-headers
%endif
# Alternate kernel packages kernel-PAE-devel et al have a virtual
# provide for kernel-devel, so this requirement does the right thing.
Requires: kernel-devel
Requires: gcc make
# Suggest: kernel-debuginfo
Requires: systemtap-runtime = %{version}-%{release}
BuildRequires: nss-tools nss-devel avahi-devel pkgconfig

%if %{with_bundled_elfutils}
Source1: elfutils-%{elfutils_version}.tar.gz
Patch1: elfutils-portability.patch
BuildRequires: m4
%global setup_elfutils -a1
%else
BuildRequires: elfutils-devel >= %{elfutils_version}
%endif

%if %{with_docs}
BuildRequires: /usr/bin/latex /usr/bin/dvips /usr/bin/ps2pdf latex2html
# On F10, xmlto's pdf support was broken off into a sub-package,
# called 'xmlto-tex'.  To avoid a specific F10 BuildReq, we'll do a
# file-based buildreq on '/usr/share/xmlto/format/fo/pdf'.
BuildRequires: xmlto /usr/share/xmlto/format/fo/pdf
%if %{with_publican}
BuildRequires: publican
BuildRequires: /usr/share/publican/Common_Content/%{publican_brand}/defaults.cfg
%endif
%endif

%if %{with_grapher}
BuildRequires: gtkmm24-devel >= 2.8
BuildRequires: libglademm24-devel >= 2.6.7
# If 'with_boost' isn't set, the boost-devel build requirement hasn't
# been specified yet.
%if ! %{with_boost}
BuildRequires: boost-devel
%endif
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
Requires: systemtap systemtap-sdt-devel dejagnu which prelink

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
Requires: zip unzip

%description client
This is the remote script compilation client component of systemtap.
It relies on a nearby compilation server to translate systemtap
scripts to kernel objects, so a client workstation only needs the
runtime, and not the compiler/etc toolchain.

%package server
Summary: Instrumentation System Server
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap
Requires: avahi avahi-tools nss nss-tools mktemp
Requires: zip unzip
Requires(post): chkconfig
Requires(preun): chkconfig
Requires(preun): initscripts
Requires(postun): initscripts

%description server
This is the remote script compilation server component of systemtap.
It announces itself to local clients with avahi, and compiles systemtap
scripts to kernel objects on their demand.

%package sdt-devel
Summary: Static probe support tools
Group: Development/System
License: GPLv2+, Public Domain
URL: http://sourceware.org/systemtap/

%description sdt-devel
Support tools to allow applications to use static probes.

%package initscript
Summary: Systemtap Initscripts
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap-runtime
Requires(post): chkconfig
Requires(preun): chkconfig
Requires(preun): initscripts
Requires(postun): initscripts

%description initscript
Initscript for Systemtap scripts

%if %{with_grapher}
%package grapher
Summary: Instrumentation System Grapher
Group: Development/System
License: GPLv2+
URL: http://sourceware.org/systemtap/
Requires: systemtap-runtime

%description grapher
SystemTap grapher is a utility for real-time visualization of
data from SystemTap instrumentation scripts.
%endif

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
%global elfutils_config --with-elfutils=elfutils-%{elfutils_version}

# We have to prevent the standard dependency generation from identifying
# our private elfutils libraries in our provides and requires.
%global _use_internal_dependency_generator	0
%global filter_eulibs() /bin/sh -c "%{1} | sed '/libelf/d;/libdw/d;/libebl/d'"
%global __find_provides %{filter_eulibs /usr/lib/rpm/find-provides}
%global __find_requires %{filter_eulibs /usr/lib/rpm/find-requires}

# This will be needed for running stap when not installed, for the test suite.
%global elfutils_mflags LD_LIBRARY_PATH=`pwd`/lib-elfutils
%endif

# Enable/disable the sqlite coverage testing support
%if %{with_sqlite}
%global sqlite_config --enable-sqlite
%else
%global sqlite_config --disable-sqlite
%endif

# Enable/disable the crash extension
%if %{with_crash}
%global crash_config --enable-crash
%else
%global crash_config --disable-crash
%endif

# Enable/disable the code to find and suggest needed rpms
%if %{with_rpm}
%global rpm_config --with-rpm
%else
%global rpm_config --without-rpm
%endif

%if %{with_docs}
%global docs_config --enable-docs
%else
%global docs_config --disable-docs
%endif

# Enable pie as configure defaults to disabling it
%if %{pie_supported}
%global pie_config --enable-pie
%else
%global pie_config --disable-pie
%endif

%if %{with_grapher}
%global grapher_config --enable-grapher
%else
%global grapher_config --disable-grapher
%endif

%if %{with_publican}
%global publican_config --enable-publican --with-publican-brand=%{publican_brand}
%else
%global publican_config --disable-publican
%endif


%configure %{?elfutils_config} %{sqlite_config} %{crash_config} %{docs_config} %{pie_config} %{grapher_config} %{publican_config} %{rpm_config} --disable-silent-rules
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

#install the useful stap-prep script
install -c -m 755 stap-prep $RPM_BUILD_ROOT%{_bindir}/stap-prep

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
%if %{with_publican}
mv $RPM_BUILD_ROOT%{_datadir}/doc/systemtap/SystemTap_Beginners_Guide docs.installed/
%endif
%endif

mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/
install -m 755 initscript/systemtap $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/systemtap
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/systemtap/conf.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/systemtap/script.d
install -m 644 initscript/config.systemtap $RPM_BUILD_ROOT%{_sysconfdir}/systemtap/config
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/cache/systemtap
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/run/systemtap

install -m 755 initscript/stap-server $RPM_BUILD_ROOT%{_sysconfdir}/rc.d/init.d/
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/stap-server
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/stap-server/conf.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig
install -m 644 initscript/config.stap-server $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/stap-server
mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/log/stap-server
touch $RPM_BUILD_ROOT%{_localstatedir}/log/stap-server/log
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
install -m 644 initscript/logrotate.stap-server $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d/stap-server

%clean
rm -rf ${RPM_BUILD_ROOT}

%pre
getent group stap-server >/dev/null || groupadd -g 155 -r stap-server || groupadd -r stap-server

%pre runtime
getent group stapdev >/dev/null || groupadd -r stapdev
getent group stapusr >/dev/null || groupadd -r stapusr
exit 0

%pre server
getent passwd stap-server >/dev/null || \
  useradd -c "Systemtap Compile Server" -u 155 -g stap-server -d %{_localstatedir}/lib/stap-server -m -r -s /sbin/nologin stap-server || \
  useradd -c "Systemtap Compile Server" -g stap-server -d %{_localstatedir}/lib/stap-server -m -r -s /sbin/nologin stap-server
test -e ~stap-server && chmod 755 ~stap-server
exit 0

%post server
test -e %{_localstatedir}/log/stap-server/log || {
     touch %{_localstatedir}/log/stap-server/log
     chmod 664 %{_localstatedir}/log/stap-server/log
     chown stap-server:stap-server %{_localstatedir}/log/stap-server/log
}
# If it does not already exist, as stap-server, generate the certificate
# used for signing and for ssl.
if test ! -e ~stap-server/.systemtap/ssl/server/stap.cert; then
   runuser -s /bin/sh - stap-server -c %{_libexecdir}/%{name}/stap-gen-cert >/dev/null
   # Authorize the certificate as a trusted ssl peer and as a trusted signer
   # on the local host.
   %{_bindir}/stap-authorize-server-cert ~stap-server/.systemtap/ssl/server/stap.cert
   %{_bindir}/stap-authorize-signing-cert ~stap-server/.systemtap/ssl/server/stap.cert
fi

# Activate the service
/sbin/chkconfig --add stap-server
exit 0

%preun server
# Check that this is the actual deinstallation of the package, as opposed to
# just removing the old package on upgrade.
if [ $1 = 0 ] ; then
    /sbin/service stap-server stop >/dev/null 2>&1
    /sbin/chkconfig --del stap-server
fi
exit 0

%postun server
# Check whether this is an upgrade of the package.
# If so, restart the service if it's running
if [ "$1" -ge "1" ] ; then
    /sbin/service stap-server condrestart >/dev/null 2>&1 || :
fi
exit 0

%post initscript
/sbin/chkconfig --add systemtap
exit 0

%preun initscript
# Check that this is the actual deinstallation of the package, as opposed to
# just removing the old package on upgrade.
if [ $1 = 0 ] ; then
    /sbin/service systemtap stop >/dev/null 2>&1
    /sbin/chkconfig --del systemtap
fi
exit 0

%postun initscript
# Check whether this is an upgrade of the package.
# If so, restart the service if it's running
if [ "$1" -ge "1" ] ; then
    /sbin/service systemtap condrestart >/dev/null 2>&1 || :
fi
exit 0

%post
# Remove any previously-built uprobes.ko materials
(make -C %{_datadir}/%{name}/runtime/uprobes clean) >/dev/null 2>&1 || true
(/sbin/rmmod uprobes) >/dev/null 2>&1 || true

%preun
# Ditto
(make -C %{_datadir}/%{name}/runtime/uprobes clean) >/dev/null 2>&1 || true
(/sbin/rmmod uprobes) >/dev/null 2>&1 || true

%files
%defattr(-,root,root)

%doc README AUTHORS NEWS COPYING examples
%if %{with_docs}
%doc docs.installed/*.pdf
%doc docs.installed/tapsets
%if %{with_publican}
%doc docs.installed/SystemTap_Beginners_Guide
%endif
%endif

%{_bindir}/stap
%{_bindir}/stap-prep
%{_bindir}/stap-report
%{_mandir}/man1/*
%{_mandir}/man3/*
%{_mandir}/man7/stappaths.7*

%dir %{_datadir}/%{name}
%{_datadir}/%{name}/runtime
%{_datadir}/%{name}/tapset

%if %{with_bundled_elfutils}
%{_libdir}/%{name}/lib*.so*
%endif

# Make sure that the uprobes module can be built by root and by the server
%dir %attr(0775,root,stap-server) %{_datadir}/%{name}/runtime/uprobes

%files runtime
%defattr(-,root,root)
%attr(4111,root,root) %{_bindir}/staprun
%{_bindir}/stap-report
%{_bindir}/stap-authorize-signing-cert
%{_libexecdir}/%{name}/stapio
%{_libexecdir}/%{name}/stap-env
%{_libexecdir}/%{name}/stap-authorize-cert
%if %{with_crash}
%{_libdir}/%{name}/staplog.so*
%endif
%{_mandir}/man7/stappaths.7*
%{_mandir}/man8/staprun.8*
%{_mandir}/man8/stap-authorize-signing-cert.8*

%doc README AUTHORS NEWS COPYING

%files testsuite
%defattr(-,root,root)
%{_datadir}/%{name}/testsuite

%files client
%defattr(-,root,root)
%{_bindir}/stap-client
%{_bindir}/stap-authorize-server-cert
%{_libexecdir}/%{name}/stap-find-servers
%{_libexecdir}/%{name}/stap-client-connect
%{_mandir}/man7/stappaths.7*
%{_mandir}/man8/stap-client.8*
%{_mandir}/man8/stap-authorize-server-cert.8*

%files server
%defattr(-,root,root)
%{_bindir}/stap-authorize-server-cert
%{_bindir}/stap-server
%{_libexecdir}/%{name}/stap-serverd
%{_libexecdir}/%{name}/stap-start-server
%{_libexecdir}/%{name}/stap-find-servers
%{_libexecdir}/%{name}/stap-find-or-start-server
%{_libexecdir}/%{name}/stap-stop-server
%{_libexecdir}/%{name}/stap-gen-cert
%{_libexecdir}/%{name}/stap-server-connect
%{_libexecdir}/%{name}/stap-sign-module
%{_mandir}/man7/stappaths.7*
%{_mandir}/man8/stap-server.8*
%{_mandir}/man8/stap-authorize-server-cert.8*
%{_sysconfdir}/rc.d/init.d/stap-server
%config(noreplace) %{_sysconfdir}/logrotate.d/stap-server
%dir %{_sysconfdir}/stap-server
%dir %{_sysconfdir}/stap-server/conf.d
%config(noreplace) %{_sysconfdir}/sysconfig/stap-server
%dir %attr(0755,stap-server,stap-server) %{_localstatedir}/log/stap-server
%ghost %config %attr(0644,stap-server,stap-server) %{_localstatedir}/log/stap-server/log
%doc initscript/README.stap-server

%files sdt-devel
%defattr(-,root,root)
%{_bindir}/dtrace
%{_includedir}/sys/sdt.h
%doc README AUTHORS NEWS COPYING

%files initscript
%defattr(-,root,root)
%{_sysconfdir}/rc.d/init.d/systemtap
%dir %{_sysconfdir}/systemtap
%dir %{_sysconfdir}/systemtap/conf.d
%dir %{_sysconfdir}/systemtap/script.d
%config(noreplace) %{_sysconfdir}/systemtap/config
%dir %{_localstatedir}/cache/systemtap
%dir %{_localstatedir}/run/systemtap
%doc initscript/README.systemtap

%if %{with_grapher}
%files grapher
%defattr(-,root,root)
%{_bindir}/stapgraph
%{_datadir}/%{name}/*.glade
%endif


%changelog
* Wed Jul 21 2010 Josh Stone <jistone@redhat.com> - 1.3-1
- Upstream release.

* Mon Mar 22 2010 Frank Ch. Eigler <fche@redhat.com> - 1.2-1
- Upstream release.

* Mon Dec 21 2009 David Smith <dsmith@redhat.com> - 1.1-1
- Upstream release.

* Tue Sep 22 2009 Josh Stone <jistone@redhat.com> - 1.0-1
- Upstream release.

* Tue Aug  4 2009 Josh Stone <jistone@redhat.com> - 0.9.9-1
- Upstream release.

* Thu Jun 11 2009 Josh Stone <jistone@redhat.com> - 0.9.8-1
- Upstream release.

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
