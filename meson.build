project('wf-dynspaces', ['cpp'], version: '0.0.1', license: 'Unlicense',
	default_options: ['cpp_std=c++17', 'warning_level=2'])

wayfire = dependency('wayfire')
wlroots = dependency('wlroots', fallback: ['wlroots', 'wlroots'])

plugin_dynspaces = shared_module('dynspaces',
	'dynspaces.cpp',
	dependencies: [wayfire, wlroots],
	install: true,
	install_dir: 'lib/wayfire')

install_data('dynspaces.xml', install_dir: 'share/wayfire/metadata')
