# GitLab Tools

## `check_public_header_hygiene.py`

`check_public_header_hygiene.py` checks Brookesia's public include paths for heavyweight or overly broad includes. Its purpose is to prevent accidental transitive dependencies from increasing C/C++ compile time and peak memory usage.

### What it checks

The script performs two textual checks:

1. It checks a small set of high-frequency public headers against fixed forbidden tokens. For example:

   - `task_scheduler.hpp` must not include `boost/asio`, `boost/thread`, or `describe_helpers.hpp`.
   - `log.hpp` must not include `boost/format`, `boost/json`, or `describe_helpers.hpp`.
   - `service/base.hpp` must not include `boost/thread`, `task_scheduler.hpp`, or `describe_helpers.hpp`.

2. It scans production source and header files under `app`, `gui`, `hal`, `runtime`, `service`, `system`, and `utils` and rejects direct use of these umbrella headers:

   ```cpp
   #include "brookesia/lib_utils.hpp"
   #include "brookesia/service_manager.hpp"
   #include "brookesia/service_helper.hpp"
   ```

The umbrella headers themselves are allowed to include those files. `examples`, `host`, `host_test`, `test_apps`, and `tests` are excluded because they are not production include paths.

### Usage

Run it from the `components/esp-brookesia` repository root:

```bash
python3 .gitlab/tools/check_public_header_hygiene.py
```

The script prints `Public header hygiene check passed` and returns `0` when no violation is found. On failure it prints each offending path and token under `Heavy public include violations:` and returns `1`.

### Scope and limitations

The check is intentionally small and deterministic. It uses literal text matching rather than a C/C++ preprocessor or include-graph parser, so it does not validate include completeness, ordering, cycles, API/ABI compatibility, or general IWYU compliance. Review any reported match in context.

The script derives the repository root from its own location and currently has no command-line options. It can be invoked locally or added as a CI/pre-commit check when public-header hygiene should be enforced automatically.
