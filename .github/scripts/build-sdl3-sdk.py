"""Build the focused SDL3 CI SDK from the repository's hashed source wraps."""

import argparse
import configparser
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request


def source(repository: Path, build: Path, name: str) -> Path:
    wrap = configparser.ConfigParser(interpolation=None)
    wrap.read(repository / "subprojects" / f"{name}.wrap")
    spec = wrap["wrap-file"]
    archive = repository / "subprojects" / "packagecache" / spec["source_filename"]
    if not archive.is_file():
        archive = build / "downloads" / spec["source_filename"]
        archive.parent.mkdir(parents=True, exist_ok=True)
        if not archive.is_file():
            request = urllib.request.Request(spec["source_url"], headers={"User-Agent": "mkxp-sdl3-ci"})
            with urllib.request.urlopen(request, timeout=60) as response, archive.open("wb") as output:
                shutil.copyfileobj(response, output)
    with archive.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != spec["source_hash"]:
            raise RuntimeError(f"Source hash mismatch: {archive}")
    sources = build / "sources"
    sources.mkdir(parents=True, exist_ok=True)
    folder = sources / spec["directory"]
    stamp = folder / ".source-sha256"
    if not stamp.is_file() or stamp.read_text() != spec["source_hash"]:
        with tarfile.open(archive) as package:
            package.extractall(sources, filter="data")
        stamp.write_text(spec["source_hash"])
    return folder


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-root", type=Path, default=Path("build/sdl3-sdk"))
    parser.add_argument("--prefix", type=Path, default=Path("build/sdk"))
    args = parser.parse_args()
    repository = args.repository.resolve()
    build = args.build_root.resolve()
    prefix = args.prefix.resolve()
    ninja = shutil.which("ninja")
    if ninja is None:
        raise RuntimeError("Ninja is required")

    common = [
        "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={Path(ninja).as_posix()}",
        "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}",
        f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}", "-DCMAKE_INSTALL_LIBDIR=lib",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_SHARED_LIBS=ON",
    ]
    if sys.platform == "win32":
        common += ["-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"]

    def install(name: str, options: list[str]) -> None:
        directory = build / name
        subprocess.run(["cmake", "-S", str(source(repository, build, name)), "-B", str(directory),
                        *common, *options], check=True)
        subprocess.run(["cmake", "--build", str(directory), "--parallel", "4"], check=True)
        subprocess.run(["cmake", "--install", str(directory)], check=True)

    sdl_options = ["-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST_LIBRARY=OFF",
                   "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_VIRTUAL_JOYSTICK=ON"]
    if sys.platform.startswith(("linux", "freebsd")):
        # The focused tests use dummy video/audio and software surfaces.
        sdl_options += ["-DSDL_UNIX_CONSOLE_BUILD=ON", "-DSDL_X11=OFF", "-DSDL_WAYLAND=OFF"]
    install("sdl3", sdl_options)

    install("freetype", ["-DBUILD_SHARED_LIBS=OFF", *[
        f"-DFT_DISABLE_{dependency}=ON" for dependency in ("ZLIB", "BZIP2", "PNG", "HARFBUZZ", "BROTLI")
    ]])
    image_options = ["-DSDLIMAGE_SAMPLES=OFF", "-DSDLIMAGE_TESTS=OFF", "-DSDLIMAGE_INSTALL=ON",
                     "-DSDLIMAGE_INSTALL_CPACK=OFF", "-DSDLIMAGE_VENDORED=OFF",
                     "-DSDLIMAGE_BACKEND_STB=ON", "-DSDLIMAGE_BACKEND_IMAGEIO=OFF",
                     "-DSDLIMAGE_BACKEND_WIC=OFF", "-DSDLIMAGE_PNG=ON", "-DSDLIMAGE_JPG=ON",
                     "-DSDLIMAGE_PNG_SAVE=ON", "-DSDLIMAGE_JPG_SAVE=ON"]
    image_options += [f"-DSDLIMAGE_{format}=OFF" for format in
                      ("AVIF", "BMP", "GIF", "JXL", "LBM", "PCX", "PNM", "QOI", "SVG",
                       "TGA", "TIF", "WEBP", "XCF", "XPM", "XV")]
    install("sdl3_image", image_options)
    install("sdl3_ttf", ["-DSDLTTF_SAMPLES=OFF", "-DSDLTTF_INSTALL=ON",
                         "-DSDLTTF_INSTALL_CPACK=OFF", "-DSDLTTF_VENDORED=OFF",
                         "-DSDLTTF_HARFBUZZ=OFF", "-DSDLTTF_PLUTOSVG=OFF"])
    print(f"SDL3 test SDK installed in {prefix}")


if __name__ == "__main__":
    main()
