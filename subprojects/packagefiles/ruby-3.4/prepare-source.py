"""Prepare the Ruby source tree for the minimal embedded runtime.

mkxp-z only links the zlib extension; no bundled gems are installed.
"""
from pathlib import Path
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

# Do not install bundled gems, including on cross builds.
(root / 'gems/bundled_gems').write_text('# No bundled gems in the embedded runtime.\n')
