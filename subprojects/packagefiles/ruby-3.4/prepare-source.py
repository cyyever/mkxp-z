"""Keep former default libraries available without RubyGems in embedded Ruby.

Ruby's release archive includes the exact bundled gem sources. Promote the
libraries that used to ship with our Ruby 3.1 runtime into the core build, so
their C extensions are linked statically and their Ruby files stay on $LOAD_PATH.
No downloads or host-installed gems are used.
"""
from pathlib import Path
import shutil
import sys

root = Path(sys.argv[1])

# Ruby's debug-only callinfo dump references this macro unconditionally even
# when the configure probe cannot provide it (for example, on current macOS
# Clang). Keep the debug helper portable with the standard C function name.
internal = root / 'internal.h'
text = internal.read_text()
if '#ifndef RUBY_FUNCTION_NAME_STRING' not in text:
    text = text.replace(
        '/* MRI debug support */',
        '#ifndef RUBY_FUNCTION_NAME_STRING\n'
        '#define RUBY_FUNCTION_NAME_STRING __func__\n'
        '#endif\n\n'
        '/* MRI debug support */',
        1,
    )
    internal.write_text(text, newline='\n')

libraries = (
    'abbrev', 'base64', 'benchmark', 'bigdecimal', 'csv', 'drb', 'fiddle',
    'getoptlong', 'logger', 'mutex_m', 'nkf', 'observer', 'ostruct', 'pstore',
    'racc', 'resolv-replace', 'rinda', 'syslog', 'win32ole',
)
for name in libraries:
    matches = list((root / '.bundle/gems').glob(name + '-[0-9]*'))
    # Ruby 3.4 still ships some of these libraries in the core tree.
    if not matches and ((root / 'lib' / (name + '.rb')).is_file()
                        or (root / 'ext' / name).is_dir()):
        continue
    if len(matches) != 1:
        raise RuntimeError(f'Expected one bundled source for {name}, got {matches}')
    gem = matches[0]
    for directory in ('lib', 'ext'):
        source = gem / directory
        if source.is_dir():
            shutil.copytree(source, root / directory, dirs_exist_ok=True)

# Ruby 3.1 did not install the optional bundled gems. The selected libraries
# above are installed with core Ruby instead, including on cross builds.
(root / 'gems/bundled_gems').write_text('# Promoted compatibility libraries are built in ext/.\n')

# Let Meson's supplied include/link directories select libffi. pkg-config can
# otherwise find the build machine's libffi during cross compilation.
fiddle = root / 'ext/fiddle/extconf.rb'
text = fiddle.read_text()
start = text.index('unless bundle\n')
end = text.index('\nunless have_libffi', start)
text = text[:start] + '''unless bundle
  ffi_header = 'ffi.h'
  have_ffi_header = have_header(ffi_header)
  have_libffi = have_ffi_header && have_library('ffi')
end
''' + text[end:]
fiddle.write_text(text, newline='\n')
