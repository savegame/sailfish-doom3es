%if "%{?_game}" == "prey"

Name: ru.sashikknox.prey
Summary: Prey AuroraOS port by sashikknox
%global binary_name prey
%define build_options -DPREY=ON -DSFOS_PACKAGE_NAME=%{name}
%define _GAMENAME Prey4Aurora
%define dir_suffix "_prey"
%else

Name: ru.sashikknox.doom3es
Summary: Doom3 AuroraOS port by sashikknox
%global binary_name doom3es
%define build_options ""
%define _GAMENAME DIII4Aurora
%define dir_suffix ""
%define _game doom3es
%endif

%define firejail_section X-Application
%define xauroraapp ""

%define __requires_exclude ^libopenal\\.so.*$
%define __provides_exclude_from ^%{_datadir}/%{name}/lib/.*\\.so.*$

Version:    1.5.3
Release:    1
Group:      Amusements/Games
License:    GPLv3
Source0:    sailfish-doom3es-%{version}.tar.bz2
BuildRequires: pkgconfig(openal)
BuildRequires: pkgconfig(libcurl)
BuildRequires: cmake
BuildRequires: gcc
BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(mce)
BuildRequires: pkgconfig(wayland-egl)
BuildRequires: pkgconfig(wayland-client)
BuildRequires: pkgconfig(wayland-cursor)
BuildRequires: pkgconfig(wayland-protocols)
BuildRequires: pkgconfig(wayland-scanner)
BuildRequires: pkgconfig(egl)
BuildRequires: pkgconfig(glesv1_cm)
BuildRequires: pkgconfig(glesv2)
BuildRequires: pkgconfig(xkbcommon)
BuildRequires: pkgconfig(gbm)
BuildRequires: pkgconfig(udev)
BuildRequires: pkgconfig(libpulse)
BuildRequires: rsync

%description
Doom3 AuroraOS port by sashikknox. Doom 3 made by Id software.

%prep
# >> setup
%setup -q -n sailfish-doom3es-%{version}
# << setup

%build
# >> build pre
sed "s/__APPNAME__/%{name}/g" sailfish-doom3es.desktop.in>%{name}.desktop
sed -i "s/__FIREJAIL__/%{firejail_section}/g" %{name}.desktop
sed -i "s/__X_AURORA_APP__/%{xauroraapp}/g" %{name}.desktop
sed -i "s/__GAME__/%{binary_name}/g" %{name}.desktop
sed -i "s/__GAME_NAME__/%{_GAMENAME}/g" %{name}.desktop
sed -i "s/#/\n/g" %{name}.desktop
# << build pre

%cmake \
    -Bbuild_libsdl_%{_arch} \
    -DSDL_PULSEAUDIO=OFF \
    -DSDL_RPATH=OFF \
    -DSDL_STATIC=ON \
    -DSDL_SHARED=OFF \
    -DSDL_WAYLAND=ON \
    -DSDL_X11=OFF \
    -DSDL_DBUS=ON \
    -DSDL_WAYLAND_LIBDECOR=OFF \
    libsdl

pushd build_libsdl_%{_arch}
%make_build -j`nproc`
rsync -avP include-config-/SDL2/* include/SDL2/
popd

%cmake \
    -Bbuild_%{_arch}%{dir_suffix} \
    -DCMAKE_BUILD_TYPE=Release \
    -DSFOS_PACKAGE_NAME="%{name}" \
    -DSAILFISHOS=ON \
    -DSDL2_INCLUDE_DIR="`pwd`/build_libsdl_%{_arch}/include/SDL2" \
    -DSDL2_LIBRARY="`pwd`/build_libsdl_%{_arch}/libSDL2.a" \
    %{build_options} .

pushd build_%{_arch}%{dir_suffix}
# %make_build 
make -j1
strip neo/base.so
[ -f neo/d3xp.so ] && strip neo/d3xp.so
strip neo/%{name}
popd
# >> build post
# << build post

%install
rm -rf %{buildroot}
# >> install pre
# << install pre
pushd build_%{_arch}%{dir_suffix}
%make_install
popd
# >> install post
# copy openal from build engine
install -D %{_libdir}/libopenal.so.1* -t %{buildroot}%{_datadir}/%{name}/lib/
install -m 655 -D base/gamecontrollerdb.txt -t %{buildroot}%{_datadir}/%{name}/
install -m 655 -D icons/512.png %{buildroot}%{_datadir}/%{name}/res/512.png
install -m 655 -D res/sashikknox.png -t %{buildroot}%{_datadir}/%{name}/res/
install -m 655 -D res/PIXY.ttf -t %{buildroot}%{_datadir}/%{name}/res/
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