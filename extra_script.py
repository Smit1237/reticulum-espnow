import subprocess
import os

Import("env")

# Get version from environment variable (CI) or git describe (local)
version = os.getenv("FIRMWARE_VERSION", "").strip()

if not version:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        version = "dev"

# Sanitize for filename (replace special chars)
version_safe = version.replace("/", "-")

# Rename output binary: firmware_<env>_<version>.bin
env.Replace(PROGNAME="firmware_%s_%s" % (env["PIOENV"], version_safe))

# Embed version as build flag so firmware can print it
env.Append(CPPDEFINES=[
    ("FIRMWARE_VERSION", env.StringifyMacro(version))
])

# C++-only warning suppressions (avoid "not valid for C" noise on .c files)
env.Append(CXXFLAGS=[
    "-Wno-volatile",              # C++20 deprecates volatile++ (libs)
    "-Wno-deprecated-declarations",  # NimBLE 2.x, ArduinoJson 7.x
    "-Wno-reorder",               # microStore field init order
    "-Wno-class-memaccess",       # microReticulum memset on class types
    "-Wno-return-local-addr",     # microReticulum Bytes.h
])

# ---------------------------------------------------------------------------
# Post-build: package bootloader + partitions + boot_app0 + app into a zip
# with a manifest.json. The web flasher reads the manifest and flashes each
# file at the correct offset — identical to what PIO does natively.
# ---------------------------------------------------------------------------

def package_firmware(source, target, env):
    import json, shutil, zipfile

    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("$PROGNAME")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    app_bin = os.path.join(build_dir, progname + ".bin")

    if not os.path.isfile(bootloader) or not os.path.isfile(partitions):
        print("  [package] Bootloader or partitions missing, skipping")
        return

    # Bootloader offset depends on chip
    board_mcu = env.BoardConfig().get("build.mcu", "esp32")
    if board_mcu in ("esp32s3", "esp32c3", "esp32c6", "esp32h2"):
        bl_offset = 0x0000
    else:
        bl_offset = 0x1000  # ESP32, ESP32-S2

    # boot_app0.bin — OTA boot selector
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    boot_app0 = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
    if not os.path.isfile(boot_app0):
        print("  [package] boot_app0.bin not found, skipping")
        return

    # Build manifest
    manifest = {
        "chipFamily": board_mcu.upper().replace("ESP32S", "ESP32-S").replace("ESP32C", "ESP32-C").replace("ESP32H", "ESP32-H"),
        "parts": [
            {"path": "bootloader.bin",  "offset": bl_offset},
            {"path": "partitions.bin",  "offset": 0x8000},
            {"path": "boot_app0.bin",   "offset": 0xE000},
            {"path": "firmware.bin",    "offset": 0x10000},
        ]
    }

    # Create zip alongside the .bin
    zip_path = os.path.join(build_dir, progname + ".zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", json.dumps(manifest, indent=2))
        zf.write(bootloader, "bootloader.bin")
        zf.write(partitions, "partitions.bin")
        zf.write(boot_app0, "boot_app0.bin")
        zf.write(app_bin, "firmware.bin")

    size_kb = os.path.getsize(zip_path) / 1024
    print("  [package] %s (%.1f KB, bl@0x%04X)" % (os.path.basename(zip_path), size_kb, bl_offset))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package_firmware)
