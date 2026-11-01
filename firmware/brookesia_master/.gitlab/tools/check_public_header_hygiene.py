# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Reject heavyweight implementation headers from Brookesia's hot public include path."""

from pathlib import Path
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

CORE_HEADER_RULES = {
    Path("utils/brookesia_lib_utils/include/brookesia/lib_utils/task_scheduler.hpp"): (
        "boost/asio",
        "boost/thread",
        "brookesia/lib_utils/describe_helpers.hpp",
    ),
    Path("utils/brookesia_lib_utils/include/brookesia/lib_utils/thread_config.hpp"): (
        "brookesia/lib_utils/describe_helpers.hpp",
    ),
    Path("utils/brookesia_lib_utils/include/brookesia/lib_utils/log.hpp"): (
        "boost/format",
        "boost/json",
        "brookesia/lib_utils/describe_helpers.hpp",
    ),
    Path("service/framework/brookesia_service_manager/include/brookesia/service_manager/service/base.hpp"): (
        "boost/thread",
        "brookesia/lib_utils/task_scheduler.hpp",
        "brookesia/lib_utils/describe_helpers.hpp",
    ),
}

UMBRELLA_INCLUDES = (
    '#include "brookesia/lib_utils.hpp"',
    '#include "brookesia/service_manager.hpp"',
    '#include "brookesia/service_helper.hpp"',
)

PRODUCTION_ROOTS = ("app", "gui", "hal", "runtime", "service", "system", "utils")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".ipp"}
EXCLUDED_PARTS = {"examples", "host", "host_test", "test_apps", "tests"}
UMBRELLA_FILES = {"lib_utils.hpp", "service_manager.hpp", "service_helper.hpp"}


def report_core_header_violations():
    violations = []
    for relative_path, forbidden_tokens in CORE_HEADER_RULES.items():
        path = REPOSITORY_ROOT / relative_path
        content = path.read_text(encoding="utf-8")
        for token in forbidden_tokens:
            if token in content:
                violations.append((relative_path, token))
    return violations


def report_umbrella_violations():
    violations = []
    for root_name in PRODUCTION_ROOTS:
        root = REPOSITORY_ROOT / root_name
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative_path = path.relative_to(REPOSITORY_ROOT)
            if path.name in UMBRELLA_FILES or any(part in EXCLUDED_PARTS for part in relative_path.parts):
                continue
            content = path.read_text(encoding="utf-8", errors="ignore")
            for include in UMBRELLA_INCLUDES:
                if include in content:
                    violations.append((relative_path, include))
    return violations


def main():
    violations = report_core_header_violations() + report_umbrella_violations()
    if not violations:
        print("Public header hygiene check passed")
        return 0

    print("Heavy public include violations:", file=sys.stderr)
    for path, token in violations:
        print(f"  {path}: {token}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
