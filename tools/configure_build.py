"""PlatformIO pre-build: one version source and explicit OTA build invariants."""
from pathlib import Path
import json
import os
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
if os.environ.get("SALARY_CLOCK_PUBLIC_BUILD") == "1":
    env.Append(CPPDEFINES=["SALARY_CLOCK_PUBLIC_BUILD"])
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
# OTA and crash capture prerequisites here, retaining other environment settings.
sdkconfig=root/("sdkconfig."+env.subst("$PIOENV"))
if sdkconfig.exists():
    content=sdkconfig.read_text(encoding="utf-8")
    required={
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE":"y",
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE":"y",
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL":"y",
        "CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS":"y",
        "CONFIG_ESP_TASK_WDT_PANIC":"y",
        "CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT":"y",
        "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH":"y",
        "CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF":"y",
        "CONFIG_ESP_COREDUMP_CHECKSUM_CRC32":"y",
        "CONFIG_ESP_COREDUMP_CHECK_BOOT":"y",
        "CONFIG_ESP_COREDUMP_STACK_SIZE":"2048",
        "CONFIG_ESP_COREDUMP_MAX_TASKS_NUM":"64",
        "CONFIG_PARTITION_TABLE_CUSTOM":"y",
        "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME":'"partitions.csv"',
    }
    for key,value in required.items():
        if f"{key}={value}" in content.splitlines():
            continue
        content=re.sub(r"^(?:"+key+r"=.*|# "+key+r" is not set)\n?","",content,flags=re.M)
        content+=f"\n{key}={value}\n"
    content=re.sub(r"^CONFIG_PARTITION_TABLE_SINGLE_APP=y$","# CONFIG_PARTITION_TABLE_SINGLE_APP is not set",content,flags=re.M)
    for key in ("CONFIG_ESP_COREDUMP_ENABLE_TO_NONE", "CONFIG_ESP_COREDUMP_ENABLE_TO_UART",
                "CONFIG_ESP_COREDUMP_DATA_FORMAT_BIN", "CONFIG_ESP_COREDUMP_CHECKSUM_SHA256",
                "CONFIG_ESP_COREDUMP_CAPTURE_DRAM", "CONFIG_ESP_COREDUMP_FLASH_NO_OVERWRITE",
                "CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT", "CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT",
                "CONFIG_ESP_SYSTEM_PANIC_GDBSTUB"):
        content=re.sub(r"^"+key+r"=.*$", "# "+key+" is not set", content, flags=re.M)
    if content != sdkconfig.read_text(encoding="utf-8"):
        sdkconfig.write_text(content,encoding="utf-8")
