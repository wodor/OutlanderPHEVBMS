"""Add the private OTA password to PlatformIO's espota uploader flags."""

from pathlib import Path
import re

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


def read_quoted_define(config_text: str, name: str) -> str:
    match = re.search(
        rf'^\s*#define\s+{re.escape(name)}\s+"((?:\\.|[^"\\])*)"',
        config_text,
        re.MULTILINE,
    )
    if not match:
        return ""
    return bytes(match.group(1), "utf-8").decode("unicode_escape")


config_path = Path(env["PROJECT_DIR"]) / ".config.h"  # type: ignore[name-defined]
if config_path.exists():
    config_text = config_path.read_text(encoding="utf-8")
    ota_password = read_quoted_define(config_text, "OTA_PASSWORD")
    if not ota_password:
        ota_password = read_quoted_define(config_text, "WIFI_PASSWORD")
    if ota_password:
        env.Append(UPLOADERFLAGS=["--auth", ota_password])  # type: ignore[name-defined]
