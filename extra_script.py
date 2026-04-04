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
