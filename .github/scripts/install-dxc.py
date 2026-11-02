"""Install the pinned official DXC release used to build SPIR-V shaders on Linux CI.

The SPIR-V artifacts depend on the exact DXC build, so CI pins one release by URL and
SHA-256 instead of using whatever a runner or distribution happens to ship.
"""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = "v1.9.2609"
URL = ("https://github.com/microsoft/DirectXShaderCompiler/releases/download/"
       f"{VERSION}/linux_dxc_2026_09_28.x86_x64.tar.gz")
SHA256 = "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    destination = parser.parse_args().destination.resolve()
    archive = destination.with_name(destination.name + ".tar.gz")
    archive.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(URL, headers={"User-Agent": "mkxp-dxc-ci"})
    with urllib.request.urlopen(request, timeout=120) as response, archive.open("wb") as output:
        shutil.copyfileobj(response, output)
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != SHA256:
        raise RuntimeError(f"DXC archive hash mismatch: {digest}")
    shutil.rmtree(destination, ignore_errors=True)
    with tarfile.open(archive) as tar:
        tar.extractall(destination, filter="data")
    archive.unlink()
    dxc = destination / "bin" / "dxc"
    dxc.chmod(0o755)
    subprocess.run([dxc, "--version"], check=True)
    if "GITHUB_PATH" in os.environ:
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as path:
            path.write(f"{dxc.parent}\n")


if __name__ == "__main__":
    main()
