"""Build the embedded Ruby with MSVC through Ruby's own mswin build system.

The autoconf build used for GCC/Clang cannot drive cl.exe, so MSVC builds use
win32/configure.bat and nmake. Only the zlib extension is built, statically. The engine links the static
library straight from the build tree; the standard library is not installed.

Usage: build-msvc.py SRCDIR BUILDDIR DESTDIR CFLAGS LDFLAGS STAMP
CFLAGS/LDFLAGS are the shell-quoted -I/-L flags Meson passes for zlib.
"""
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

srcdir, builddir, destdir, cflags, ldflags, stamp = sys.argv[1:7]
srcdir = Path(srcdir)
builddir = Path(builddir)
destdir = Path(destdir)
builddir.mkdir(parents=True, exist_ok=True)

env = dict(os.environ)
# configure.bat and nmake run generated batch files from the current directory.
env.pop('NoDefaultCurrentDirectoryInExePath', None)
# Ruby bootstrap installers may export GNU make; recursive mswin builds need nmake.
env['MAKE'] = 'nmake'
env.pop('MAKEFLAGS', None)
env.pop('MFLAGS', None)

# Ruby's mkmf looks for zlib under --with-opt-dir as include/zlib.h and lib/zlib.lib.
optdir = builddir / 'zlib-opt'
(optdir / 'include').mkdir(parents=True, exist_ok=True)
(optdir / 'lib').mkdir(parents=True, exist_ok=True)
include_dirs = [Path(arg[2:]) for arg in shlex.split(cflags) if arg.startswith('-I')]
library_dirs = [Path(arg[2:]) for arg in shlex.split(ldflags) if arg.startswith('-L')]
for header in ('zlib.h', 'zconf.h'):
    found = next((d / header for d in include_dirs if (d / header).is_file()), None)
    if found is None:
        sys.exit(f'build-msvc.py: {header} not found in {include_dirs}')
    shutil.copy2(found, optdir / 'include' / header)
library_names = ('z.lib', 'libz.a', 'zs.lib', 'zlibstatic.lib', 'libzlibstatic.a', 'zlib.lib', 'libz.lib')
library = next((d / n for d in library_dirs for n in library_names if (d / n).is_file()), None)
if library is None:
    sys.exit(f'build-msvc.py: static zlib not found in {library_dirs}')
shutil.copy2(library, optdir / 'lib' / 'zlib.lib')


def run(*command):
    print('+', ' '.join(map(str, command)), flush=True)
    subprocess.run([str(c) for c in command], cwd=builddir, env=env, check=True)


if not (builddir / 'Makefile').is_file():
    run(
        srcdir / 'win32' / 'configure.bat',
        '--prefix=' + destdir.as_posix(),
        '--disable-install-doc',
        '--disable-rubygems',
        '--with-static-linked-ext',
        '--with-ext=zlib',
        '--with-opt-dir=' + optdir.as_posix(),
    )
# The rubyspec C-API test extensions link against the DLL import library, which cannot work with our
# dllimport-free headers; they are not needed, so point their target list at a placeholder.
run('nmake', 'main', 'RUBYSPEC_CAPIEXT_EXTS=rubyspec-capiext-skipped')
Path(stamp).write_text('')
