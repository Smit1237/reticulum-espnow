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
# Post-build: merge bootloader + partitions + app into a single flashable
# binary. This ensures the web flasher works on clean/erased chips.
# The merged binary replaces the app-only binary — flash at offset 0x0.
# ---------------------------------------------------------------------------

def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("$PROGNAME")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    app_bin = os.path.join(build_dir, progname + ".bin")
    app_only = os.path.join(build_dir, progname + ".app.bin")

    if not os.path.isfile(bootloader) or not os.path.isfile(partitions):
        print("  [merge] Bootloader or partitions missing, skipping merge")
        return

    # Save the original app-only binary (PIO overwrites it each build,
    # but we need the un-merged version as input to merge-bin)
    import shutil
    shutil.copy2(app_bin, app_only)

    # Bootloader offset depends on chip
    board_mcu = env.BoardConfig().get("build.mcu", "esp32")
    if board_mcu in ("esp32s3", "esp32c3", "esp32c6", "esp32h2"):
        bl_offset = "0x0000"
    else:
        bl_offset = "0x1000"  # ESP32, ESP32-S2

    # boot_app0.bin at 0xe000 — OTA boot selector, required for bootloader
    # to find the app partition
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    boot_app0 = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
    if not os.path.isfile(boot_app0):
        print("  [merge] boot_app0.bin not found at %s, skipping merge" % boot_app0)
        return

    tmp_merged = app_bin + ".merged"

    cmd = [
        env.subst("$PYTHONEXE"), "-m", "esptool",
        "--chip", board_mcu,
        "merge-bin",
        "-o", tmp_merged,
        "--flash-mode", "dio",
        "--flash-freq", "80m",
        "--flash-size", "4MB",
        bl_offset, bootloader,
        "0x8000", partitions,
        "0xe000", boot_app0,
        "0x10000", app_only,
    ]

    print("  [merge] Creating merged binary (%s, bl@%s)..." % (board_mcu, bl_offset))
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0 and os.path.isfile(tmp_merged):
            os.replace(tmp_merged, app_bin)
            size_kb = os.path.getsize(app_bin) / 1024
            print("  [merge] OK: %.1f KB (flash at 0x0)" % size_kb)
        else:
            print("  [merge] FAILED: %s" % result.stderr.strip())
    except Exception as e:
        print("  [merge] FAILED: %s" % str(e))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
