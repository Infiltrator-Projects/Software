#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/source_contracts.py")
text = path.read_text()
old = 'assert cinnamon_native.count("configure_https_only(curl);") == cinnamon_native.count("curl_easy_perform(curl)")'
new = 'assert cinnamon_native.count("if (!configure_https_only(curl))") == cinnamon_native.count("curl_easy_perform(curl)")'
if text.count(old) != 1:
    raise SystemExit(f"expected one Cinnamon transport assertion, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
