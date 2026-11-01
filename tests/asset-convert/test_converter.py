"""Exercise the native converter using generated lossless fixtures; no game assets."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import zlib

TOOL = Path(sys.argv[1]).resolve()


def invoke(kind: str, source: Path, target: Path, success: bool = True):
    result = subprocess.run([str(TOOL), kind, str(source), str(target)],
                            capture_output=True, text=True, encoding="utf-8")
    if success:
        if result.returncode:
            raise AssertionError(result.stderr)
        return json.loads(result.stdout)
    if result.returncode == 0:
        raise AssertionError("converter unexpectedly accepted unsafe/invalid output")
    return None


def chunk(name: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))


def png_fixture() -> bytes:
    # All alpha cases, including hidden RGB under fully transparent pixels.
    pixels = bytes([255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0,
                    17, 33, 65, 255, 0, 0, 0, 255, 255, 255, 255, 255])
    rows = b"\0" + pixels[:12] + b"\0" + pixels[12:]
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 3, 2, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def wave_fixture(samples: bytes, bits: int, floating: bool, rate: int = 8000, channels: int = 1, format_tag: int | None = None) -> bytes:
    frame_size = channels * bits // 8
    format_tag = format_tag if format_tag is not None else (3 if floating else 1)
    fmt = struct.pack("<HHIIHH", format_tag, channels, rate, rate * frame_size, frame_size, bits)
    if format_tag != 1:
        fmt += b"\0\0"
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt
    if format_tag != 1:
        body += b"fact" + struct.pack("<II", 4, len(samples) // frame_size)
    body += b"data" + struct.pack("<I", len(samples)) + samples + (b"\0" if len(samples) % 2 else b"")
    return b"RIFF" + struct.pack("<I", len(body)) + body


def wave_data(path: Path) -> bytes:
    raw = path.read_bytes()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE"
    assert struct.unpack_from("<I", raw, 4)[0] == len(raw) - 8
    offset = 12
    while offset < len(raw):
        name = raw[offset:offset + 4]
        length = struct.unpack_from("<I", raw, offset + 4)[0]
        if name == b"data":
            return raw[offset + 8:offset + 8 + length]
        offset += 8 + length + length % 2
    raise AssertionError("WAV data missing")


with tempfile.TemporaryDirectory(prefix="mkxp-convert-雨血-") as directory:
    root = Path(directory)
    source = root / "原图.png"
    target = root / "输出.png"
    original = png_fixture()
    source.write_bytes(original)
    report = invoke("image", source, target)
    assert (report["width"], report["height"]) == (3, 2)
    assert source.read_bytes() == original
    output = target.read_bytes()
    invoke("image", source, target, success=False)
    assert target.read_bytes() == output
    invoke("image", source, source, success=False)
    assert source.read_bytes() == original
    bmp = root / "legacy-save.bmp"
    # A 24-bit BI_RGB DIB with one padded BGR row, as used by legacy saves.
    bmp_header = struct.pack("<2sIHHI", b"BM", 58, 0, 0, 54)
    dib_header = struct.pack("<IiiHHIIiiII", 40, 1, 1, 1, 24, 0, 4, 0, 0, 0, 0)
    bmp_original = bmp_header + dib_header + bytes([20, 50, 120, 0])
    bmp.write_bytes(bmp_original)
    bmp_report = invoke("image", bmp, root / "legacy-save.png")
    assert "SDL_LoadBMP fallback" in bmp_report["decoder"]
    assert (bmp_report["width"], bmp_report["height"]) == (1, 1)
    assert bmp_report["decoded_crc32"] == f"{zlib.crc32(bytes([120, 50, 20, 255])):08x}"
    assert bmp.read_bytes() == bmp_original
    bad = root / "broken.jpg"
    bad.write_bytes(b"not an image")
    absent = root / "absent.png"
    invoke("image", bad, absent, success=False)
    assert not absent.exists()

    formats = [
        ("u8", bytes([0, 1, 127, 128, 255]), 8, False),
        ("s16", struct.pack("<5h", -32768, -1, 0, 1, 32767), 16, False),
        ("s32", struct.pack("<5i", -2147483648, -1, 0, 1, 2147483647), 32, False),
        # Signed zero and subnormal bits would expose silent float->int quantization.
        ("f32", struct.pack("<6I", 0, 0x80000000, 1, 0x3EAAAAAB, 0x3F800000, 0xBF800000), 32, True),
    ]
    for name, samples, bits, floating in formats:
        source = root / f"原音-{name}.wav"
        target = root / f"输出-{name}.wav"
        original = wave_fixture(samples, bits, floating)
        source.write_bytes(original)
        report = invoke("audio", source, target)
        assert report["rate"] == 8000 and report["channels"] == 1
        assert report["bits_per_sample"] == bits
        assert report["wave_format"] == ("IEEE_FLOAT" if floating else "PCM")
        assert wave_data(target) == samples
        assert report["decoded_crc32"] == f"{zlib.crc32(samples):08x}"
        assert source.read_bytes() == original
        output = target.read_bytes()
        invoke("audio", source, target, success=False)
        assert target.read_bytes() == output
        invoke("audio", source, source, success=False)
        assert source.read_bytes() == original
    alaw = root / "a-law.wav"
    alaw.write_bytes(wave_fixture(bytes([0xD5, 0x55, 0xAA, 0x2A]), 8, False, format_tag=6))
    report = invoke("audio", alaw, root / "a-law-decoded.wav")
    assert "SDL_LoadWAV fallback" in report["decoder"]
    assert report["bits_per_sample"] == 16 and report["frames"] == 4
    assert wave_data(root / "a-law-decoded.wav") == struct.pack("<4h", 8, -8, 32256, -32256)
    stereo = root / "stereo.wav"
    stereo.write_bytes(wave_fixture(struct.pack("<8h", -1, 1, -2, 2, -3, 3, -4, 4), 16, False, 44100, 2))
    report = invoke("audio", stereo, root / "stereo-out.wav")
    assert report["rate"] == 44100 and report["channels"] == 2 and report["frames"] == 4
print("Native asset conversion checks passed")
