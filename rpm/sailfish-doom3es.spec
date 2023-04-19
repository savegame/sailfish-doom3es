%if "0%{?_special_name}" != "0"
Name:  %_special_name
%else
Name:  harbour-doom3es
%endif

%define build_dir $RPM_BUILD_ROOT/build

%ifarch armv7hl
%global build_dir build_armv7hl
%else 
    %ifarch aarch64
        %global build_dir build_aarch64
    %else
        %global build_dir build_x86
    %endif
%endif

Summary:    Doom3 AuroraOS/SailfishOS port by sashikknox
Version:    1.5.3
Release:    1
Group:      Amusements/Games
License:    GPLv3
Source0:    sailfish-doom3es-%{version}.tar.bz2
Requires:   OpenAL
BuildRequires: pkgconfig(openal)
BuildRequires: cmake
BuildRequires: dbus-devel
BuildRequires: pkgconfig(mce)
BuildRequires: pkgconfig(wayland-egl)
BuildRequires: pkgconfig(wayland-client)
BuildRequires: pkgconfig(wayland-cursor)
BuildRequires: pkgconfig(wayland-protocols)
BuildRequires: pkgconfig(wayland-scanner)
BuildRequires: pkgconfig(egl)
BuildRequires: pkgconfig(glesv2)
BuildRequires: pkgconfig(xkbcommon)

%description
Doom3 AuroraOS/SailfishOS port by sashikknox. Doom 3 made by Id software.

%prep
# >> setup
%setup -q -n sailfish-doom3es-%{version}
# << setup

%build
# >> build pre
# << build pre
sed "s/__APPNAME__/%{name}/g" sailfish-doom3es.desktop.in>%{name}.desktop
mkdir -p %{build_dir}
cd %{build_dir}
cmake \
    -DCMAKE_BUILD_TYPE=Relese \
    -DSFOS_PACKAGE_NAME="%{name}" \
    -DSAILFISHOS=ON \
    ../
%make_build
strip neo/base.so
strip neo/d3xp.so
strip neo/%{name}
# >> build post
# << build post

%install
rm -rf %{buildroot}
# >> install pre
# << install pre
cd %{build_dir}
%make_install
# >> install post
# << install post

# desktop-file-install --delete-original       \
#   --dir %{buildroot}%{_datadir}/applications             \
#    %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png


%changelog
* Wed Apr 12 2023 sashikknox <sashikknox@gmail.com> 1.5.3
- add touchscreen controls
- add automatic screen rotation