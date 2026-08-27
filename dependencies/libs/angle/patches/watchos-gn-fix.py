#!/usr/bin/env python3
"""Insert watchOS GN branches before the tvOS anchors in ANGLE/Chromium."""
from __future__ import annotations

import pathlib
import sys


def insert_before_tvos(path: str, block: str) -> None:
    p = pathlib.Path(path)
    text = p.read_text()
    needle = '} else if (target_platform == "tvos") {'
    if 'target_platform == "watchos"' in text:
        return
    if needle not in text:
        sys.exit(f"missing tvos anchor in {path}")
    p.write_text(text.replace(needle, block + needle, 1))


insert_before_tvos(
    "build/config/ios/ios_sdk.gni",
    """  } else if (target_platform == "watchos") {
    if (target_environment == "simulator") {
      ios_sdk_name = "watchsimulator"
      ios_sdk_platform = "WatchSimulator"
    } else if (target_environment == "device") {
      ios_sdk_name = "watchos"
      ios_sdk_platform = "WatchOS"
    } else {
      assert(false, "unsupported target_environment=$target_environment")
    }
""",
)

insert_before_tvos(
    "build/config/clang/BUILD.gn",
    """    } else if (target_platform == "watchos") {
      if (target_environment == "simulator") {
        libname = "watchossim"
      } else if (target_environment == "device") {
        libname = "watchos"
      } else {
        assert(false, "unsupported target_environment=$target_environment")
      }
""",
)

insert_before_tvos(
    "build/config/rust.gni",
    """    } else if (target_platform == "watchos") {
      if (target_environment == "simulator") {
        rust_abi_target = "aarch64-apple-watchos-sim"
        cargo_target_abi = "sim"
      } else if (target_environment == "device") {
        rust_abi_target = "aarch64-apple-watchos"
        cargo_target_abi = ""
      } else {
        assert(false, "unsupported target_environment=$target_environment")
      }
""",
)

insert_before_tvos(
    "build/config/ios/BUILD.gn",
    """  } else if (target_platform == "watchos") {
    triplet_os = "apple-watchos"
""",
)

rules_gni = pathlib.Path("build/config/ios/rules.gni")
rules_text = rules_gni.read_text()
rules_needle = '    } else if (target_platform == "tvos") {'
rules_block = """    } else if (target_platform == "watchos") {
      _build_info_plist = "//build/config/ios/BuildInfo.plist"
"""
if rules_block.strip() not in rules_text:
    if rules_needle not in rules_text:
        sys.exit("missing tvos anchor in build/config/ios/rules.gni")
    rules_gni.write_text(rules_text.replace(rules_needle, rules_block + rules_needle, 1))
