"""Sets the rev7 shipped defaults in a hydrothunder.toml (keys replaced in place or prepended).

usage: pkg_toml.py <hydrothunder.toml>
"""
import re
import sys

DEFAULTS = {
    # Measured on Asgard: FSR 1 matches the best FSR 3.1 setting in sharpness and cost.
    "present_effect": '"fsr"',
    "ht_sharpness": "7",  # launcher slider; 7 = RCAS 0.25, no extra CAS
    "present_fsr_sharpness_reduction": "0.25",
    "present_cas_additional_sharpness": "0.00",
    "present_fsr3_sharpness": "0.0",
    # Launcher "Qualidade": 720p internal up to 1080p, Nativa only when chosen.
    "ht_render_quality": "1",
    "resolution_scale": "1",
    # FXAA off: it doubles the outline of small HUD digits after upscaling.
    "swap_post_effect": '""',
    "ht_perf_log": "false",
}

path = sys.argv[1]
text = open(path, encoding="utf-8").read()
for key, value in DEFAULTS.items():
    line = f"{key} = {value}"
    pattern = re.compile(rf"^{re.escape(key)}\s*=.*$", re.M)
    text = pattern.sub(line, text) if pattern.search(text) else line + "\n" + text
open(path, "w", encoding="utf-8", newline="\n").write(text)
print("ok", path)
