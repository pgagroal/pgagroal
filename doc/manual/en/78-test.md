\newpage

## Test Suite

### Overview

This document explains how to run the pgagroal test suite, generate code coverage, and use containerized testing. All testing is now performed using the `check.sh` script with containerized PostgreSQL (recommended and default for all development and CI).

**Running Specific Test Cases or Modules**

To run one particular test case or module, use `<PATH_TO_PGAGROAL>/build/test/pgagroal_test -t <test_case_name> <project_directory> <user> <database>` or `<PATH_TO_PGAGROAL>/build/test/pgagroal_test -m <module_name> <project_directory> <user> <database>`. This requires the test environment to already be set up by `check.sh`.

### Containerized

The `check.sh` script is the main and recommended way to run the pgagroal test suite. It works on any system with Docker or Podman (Linux, macOS, FreeBSD, Windows/WSL2). It automatically builds a PostgreSQL 17 container, sets up the test environment, runs all tests, and generates coverage reports and logs. No local PostgreSQL installation is required.

**Key Features**

- **No local PostgreSQL required**: Uses Docker/Podman containers
- **Consistent environment**: Same PostgreSQL version (17) across all systems
- **Automatic cleanup**: Containers are removed after tests
- **Integrated coverage**: Coverage reports generated automatically
- **Isolated testing**: No interference with local PostgreSQL installations
- **Multiple configurations**: Supports running tests on multiple pgagroal configurations
- **Easy setup**: `./check.sh setup` installs all dependencies and builds the PostgreSQL image
- **Flexible CI support**: Used in CI for Linux, and will be used for all platforms after migration

**Usage**

```sh
./check.sh [sub-command]
```

**Subcommands:**

- `setup`                  Install dependencies and build PostgreSQL image (one-time setup)
- `clean`                  Clean up test suite environment and remove PostgreSQL image
- `run-configs`            Run the testsuite on multiple pgagroal configurations (containerized)
- `ci`                     Run in CI mode (local PostgreSQL, no container)
- `run-configs-ci`         Run multiple configuration tests using local PostgreSQL (like ci + run-configs)
- `ci-nonbuild`            Run in CI mode (local PostgreSQL, skip build step)
- `run-configs-ci-nonbuild` Run multiple configuration tests using local PostgreSQL, skip build step
- (no sub-command)         Default: run all tests in containerized mode

> **For local development, use only the `run-configs` and default (no sub-command) modes. Other modes (`ci`, `run-configs-ci`, etc.) are intended for CI and may interfere with your local PostgreSQL setup if used locally.**

**Artifacts and Logs**

After running containerized tests, you will find:

- Test logs: `/tmp/pgagroal-test/log/`
- PostgreSQL logs: `/tmp/pgagroal-test/pg_log/`
- Coverage reports: `/tmp/pgagroal-test/coverage/`

It is recommended that you **ALWAYS** run tests before raising PR.

**MCTF Framework Overview**

MCTF (Minimal C Test Framework) is pgagroal's custom test framework designed for simplicity and ease of use.

**What MCTF Can Do:**
- **Automatic test registration** - Tests are automatically registered via constructor attributes
- **Module organization** - Module names are automatically extracted from file names (e.g., 'test_art.c' -> module 'art')
- **Flexible assertions** - Assert macros with optional printf-style error messages
- **Test filtering** - Run tests by name pattern ('-t') or by module ('-m')
- **Test skipping** - Skip tests conditionally using 'MCTF_SKIP()' when prerequisites aren't met
- **Cleanup pattern** - Structured cleanup using goto labels for resource management
- **Error tracking** - Automatic error tracking with line numbers and custom error messages
- **Multiple assertion types** - Various assertion macros ('MCTF_ASSERT', 'MCTF_ASSERT_PTR_NONNULL', 'MCTF_ASSERT_INT_EQ', 'MCTF_ASSERT_STR_EQ', etc.)

**What MCTF Cannot Do (Limitations):**
- **No test fixtures** - No automatic setup/teardown per test suite (you must handle setup and cleanup manually in each test)
- **No parameterized tests** - Tests cannot be parameterized (each variation needs a separate test function)
- **No parallel or async execution** - Tests run sequentially and synchronously
- **No built-in timeouts** - No framework-level test timeouts (rely on OS-level signals or manual timeouts)
- **No test organization beyond modules** - No test suites, groups, tags, or metadata beyond module names extracted from filenames

**Add Testcases**

To add an additional testcase, go to [testcases](https://github.com/pgagroal/pgagroal/tree/main/test/testcases) directory inside the `pgagroal` project.

Create a `.c` file that contains the test and use the `MCTF_TEST()` macro to define your test. Tests are automatically registered and module names are extracted from file names.

Example test structure:

```c
#include <mctf.h>
#include <tsclient.h>

MCTF_TEST(test_my_feature)
{
   // Your test code here
   int result = some_function();
   MCTF_ASSERT(result == 0, cleanup, "function should return 0");

cleanup:
   MCTF_FINISH();
}
```

**MCTF_ASSERT Usage:**

The `MCTF_ASSERT` macro supports optional error messages with printf-style formatting:
- **Without message:** `MCTF_ASSERT(condition, cleanup);` - No error message displayed
- **With simple message:** `MCTF_ASSERT(condition, cleanup, "error message");`
- **With formatted message:** `MCTF_ASSERT(condition, cleanup, "got %d, expected 0", value);`
- Format arguments (like `value`) are optional and only needed when the message contains format specifiers (`%d`, `%s`, etc.)
- Multiple format arguments: `MCTF_ASSERT(a == b, cleanup, "expected %d but got %d", expected, actual);`

**Prerequisites**

- **Docker or Podman** installed and running
- **LLVM/clang** and **llvm-cov**/**llvm-profdata** installed (for coverage reports)

> **Note:** The `check.sh` script always builds the project with Clang in Debug mode for coverage and testability.

**Notes**

- The containerized approach automatically handles cleanup on exit.
- Use `./check.sh clean` to manually remove containers and test data.
- PostgreSQL container logs are available with debug5 level for troubleshooting.
- The script automatically detects and uses either Docker or Podman.
- It is recommended to **ALWAYS** run tests before raising a PR.
- Coverage reports are generated using LLVM tooling (clang, llvm-cov, llvm-profdata).
- For local development, use only the `run-configs` and default (no sub-command) modes. Other modes (`ci`, `run-configs-ci`, etc.) are intended for CI and may interfere with your local PostgreSQL setup if used locally.
