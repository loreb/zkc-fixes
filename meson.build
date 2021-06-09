project(
	'zkc',
	'c',
	version: '0.1.0',
	license: 'GPL',
	meson_version: '>=0.51.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-D_POSIX_C_SOURCE=200809L',
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wmissing-prototypes',
	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

app_inc = include_directories('include')

sqlite3 = dependency('sqlite3')
uuid = dependency('uuid')

src_files = [
	'src/main.c',
	'src/app.c'
]

executable(
	'zkc',
	files(src_files),
	install: true,
	dependencies: [sqlite3, uuid],
	include_directories: [app_inc],
	#link_args: ['-static']
)
