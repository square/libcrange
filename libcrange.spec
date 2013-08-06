Name:     libcrange
Version:  1.0.2
Release:  1%{?dist}
Summary:  C version of range

Group:      Base
License:    GPL
URL:        http://github.com/boinger/libcrange
Source0:    %{name}-latest.tar.gz
BuildRoot:  %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires: apr-devel, libyaml-devel, pcre-devel, perl-devel, sqlite-devel
Requires: apr, flex, libyaml, pcre, perl, perl-YAML-Syck, perl-core, perl-libs, sqlite


%description
A library for parsing and generating range expressions.


%prep
%setup -q -n source


%build
aclocal || exit 1
libtoolize --force || exit 1
autoheader || exit 1
automake -a || exit 1
autoconf || exit 1
%configure --prefix=/usr
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%doc
%{_bindir}/crange
%{_includedir}/libcrange.h
%{_libdir}/libcrange*


%changelog

* Mon Aug 05 2013 Jeff Vier <jeff@jeffvier.com> - 1.0.2-1
- add Requires/BuildRequires

