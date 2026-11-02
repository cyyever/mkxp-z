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
    # The focused SDK uses upstream CMake, so image/font Meson overlays must
    # not be applied here. SDL GPU and SDL_mixer decoder fixes are shared with runtime.
    patches = []
    if name in ("sdl3", "sdl3_mixer"):
        for entry in spec.get("diff_files", "").split(","):
            # end-of-diff-files.patch only exists for wrap syntax; it adds a dummy file.
            if entry.strip() and entry.strip() != "end-of-diff-files.patch":
                patches.append(repository / "subprojects" / "packagefiles" / entry.strip())
    source_state = spec["source_hash"]
    if patches:
        digest = hashlib.sha256(source_state.encode("ascii"))
        for patch in patches:
            digest.update(patch.name.encode("utf-8") + b"\0" + patch.read_bytes())
        source_state = digest.hexdigest()
    stamp = folder / ".source-sha256"
    if not stamp.is_file() or stamp.read_text() != source_state:
        with tarfile.open(archive) as package:
            package.extractall(sources, filter="data")
        for patch in patches:
            # Override the work tree: the extracted directory can be nested
            # inside a checkout and git must not patch the repository root.
            subprocess.run(["git", "--work-tree=.", "apply", "--ignore-whitespace", "-p1", str(patch)],
                           cwd=folder, check=True)
        stamp.write_text(source_state)
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

    # Only the header-only decoders the engine plays: no codec libraries or DLLs.
    mixer_options = ["-DSDLMIXER_TESTS=OFF", "-DSDLMIXER_EXAMPLES=OFF", "-DSDLMIXER_INSTALL=ON",
                     "-DSDLMIXER_INSTALL_CPACK=OFF", "-DSDLMIXER_VENDORED=OFF",
                     "-DSDLMIXER_WAVE=ON", "-DSDLMIXER_FLAC=ON", "-DSDLMIXER_FLAC_DRFLAC=ON",
                     "-DSDLMIXER_MP3=ON", "-DSDLMIXER_MP3_DRMP3=ON", "-DSDLMIXER_VORBIS_STB=ON"]
    mixer_options += [f"-DSDLMIXER_{backend}=OFF" for backend in
                      ("AIFF", "VOC", "AU", "FLAC_LIBFLAC", "GME", "MOD", "MP3_MPG123", "MIDI",
                       "OPUS", "VORBIS_VORBISFILE", "VORBIS_TREMOR", "WAVPACK")]
    install("sdl3_mixer", mixer_options)
    print(f"SDL3 test SDK installed in {prefix}")


if __name__ == "__main__":
    main()
