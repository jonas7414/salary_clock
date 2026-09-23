"""PlatformIO pre-build: one version source and explicit OTA build invariants."""
from pathlib import Path
import json
import re
import sys

Import("env")
root=Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0,str(root/"tools"))
from release_tools import load_version, read_partitions, validate_layout, parse_size

version=load_version(root)
board=env.BoardConfig()
if board.get("build.mcu") != "esp32s3":
    raise RuntimeError("This OTA layout is for the project's ESP32-S3 board")
flash_size=parse_size(board.get("upload.flash_size"))
configured_size=parse_size(env.GetProjectOption("board_upload.flash_size",board.get("upload.flash_size")))
if flash_size != configured_size:
    raise RuntimeError("Board flash size and project override disagree; review the partition layout")
validate_layout(read_partitions(root/"partitions.csv"),flash_size)
env.Append(CPPDEFINES=[("APP_FIRMWARE_VERSION",env.StringifyMacro(version))])
# PlatformIO's ESP-IDF builder does not watch version.txt or CMake's extra
# configure dependencies. Invalidate only its generated cache when the recorded
# project version differs, so app_desc and the application macro stay in sync.
build=Path(env.subst("$BUILD_DIR"))
description=build/"project_description.json"
cache=build/"CMakeCache.txt"
if cache.exists():
    try:
        cached_version=json.loads(description.read_text(encoding="utf-8")).get("project_version")
    except (OSError,ValueError):
        cached_version=None
    if cached_version != version:
        cache.unlink()
# Existing generated sdkconfig files override sdkconfig.defaults. Enforce only the
# OTA prerequisites here, retaining all other user and environment settings.
sdkconfig=root/("sdkconfig."+env.subst("$PIOENV"))
if sdkconfig.exists():
    content=sdkconfig.read_text(encoding="utf-8")
    required={
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE":"y",
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE":"y",
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL":"y",
        "CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS":"y",
        "CONFIG_ESP_TASK_WDT_PANIC":"y",
        "CONFIG_PARTITION_TABLE_CUSTOM":"y",
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME":'"partitions.csv"',
    }
    for key,value in required.items():
        if f"{key}={value}" in content.splitlines():
            continue
        content=re.sub(r"^(?:"+key+r"=.*|# "+key+r" is not set)\n?","",content,flags=re.M)
        content+=f"\n{key}={value}\n"
    content=re.sub(r"^CONFIG_PARTITION_TABLE_SINGLE_APP=y$","# CONFIG_PARTITION_TABLE_SINGLE_APP is not set",content,flags=re.M)
    if content != sdkconfig.read_text(encoding="utf-8"):
        sdkconfig.write_text(content,encoding="utf-8")
