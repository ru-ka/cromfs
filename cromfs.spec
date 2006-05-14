# $Revision: 1.1 $, $Date: 2006/05/14 15:13:44 $
Summary:	Compressed ROM filesystem for Linux in user-space
Name:		cromfs
Version:	1.1.0.2
Release:	0.1
License:	GPL
Group:		Applications
Source0:	http://bisqwit.iki.fi/src/arch/%{name}-%{version}.tar.bz2
# Source0-md5:	54b91063f21781330a8a7ef27faae19c
URL:		http://bisqwit.iki.fi/source/cromfs.html
BuildRequires:	libfuse-devel
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%description
Cromfs is a compressed read-only filesystem for Linux. Cromfs is
intended for permanently archiving gigabytes of big files that have
lots of redundancy.

In terms of compression it is much similar to 7-zip files, except that
practical realtime access (albeit much slower than on most other
filesystems) can be provided for the whole archive contents; the user
does not need to launch a program to decompress a single file, nor
does he need to wait while the system decompresses 500 files from a
1000-file archive to get him the 1 file he wanted to open.

The creation of cromfs was inspired from Squashfs and Cramfs.

%prep
%setup -q

%build
%{__make} \
	CXX="%{__cxx}" \
	CXXFLAGS="%{rpmcflags}"

%install
rm -rf $RPM_BUILD_ROOT

install -d $RPM_BUILD_ROOT%{_bindir}
install  cromfs-driver $RPM_BUILD_ROOT%{_bindir}
install  util/mkcromfs $RPM_BUILD_ROOT%{_bindir}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(644,root,root,755)
%doc doc/ChangeLog doc/FORMAT README.html
%attr(755,root,root) %{_bindir}/*

%define date	%(echo `LC_ALL="C" date +"%a %b %d %Y"`)
%changelog
* %{date} PLD Team <feedback@pld-linux.org>
All persons listed below can be reached at <cvs_login>@pld-linux.org

$Log: cromfs.spec,v $
Revision 1.1  2006/05/14 15:13:44  areq
- Init PLD spec

