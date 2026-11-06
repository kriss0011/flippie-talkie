"""Build just this external app using the pinned RogueMaster SDK."""
from pathlib import Path

exec(compile(Path("fbt_options.py").read_text(), "fbt_options.py", "exec"))
# Avoid dependencies on RogueMaster's unrelated bundled external apps.
# This selects headers/build configuration only; no firmware is flashed.
FIRMWARE_APP_SET = "unit_tests_min"
EXTRA_EXT_APPS = ["flippie_talkie"]
