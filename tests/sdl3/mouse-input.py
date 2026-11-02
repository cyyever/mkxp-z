"""Windows native mouse messages: quick clicks between RGSS Input.update calls."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    if sys.platform != 'win32':
        raise SystemExit('This regression requires Windows window messages')
    binary = Path(sys.argv[1]).resolve()
    repository = Path(__file__).resolve().parents[2]
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    for hires in [False, True]:
        with tempfile.TemporaryDirectory(prefix='mkxp-mouse-') as temporary:
            fixture = Path(temporary)
            executable = fixture / binary.name
            shutil.copy2(binary, executable)
            script = fixture / 'probe.rb'
            script.write_text('''File.write('ready', 'ready')
sleep 0.4
triggers = releases = 0
30.times do
  Graphics.update
  Input.update
  if Input.trigger?(Input::MOUSELEFT)
    triggers += 1
  end
  releases += 1 if Input.release?(Input::MOUSELEFT)
end
File.write('result.txt', "#{triggers},#{releases}")
''', encoding='utf-8')
            configuration = dict(rgssVersion=1, gameFolder=str(fixture),
                customScript=str(script), RTP=[str(repository / 'assets')],
                defScreenW=640, defScreenH=480, fixedAspectRatio=True,
                fullscreen=False, enableSettings=False, winConsole=False,
                enableHires=hires, framebufferScalingFactor=3,
                windowTitle='SDL3 mouse regression')
            (fixture / 'mkxp.json').write_text(json.dumps(configuration), encoding='utf-8')
            (fixture / 'Game.ini').write_text('[Game]\nTitle=SDL3 mouse regression\nScripts=Data/Scripts.rxdata\n')
            with (fixture / 'stdout.log').open('w') as stdout, (fixture / 'stderr.log').open('w') as stderr:
                process = subprocess.Popen([str(executable)], cwd=fixture, stdout=stdout, stderr=stderr)
                windows = []

                @callback_type
                def collect(window, _):
                    pid = wintypes.DWORD()
                    user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                    title = ctypes.create_unicode_buffer(256)
                    user.GetWindowTextW(window, title, 256)
                    if pid.value == process.pid and title.value == 'SDL3 mouse regression':
                        windows.append(window)
                    return True

                try:
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline and process.poll() is None:
                        if (fixture / 'ready').exists():
                            user.EnumWindows(collect, 0)
                            if windows:
                                break
                        time.sleep(.01)
                    assert windows, 'engine window did not become ready'
                    window = windows[0]
                    # Activate only the test window's SDL event state. No physical
                    # pointer movement or clicks reach the user's other windows.
                    point = 100 + 100 * 65536
                    messages = [(0x6, 1, 0), (0x7, 0, 0), (0x200, 0, point),
                                (0x201, 1, point), (0x202, 0, point),
                                (0x201, 1, point), (0x202, 0, point)]
                    for message, button, position in messages:
                        assert user.PostMessageW(window, message, button, position)
                    process.wait(timeout=15)
                    assert process.returncode == 0, 'engine failed'
                    actual = (fixture / 'result.txt').read_text()
                    assert actual == '1,1', f'lost click/release: {actual}'
                    print(f'PASS: native mouse click/release between Input.update calls, hires={hires}')
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait()


if __name__ == '__main__':
    main()
