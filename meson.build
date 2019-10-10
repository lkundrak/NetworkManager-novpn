# nm-novpn-service - Mock VPN service for NetworkManager
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# (C) Copyright 2018 Lubomir Rintel

project('NetworkManager-novpn', 'c', version: '1.8')

gnome = import('gnome')

glib2 = dependency('glib-2.0', version: '>= 2.40')
gtk3 = dependency('gtk+-3.0', version: '>= 3.10')
libnm = dependency('libnm', version: '>= 1.4')
libnma = dependency('libnma', version: '>= 1.8')
dl = meson.get_compiler('c').find_library('dl')

extra_args = [
	'-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_40',
	'-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_40',
	'-DGTK_VERSION_MIN_REQUIRED=GTK_VERSION_3_0',
	'-DGTK_VERSION_MAX_ALLOWED=GTK_VERSION_3_0',
	'-DNM_VERSION_MIN_REQUIRED=NM_VERSION_1_4',
	'-DNM_VERSION_MAX_ALLOWED=NM_VERSION_1_4',
	'-DNMA_VERSION_MIN_REQUIRED=NMA_VERSION_1_8_16',
	'-DNMA_VERSION_MAX_ALLOWED=NMA_VERSION_1_8_16',
]

service_data = configuration_data()
service_data.set('LIBEXECDIR', join_paths(get_option('prefix'), get_option('libexecdir')))

shared_library('nm-novpn-editor-plugin',
	'nm-novpn-editor-plugin.c',
	dependencies: [glib2, libnm, dl],
	c_args: extra_args,
	install: true,
	install_dir: join_paths(get_option('libdir'), 'NetworkManager'))

shared_library('nm-novpn-editor',
	'nm-novpn-editor.c',
	gnome.compile_resources('nm-novpn-resources', 'nm-novpn.gresource.xml', source_dir: 'gtk3'),
	dependencies: [glib2, libnm, libnma, gtk3],
	c_args: extra_args,
	install: true,
	install_dir: join_paths(get_option('libdir'), 'NetworkManager'))

configure_file(input: 'nm-novpn-service.name.in',
	output: 'nm-novpn-service.name',
	configuration: service_data,
	install: true,
	install_dir: libnm.get_pkgconfig_variable('vpnservicedir'))

executable('nm-novpn-auth-dialog-gui',
	'nm-novpn-auth-dialog-gui.c',
	dependencies: [glib2, libnma, gtk3],
	c_args: extra_args,
	install: true,
	install_dir: get_option('libexecdir'))

executable('nm-novpn-auth-dialog',
	'nm-novpn-auth-dialog.c',
	dependencies: [glib2, libnm],
	c_args: extra_args,
	install: true,
	install_dir: get_option('libexecdir'))

install_data('nm-novpn-service.conf',
	install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'dbus-1', 'system.d'))

executable('nm-novpn-service',
	'nm-novpn-service.c',
	dependencies: [glib2, libnm],
	c_args: extra_args,
	install: true,
	install_dir: get_option('libexecdir'))

executable('run-vpn',
	'run-vpn.c',
	dependencies: [glib2, libnm, gtk3],
	c_args: extra_args)

enable_gtk4 = get_option('gtk4')
if enable_gtk4
	gtk4 = dependency('gtk4', version: '>= 3.96')
	libnma_gtk4 = dependency('libnma-gtk4')

	shared_library('nma-gtk4-novpn-editor',
		'nm-novpn-editor.c',
		gnome.compile_resources('nma-gtk4-novpn-resources', 'nm-novpn.gresource.xml', source_dir: 'gtk4'),
		dependencies: [glib2, libnm, libnma_gtk4, gtk4],
		c_args: extra_args,
		install: true,
		install_dir: join_paths(get_option('libdir'), 'NetworkManager'))

	executable('run-vpn-gtk4',
		'run-vpn.c',
		dependencies: [glib2, libnm, gtk4],
		c_args: extra_args)
endif
