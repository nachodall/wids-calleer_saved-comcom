"""Exporta el framebuffer C real a PNG/APNG usando solo la biblioteca estandar."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

PROJECT = Path(__file__).resolve().parents[1]
SIGNATURE = b"\x89PNG\r\n\x1a\n"


def chunk(kind, data):
    return (struct.pack("!I", len(data)) + kind + data
            + struct.pack("!I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def header(width, height):
    return SIGNATURE + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 0, 0, 0, 0))


def compressed(pixels, width, height):
    return zlib.compress(b"".join(
        b"\0" + pixels[y * width:(y + 1) * width] for y in range(height)))


def read_frame(path, scale=4):
    pgm = path.read_bytes().split(b"\n", 3)
    assert pgm[:3] == [b"P5", b"128 64", b"255"]
    width, height = 128 * scale, 64 * scale
    return bytes(pgm[3][(y // scale) * 128 + x // scale]
                 for y in range(height) for x in range(width))


def export(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    names = ["menu", "history", "detail", "alert-0000", "alert-0240", "alert-1000"]
    width, height = 3 * 528, 2 * 272
    pixels = bytearray([24]) * (width * height)
    for i, name in enumerate(names):
        frame = read_frame(source / (name + ".pgm"))
        for y in range(256):
            offset = ((i // 3) * 272 + 8 + y) * width + (i % 3) * 528 + 8
            pixels[offset:offset + 512] = frame[y * 512:(y + 1) * 512]
    png = header(width, height) + chunk(b"IDAT", compressed(pixels, width, height))
    (destination / "menu-preview.png").write_bytes(png + chunk(b"IEND", b""))

    frames = sorted(source.glob("alert-*.pgm")) + [source / "detail.pgm"]
    png = header(512, 256) + chunk(b"acTL", struct.pack("!2I", len(frames), 0))
    sequence = 0
    for i, path in enumerate(frames):
        delay = 1200 if i == len(frames) - 1 else 40
        png += chunk(b"fcTL", struct.pack("!5I2H2B",
                     sequence, 512, 256, 0, 0, delay, 1000, 0, 0))
        sequence += 1
        data = compressed(read_frame(path), 512, 256)
        if i == 0:
            png += chunk(b"IDAT", data)
        else:
            png += chunk(b"fdAT", struct.pack("!I", sequence) + data)
            sequence += 1
    (destination / "alerta-animada.png").write_bytes(png + chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Uso: python3 tests/preview_placa_b.py DIRECTORIO_SALIDA")
    destination = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="placa-b-preview-") as directory:
        temp = Path(directory)
        binary = temp / "test_ui"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(PROJECT / "placa_b_consola/main"),
            "-I", str(PROJECT / "shared"),
            str(PROJECT / "tests/test_placa_b_ui.c"),
            str(PROJECT / "placa_b_consola/main/console_ui.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), str(temp)], check=True)
        export(temp, destination)
    print(f"Capturas: {destination / 'menu-preview.png'}")
    print(f"Animacion APNG: {destination / 'alerta-animada.png'}")


if __name__ == "__main__":
    main()
