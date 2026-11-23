#!/usr/bin/env python3
"""Fail CI if example configuration or source inventory is inconsistent."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN_DIR = ROOT / "plugins"
SDK_CATEGORIES = ROOT.parent / "sdk" / "categories.json"
TAXONOMY = json.loads(SDK_CATEGORIES.read_text(encoding="utf-8"))
allowed = set(TAXONOMY["categories"])
configs = sorted(PLUGIN_DIR.glob("*/plugin.config.json"))
if len(configs) != 21:
    raise SystemExit(f"Expected 20 original DSP examples plus 0xFX Amp Collection, found {len(configs)}")
seen_ids = set()
for config_path in configs:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    name = config_path.parent.name
    category = config.get("category")
    if category not in allowed:
        raise SystemExit(f"{name}: invalid category {category!r}; allowed: {', '.join(sorted(allowed))}")
    plugin_id = config.get("id")
    if not plugin_id or plugin_id in seen_ids:
        raise SystemExit(f"{name}: missing or duplicate plugin ID {plugin_id!r}")
    seen_ids.add(plugin_id)
    sources = config.get("cpp", {}).get("sources", [])
    if not sources:
        raise SystemExit(f"{name}: no C++ sources configured")
    for source in sources:
        file = (config_path.parent / source).resolve()
        if not file.is_relative_to(PLUGIN_DIR.resolve()) or not file.is_file():
            raise SystemExit(f"{name}: missing or unsafe source {source!r}")
    if icon := config.get("icon"):
        file = (config_path.parent / icon).resolve()
        if not file.is_relative_to(PLUGIN_DIR.resolve()) or not file.is_file():
            raise SystemExit(f"{name}: missing or unsafe icon {icon!r}")
    print(f"PASS {name}: {category} ({len(sources)} sources)")
    license = config.get("license")
    if license:
        notices = license.get("notices", [])
        if not license.get("expression") or not notices:
            raise SystemExit(f"{name}: license expression and notices are required")
        for notice in notices:
            path = notice.get("path", "")
            if not path.startswith("licenses/") or ".." in path:
                raise SystemExit(f"{name}: unsafe notice path {path!r}")
            file = (config_path.parent / path).resolve()
            if not file.is_relative_to(config_path.parent.resolve()) or not file.is_file():
                raise SystemExit(f"{name}: missing license notice {path!r}")
print("PASS: 21 examples with valid categories, sources and bundled notices")
