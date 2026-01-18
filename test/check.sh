#!/bin/bash
#
# Copyright (C) 2025 The pgagroal community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
set -eo pipefail

# Variables
ENV_PGVERSION=17
IMAGE_NAME="pgagroal-test-postgresql${ENV_PGVERSION}-rocky9"
CONTAINER_NAME="pgagroal-test-postgresql${ENV_PGVERSION}"

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
PROJECT_DIRECTORY=$(realpath "$SCRIPT_DIR/..")
EXECUTABLE_DIRECTORY=$PROJECT_DIRECTORY/build/src
TEST_DIRECTORY=$PROJECT_DIRECTORY/build/test
TEST_PG_DIRECTORY=$PROJECT_DIRECTORY/test/postgresql/src/postgresql${ENV_PGVERSION}

PGAGROAL_ROOT_DIR="/tmp/pgagroal-test"
BASE_DIR="$PROJECT_DIRECTORY/pgagroal-testsuite"
COVERAGE_DIR="$PGAGROAL_ROOT_DIR/coverage"
LOG_DIR="$PGAGROAL_ROOT_DIR/log"
PG_LOG_DIR="$PGAGROAL_ROOT_DIR/pg_log"

# BASE DIR holds all the run time data
CONFIGURATION_DIRECTORY=$BASE_DIR/conf
RESOURCE_DIRECTORY=$BASE_DIR/resource

PG_DATABASE=mydb
PG_USER_NAME=myuser
PG_USER_PASSWORD=yourpassword
PG_REPL_USER_NAME=repl
PG_REPL_PASSWORD=replpass
PG_UTF8_USER_NAME=utf8user
PG_UTF8_USER_PASSWORD=HelloПриветمرحبا你好नमस्तेสวัสดีこんにちは안녕하세요𠜎𡃁𩷶
PG_UTF8_DATABASE=utf8db
PGAGROAL_PORT=2345
USER=$(whoami)
MODE="dev"
PORT=6432

# Detect container engine: Docker or Podman
if command -v docker &> /dev/null; then
  CONTAINER_ENGINE="sudo docker"
elif command -v podman &> /dev/null; then
  CONTAINER_ENGINE="podman"
else
  echo "Neither Docker nor Podman is installed. Please install one to proceed."
  exit 1
fi 

 # Port conflict resolution functions
stop_pgagroal() {
   echo "Stopping pgagroal instance..."
   set +e
   # Use the correct PID file path with port number
   PID_FILE="/tmp/pgagroal.${PGAGROAL_PORT}.pid"
   # First try graceful shutdown
   if [[ -f "$CONFIGURATION_DIRECTORY/pgagroal.conf" ]]; then
     $EXECUTABLE_DIRECTORY/pgagroal-cli -c $CONFIGURATION_DIRECTORY/pgagroal.conf shutdown 2>/dev/null
     sleep 3
   fi
   # Check if pgagroal is still running and force kill if necessary
   if pgrep pgagroal > /dev/null; then
     echo "pgagroal still running, force stopping..."
     kill -9 $(pgrep pgagroal) 2>/dev/null
     sleep 2
   fi
   # Clean up PID files
   sudo rm -f /tmp/pgagroal.*.pid 2>/dev/null
   echo "pgagroal stop completed"
   set -e
}
is_port_in_use() {
    local port=$1
    if command -v ss &> /dev/null; then
        ss -tuln | grep ":$port " >/dev/null 2>&1
    elif command -v lsof &> /dev/null; then
        lsof -i:$port >/dev/null 2>&1
    elif command -v netstat &> /dev/null; then
        netstat -tuln | grep ":$port " >/dev/null 2>&1
    else
        # Fallback: try to bind to the port
        (echo >/dev/tcp/localhost/$port) >/dev/null 2>&1
    fi
    return $?
}

next_available_port() {
    local port=$1
    while true; do
        is_port_in_use $port
        if [ $? -ne 0 ]; then
            echo "$port"
            return 0
        else
            echo "Port $port is in use, trying next port..." >&2
            port=$((port + 1))
        fi
    done
}

# Use environment variable if set, otherwise find next available port
if [ -n "$PGAGROAL_TEST_PORT" ]; then
    PORT=$PGAGROAL_TEST_PORT
    echo "Using specified port: $PORT"
else
    PORT=$(next_available_port $PORT)
    echo "Using next available port: $PORT"
fi
echo "Container port is set to: $PORT"

cleanup() {
   echo "Clean up"
   set +e
   echo "Shutdown pgagroal"
   
   # Use the correct PID file path with port number
   PID_FILE="/tmp/pgagroal.${PGAGROAL_PORT}.pid"
   
   # First try graceful shutdown
   if [[ -f "$CONFIGURATION_DIRECTORY/pgagroal.conf" ]]; then
     $EXECUTABLE_DIRECTORY/pgagroal-cli -c $CONFIGURATION_DIRECTORY/pgagroal.conf shutdown 2>/dev/null
     sleep 3
   fi
   
   # Check if pgagroal is still running and force kill if necessary
   if pgrep pgagroal > /dev/null; then
     echo "pgagroal still running, force stopping..."
     kill -9 $(pgrep pgagroal) 2>/dev/null
     sleep 2
   fi
   
   # Clean up PID files
   sudo rm -f /tmp/pgagroal.*.pid 2>/dev/null
   
   echo "pgagroal cleanup completed"

   echo "Clean Test Resources"
   if [[ -d $PGAGROAL_ROOT_DIR ]]; then
      if ! sudo chown -R "$USER:$USER" "$PGAGROAL_ROOT_DIR"; then
        echo " Could not change ownership. You might need to clean manually."
      fi

      if [[ -d $BASE_DIR ]]; then
        sudo rm -Rf "$BASE_DIR"
      fi
      
      # Generate LLVM coverage reports
      if [[ $COVERAGE == "YES" ]]; then
         if ls "$COVERAGE_DIR"/*.profraw >/dev/null 2>&1; then
          echo "Generating coverage report, expect error when the binary is not covered at all"
          llvm-profdata merge -sparse $COVERAGE_DIR/*.profraw -o $COVERAGE_DIR/coverage.profdata

          echo "Generating $COVERAGE_DIR/coverage-report-libpgagroal.txt"
          llvm-cov report $EXECUTABLE_DIRECTORY/libpgagroal.so \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-report-libpgagroal.txt
          echo "Generating $COVERAGE_DIR/coverage-report-pgagroal.txt"
          llvm-cov report $EXECUTABLE_DIRECTORY/pgagroal \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-report-pgagroal.txt
         echo "Generating $COVERAGE_DIR/coverage-report-pgagroal-cli.txt"
         llvm-cov report $EXECUTABLE_DIRECTORY/pgagroal-cli \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-report-pgagroal-cli.txt
         echo "Generating $COVERAGE_DIR/coverage-report-pgagroal-admin.txt"
         llvm-cov report $EXECUTABLE_DIRECTORY/pgagroal-admin \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-report-pgagroal-admin.txt

          echo "Generating $COVERAGE_DIR/coverage-libpgagroal.txt"
          llvm-cov show $EXECUTABLE_DIRECTORY/libpgagroal.so \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-libpgagroal.txt
          echo "Generating $COVERAGE_DIR/coverage-pgagroal.txt"
          llvm-cov show $EXECUTABLE_DIRECTORY/pgagroal \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-pgagroal.txt
         echo "Generating $COVERAGE_DIR/coverage-pgagroal-cli.txt"
         llvm-cov show $EXECUTABLE_DIRECTORY/pgagroal-cli \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-pgagroal-cli.txt
         echo "Generating $COVERAGE_DIR/coverage-pgagroal-admin.txt"
         llvm-cov show $EXECUTABLE_DIRECTORY/pgagroal-admin \
            --instr-profile=$COVERAGE_DIR/coverage.profdata \
            --format=text > $COVERAGE_DIR/coverage-pgagroal-admin.txt
         
          echo "Coverage --> $COVERAGE_DIR"
       fi
    fi
      
      echo "Logs --> $LOG_DIR, $PG_LOG_DIR"
      sudo chmod -R 700 "$PGAGROAL_ROOT_DIR"
   else
     echo "$PGAGROAL_ROOT_DIR not present ... ok"
   fi

   if [[ $MODE != "ci" ]]; then
  echo "Removing postgres $ENV_PGVERSION container"
    remove_postgresql_container
   fi

   echo "Unsetting environment variables"
   unset_pgagroal_test_variables

   set -e
}

build_postgresql_image() {
  echo "Building the PostgreSQL $ENV_PGVERSION image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG_DIRECTORY
  set +e
  make clean
  set -e
  make build
  cd $CUR_DIR
}

cleanup_postgresql_image() {
  set +e
  echo "Cleanup of the PostgreSQL $ENV_PGVERSION image $IMAGE_NAME"
  CUR_DIR=$(pwd)
  cd $TEST_PG_DIRECTORY
  make clean
  cd $CUR_DIR
  set -e
}

start_postgresql_container() {
  $CONTAINER_ENGINE run -p $PORT:5432 -v "$PG_LOG_DIR:/pglog:z" \
  --name $CONTAINER_NAME -d \
  -e PG_DATABASE=$PG_DATABASE \
  -e PG_USER_NAME=$PG_USER_NAME \
  -e PG_USER_PASSWORD=$PG_USER_PASSWORD \
  -e PG_REPL_USER_NAME=$PG_REPL_USER_NAME \
  -e PG_REPL_PASSWORD=$PG_REPL_PASSWORD \
  -e PG_UTF8_USER_NAME=$PG_UTF8_USER_NAME \
  -e PG_UTF8_USER_PASSWORD=$PG_UTF8_USER_PASSWORD \
  -e PG_UTF8_DATABASE=$PG_UTF8_DATABASE \
  -e PG_LOG_LEVEL=debug5 \
  $IMAGE_NAME

  echo "Checking PostgreSQL $ENV_PGVERSION container readiness"
  sleep 3
  for attempt in {1..6}; do
    if $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-$ENV_PGVERSION/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
      break
    fi
    echo "PostgreSQL $ENV_PGVERSION not ready yet (attempt $attempt/6). Waiting 30 seconds before retry..."
    sleep 30
  done

  # Final readiness check; if still not ready after retries, fail
  if $CONTAINER_ENGINE exec $CONTAINER_NAME /usr/pgsql-$ENV_PGVERSION/bin/pg_isready -h localhost -p 5432 >/dev/null 2>&1; then
    echo "PostgreSQL $ENV_PGVERSION is ready!"
  else
    echo "Printing container logs..."
    $CONTAINER_ENGINE logs $CONTAINER_NAME
    echo ""
    echo "PostgreSQL $ENV_PGVERSION is not ready after 3 minutes, exiting"
    cleanup_postgresql_image
    exit 1
  fi
}

remove_postgresql_container() {
  $CONTAINER_ENGINE stop $CONTAINER_NAME 2>/dev/null || true
  $CONTAINER_ENGINE rm -f $CONTAINER_NAME 2>/dev/null || true
}

start_postgresql() {
  echo "Setting up PostgreSQL $ENV_PGVERSION directory"
  set +e
  sudo rm -Rf /conf /pgconf /pgdata /pgwal
  sudo cp -R $TEST_PG_DIRECTORY/root /
  sudo ls /root
  sudo mkdir -p /conf /pgconf /pgdata /pgwal /pglog

  sudo cp -R $TEST_PG_DIRECTORY/conf/* /conf/
  sudo ls /conf
  sudo chown -R postgres:postgres /conf /pgconf /pgdata /pgwal /pglog
  sudo chmod -R 777 /conf /pgconf /pgdata /pgwal /pglog /root
  sudo chmod +x /root/usr/bin/run-postgresql-local
  sudo mkdir -p /root/usr/local/bin

  echo "Setting up env variables"
  export PG_DATABASE=${PG_DATABASE}
  export PG_USER_NAME=${PG_USER_NAME}
  export PG_USER_PASSWORD=${PG_USER_PASSWORD}
  export PG_REPL_USER_NAME=${PG_REPL_USER_NAME}
  export PG_REPL_PASSWORD=${PG_REPL_PASSWORD}
  export PG_UTF8_USER_NAME=${PG_UTF8_USER_NAME}
  export PG_UTF8_USER_PASSWORD=${PG_UTF8_USER_PASSWORD}
  export PG_UTF8_DATABASE=${PG_UTF8_DATABASE}

  sudo -E -u postgres /root/usr/bin/run-postgresql-local
  set -e
}

pgagroal_initialize_configuration() {
   touch $CONFIGURATION_DIRECTORY/pgagroal.conf $CONFIGURATION_DIRECTORY/pgagroal_hba.conf $CONFIGURATION_DIRECTORY/pgagroal_users.conf $CONFIGURATION_DIRECTORY/pgagroal_databases.conf $CONFIGURATION_DIRECTORY/pgagroal_frontend_users.conf
   echo "Creating pgagroal configuration files inside $CONFIGURATION_DIRECTORY ... ok"
   
   # Create pgagroal.conf
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgagroal.conf
[pgagroal]
host = localhost
port = $PGAGROAL_PORT

log_type = file
log_level = debug5
log_path = $LOG_DIR/pgagroal.log

max_connections = 8
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/
pipeline = 'performance'

[primary]
host = localhost
port = $PORT
EOF

   # Create HBA configuration
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgagroal_hba.conf
host    all all all trust
EOF

   # Create database aliases configuration
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgagroal_databases.conf
#
# DATABASE=ALIAS1,ALIAS2 USER MAX_SIZE INITIAL_SIZE MIN_SIZE
#
$PG_DATABASE=pgalias1,pgalias2 $PG_USER_NAME 6 6 1
$PG_UTF8_DATABASE $PG_UTF8_USER_NAME 2 2 1

EOF

   echo "Add test configuration to pgagroal.conf ... ok"
   
   # Create master key if it doesn't exist
   if [[ ! -e $HOME/.pgagroal/master.key ]]; then
     $EXECUTABLE_DIRECTORY/pgagroal-admin master-key -P $PG_USER_PASSWORD
   fi
   
   # Add PostgreSQL test user
   $EXECUTABLE_DIRECTORY/pgagroal-admin -f $CONFIGURATION_DIRECTORY/pgagroal_users.conf -U $PG_USER_NAME -P $PG_USER_PASSWORD user add
   echo "Add user $PG_USER_NAME to pgagroal_users.conf file ... ok"
   
   # Add system user for test executable compatibility
  $EXECUTABLE_DIRECTORY/pgagroal-admin -f $CONFIGURATION_DIRECTORY/pgagroal_users.conf -U $USER -P $PG_USER_PASSWORD user add
  echo "Add user $USER to pgagroal_users.conf file ... ok"

  # Add UTF-8 test user
  $EXECUTABLE_DIRECTORY/pgagroal-admin -f $CONFIGURATION_DIRECTORY/pgagroal_users.conf -U "$PG_UTF8_USER_NAME" -P "$PG_UTF8_USER_PASSWORD" user add
  echo "Add UTF-8 user $PG_UTF8_USER_NAME to pgagroal_users.conf file ... ok"
   
   # Create empty frontend users configuration to avoid default path conflicts
   cat <<EOF >$CONFIGURATION_DIRECTORY/pgagroal_frontend_users.conf
# pgagroal frontend users configuration for testing
# This file prevents pgagroal from reading default system frontend users
EOF
   echo "Created pgagroal_frontend_users.conf file ... ok"
   echo ""
}

export_pgagroal_test_variables() {
  echo "export PGAGROAL_TEST_BASE_DIR=$BASE_DIR"
  export PGAGROAL_TEST_BASE_DIR=$BASE_DIR

  echo "export PGAGROAL_TEST_CONF=$CONFIGURATION_DIRECTORY/pgagroal.conf"
  export PGAGROAL_TEST_CONF=$CONFIGURATION_DIRECTORY/pgagroal.conf
}

unset_pgagroal_test_variables() {
  unset PGAGROAL_TEST_BASE_DIR
  unset PGAGROAL_TEST_CONF
  unset LLVM_PROFILE_FILE
  unset CK_RUN_CASE
  unset CK_RUN_SUITE
  unset CC
}

execute_testcases() {
   local config_name="${1:-default}"
   echo "Execute Testcases for configuration: $config_name"
   set +e
   echo "Starting pgagroal server in daemon mode"
   $EXECUTABLE_DIRECTORY/pgagroal -c $CONFIGURATION_DIRECTORY/pgagroal.conf -a $CONFIGURATION_DIRECTORY/pgagroal_hba.conf -u $CONFIGURATION_DIRECTORY/pgagroal_users.conf -l $CONFIGURATION_DIRECTORY/pgagroal_databases.conf -F $CONFIGURATION_DIRECTORY/pgagroal_frontend_users.conf -d
   echo "Wait for pgagroal to be ready"
   sleep 10
   $EXECUTABLE_DIRECTORY/pgagroal-cli -c $CONFIGURATION_DIRECTORY/pgagroal.conf status details
   if [[ $? -eq 0 ]]; then
      echo "pgagroal server started ... ok"
   else
      echo "pgagroal server not started ... not ok"
      exit 1
   fi

   echo "Start running tests for $config_name"
  $TEST_DIRECTORY/pgagroal_test $PROJECT_DIRECTORY $PG_USER_NAME $PG_DATABASE
   test_result=$?
   
   
   if [[ $test_result -ne 0 ]]; then
      echo "Tests failed for configuration: $config_name"
      exit 1
   fi
   set -e
}

run_multiple_config_tests() {
   echo "Running tests on multiple pgagroal configurations"
   
   TEST_CONFIG_DIRECTORY="$PROJECT_DIRECTORY/test/conf"
   
   if [ -d "$TEST_CONFIG_DIRECTORY" ]; then
      for entry in "$TEST_CONFIG_DIRECTORY"/*; do
         entry=$(realpath "$entry")
         config_name=$(basename "$entry")
         
         if [[ -d "$entry" && -f "$entry/pgagroal.conf" && -f "$entry/pgagroal_hba.conf" ]]; then
            echo ""
            echo "=========================================="
            echo "Testing configuration: $config_name"
            echo "=========================================="
            
            # Backup current configuration
            cp "$CONFIGURATION_DIRECTORY/pgagroal.conf" "$CONFIGURATION_DIRECTORY/pgagroal.conf.backup"
            cp "$CONFIGURATION_DIRECTORY/pgagroal_hba.conf" "$CONFIGURATION_DIRECTORY/pgagroal_hba.conf.backup"
            
            # Copy test configuration
            cp "$entry/pgagroal.conf" "$CONFIGURATION_DIRECTORY/pgagroal.conf"
            cp "$entry/pgagroal_hba.conf" "$CONFIGURATION_DIRECTORY/pgagroal_hba.conf"
            
            # Update log path in the configuration to use our log directory
            sed -i "s|log_path = test.log|log_path = $LOG_DIR/pgagroal-$config_name.log|g" "$CONFIGURATION_DIRECTORY/pgagroal.conf"
            
            # Update port to match our PostgreSQL container
            sed -i "s|port = 5432|port = $PORT|g" "$CONFIGURATION_DIRECTORY/pgagroal.conf"
            
            # Stop any running pgagroal instance before starting new config
            stop_pgagroal
            # Run tests for this configuration
            execute_testcases "$config_name"
            # Stop after test in case test leaves it running
            stop_pgagroal
            
            # Restore original configuration
            cp "$CONFIGURATION_DIRECTORY/pgagroal.conf.backup" "$CONFIGURATION_DIRECTORY/pgagroal.conf"
            cp "$CONFIGURATION_DIRECTORY/pgagroal_hba.conf.backup" "$CONFIGURATION_DIRECTORY/pgagroal_hba.conf"
            
            echo "Configuration $config_name: PASSED"
         else
            echo "Warning: Configuration directory '$entry' is missing required files"
            echo "  Required: pgagroal.conf and pgagroal_hba.conf"
         fi
      done
      
      # Clean up backup files
      rm -f "$CONFIGURATION_DIRECTORY/pgagroal.conf.backup"
      rm -f "$CONFIGURATION_DIRECTORY/pgagroal_hba.conf.backup"
      
      echo ""
      echo "=========================================="
      echo "All configuration tests completed!"
      echo "=========================================="
   else
      echo "Configuration directory $TEST_CONFIG_DIRECTORY not present"
      exit 1
   fi
}

usage() {
  echo "Usage: $0 [sub-command]"
  echo "Subcommands:"
  echo "  setup                  Install dependencies and build PostgreSQL image (one-time setup)"
  echo "  clean                  Clean up test suite environment and remove PostgreSQL image"
  echo "  run-configs            Run the testsuite on multiple pgagroal configurations (containerized)"
  echo "  ci                     Run in CI mode (local PostgreSQL, no container)"
  echo "  run-configs-ci         Run multiple configuration tests using local PostgreSQL (like ci + run-configs)"
  echo "  ci-nonbuild            Run in CI mode (local PostgreSQL, skip build step)"
  echo "  run-configs-ci-nonbuild Run multiple configuration tests using local PostgreSQL, skip build step"
  echo "  (no sub-command)       Default: run all tests in containerized mode"
  echo "  gcc                    Compile with gcc and no-coverage"
  exit 1
}

run_tests() {
  echo "Building PostgreSQL $ENV_PGVERSION image if necessary"
  if $CONTAINER_ENGINE image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo "Image $IMAGE_NAME exists, skip building"
  else
    if [[ $MODE != "ci" ]]; then
      build_postgresql_image
    fi
  fi

  if [[ $MODE == "gcc" ]]; then
    COVERAGE=NO
  else
    COVERAGE=YES
  fi

  echo "Preparing the pgagroal directory"
  sudo rm -Rf "$PGAGROAL_ROOT_DIR"
  mkdir -p "$PGAGROAL_ROOT_DIR"
  # Set up LLVM coverage
  if [[ $COVERAGE == "YES" ]]; then 
    export LLVM_PROFILE_FILE="$COVERAGE_DIR/coverage-%m-%p.profraw"
    mkdir -p "$LOG_DIR" "$PG_LOG_DIR" "$COVERAGE_DIR" "$BASE_DIR" "$RESOURCE_DIRECTORY"
  else 
    mkdir -p "$LOG_DIR" "$PG_LOG_DIR" "$BASE_DIR" "$RESOURCE_DIRECTORY"
  fi

  cp -R "$PROJECT_DIRECTORY/test/resource" $BASE_DIR
  mkdir -p "$CONFIGURATION_DIRECTORY"
  mkdir -p "$PROJECT_DIRECTORY/log"
  # Check if pgbench is available
  if ! command -v pgbench &> /dev/null; then
    echo "Warning: pgbench not found in PATH. Tests may fail."
    find /usr -name "pgbench" 2>/dev/null || echo "pgbench not found in /usr"
  else
    echo "pgbench found: $(which pgbench)"
  fi
  sudo chmod -R 777 "$PGAGROAL_ROOT_DIR"
  if [[ "$1" != "ci-nonbuild" && "$1" != "run-configs-ci-nonbuild" ]]; then
   echo "Building pgagroal"
   mkdir -p "$PROJECT_DIRECTORY/build"
   cd "$PROJECT_DIRECTORY/build"
   # Configure build with LLVM coverage or GCC without coverage
   if [[ $MODE != gcc ]]; then
     export CC=$(which clang)
     echo "Using Clang compiler with LLVM coverage: $CC"
   else
     export CC=$(which gcc)
     echo "Using GCC compiler without coverage: $CC"
   cmake -DCMAKE_C_COMPILER=$CC \
        -DCMAKE_BUILD_TYPE=Debug \
        ..
   fi
   make -j$(nproc)
   cd ..
  fi
  if [[ $MODE == "ci" ]]; then
  echo "Start PostgreSQL $ENV_PGVERSION locally"
    start_postgresql
  else
  echo "Start PostgreSQL $ENV_PGVERSION container"
    start_postgresql_container
  fi

  # Initialize pgbench database for main user
  echo "Initializing pgbench database for main user..."
  PGPASSWORD="$PG_USER_PASSWORD" pgbench -i -s 1 -h localhost -p "$PORT" -U "$PG_USER_NAME" -d "$PG_DATABASE"
  if [[ $? -ne 0 ]]; then
    echo "pgbench initialization failed for main user!"
    cleanup_postgresql_image
    exit 1
  fi
  echo "pgbench initialization for main user completed."

  # Initialize pgbench database for UTF-8 user
  echo "Initializing pgbench database for UTF-8 user..."
  PGPASSWORD="$PG_UTF8_USER_PASSWORD" pgbench -i -s 1 -h localhost -p "$PORT" -U "$PG_UTF8_USER_NAME" -d "$PG_UTF8_DATABASE"
  if [[ $? -ne 0 ]]; then
    echo "pgbench initialization failed for UTF-8 user!"
    cleanup_postgresql_image
    exit 1
  fi
  echo "pgbench initialization for UTF-8 user completed."

  echo "Initialize pgagroal"
  pgagroal_initialize_configuration
  export_pgagroal_test_variables
  if [ "$1" = "run-configs" ] || [ "$1" = "run-configs-ci" ] || [ "$1" = "run-configs-ci-nonbuild" ]; then
    run_multiple_config_tests
  else
    execute_testcases
  fi
}

if [[ $# -gt 1 ]]; then
   usage # More than one argument, show usage and exit
elif [[ $# -eq 1 ]]; then
  if [[ "$1" == "setup" ]]; then
    build_postgresql_image
    # Install LLVM coverage dependencies or GCC dependencies
    sudo dnf install -y \
      cmake \
      make \
      libev libev-devel \
      openssl openssl-devel \
      systemd systemd-devel \
      zlib zlib-devel \
      libzstd libzstd-devel \
      lz4 lz4-devel \
      libssh libssh-devel \
      libatomic \
      bzip2 bzip2-devel \
      libarchive libarchive-devel \
      check check-devel check-static
    if [[ $MODE != "gcc" ]]; then
      echo "Installing LLVM coverage tools..."
      sudo dnf install -y \
        clang \
        clang-analyzer \
        llvm \
        libasan \
        libasan-static
    else
      echo "Using GCC mode (no LLVM coverage tools)"
      sudo dnf install -y gcc gcc-c++
    fi

    echo "Setup complete"
  elif [[ "$1" == "clean" ]]; then
    sudo rm -Rf $COVERAGE_DIR
    cleanup
    cleanup_postgresql_image
    sudo rm -Rf $PGAGROAL_ROOT_DIR
  elif [[ "$1" == "run-configs" ]]; then
    trap cleanup EXIT SIGINT
    run_tests "run-configs"
  elif [[ "$1" == "ci" ]]; then
    MODE="ci"
    PORT=5432
    trap cleanup EXIT
    run_tests
  elif [[ "$1" == "run-configs-ci" ]]; then
    MODE="ci"
    PORT=5432
    trap cleanup EXIT
    run_tests "run-configs"
  elif [[ "$1" == "ci-nonbuild" ]]; then
    MODE="ci"
    PORT=5432
    trap cleanup EXIT
    run_tests "ci-nonbuild"
  elif [[ "$1" == "run-configs-ci-nonbuild" ]]; then
    MODE="ci"
    PORT=5432
    trap cleanup EXIT
    run_tests "run-configs-ci-nonbuild"
  elif [[ "$1" == "gcc" ]]; then
    MODE="gcc"
    trap cleanup EXIT SIGINT
    run_tests
  else
    echo "Invalid parameter: $1"
    usage # If an invalid parameter is provided, show usage and exit
  fi
else
   # If no arguments are provided, run function_without_param
   trap cleanup EXIT SIGINT
   run_tests
fi
