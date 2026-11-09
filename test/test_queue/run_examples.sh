#!/usr/bin/env bash
#
# Example scenarios for oversubscription_test against a pgagroal instance.
#
# Fill in the connection variables, then run:  ./run_examples.sh [scenario...]
# With no arguments all scenarios run in order.
#
# IMPORTANT pgagroal configuration notes:
#   - max_connections must equal MAX_CONN below (and PostgreSQL's own
#     max_connections must be >= MAX_CONN + headroom for the sampler
#     connection and admin tools).
#   - Scenarios 1 and 4 need a generous wait_queue_timeout (e.g. 30s) so the
#     queue absorbs the burst instead of timing out.
#   - Scenario 2 needs wait_queue_timeout (e.g. 10s) SMALLER than HOLD below.
#   - Run on an otherwise idle system (the sampler counts ALL non-idle
#     backends on the PostgreSQL instance).

set -u

HOST=${HOST:-localhost}
PGPORT=${PGPORT:-2345}          # pgagroal port
PGDIRECT=${PGDIRECT:-5432}      # PostgreSQL port (for the sampler)
USER=${USER:-mohammad}
DB=${DB:-postgres}
MAX_CONN=${MAX_CONN:-100}
CLIENTS=${CLIENTS:-200}

BIN=${BIN:-./oversubscription_test}
DSN_DIRECT="host=${HOST} port=${PGDIRECT} user=${USER} dbname=${DB}"

run() {
  echo "=============================================================="
  echo "\$ $*"
  echo "=============================================================="
  "$@"
  local rc=$?
  echo "---- exit code: ${rc} ----"
  echo
  return 0
}

scenario_burst_absorb() {
  # 1) Burst absorption: 200 clients vs 100 slots, each holds 0.5s.
  #    Contract: ALL 200 served, occupancy never exceeds 100, zero errors.
  run "${BIN}" -H "${HOST}" -p "${PGPORT}" -U "${USER}" -d "${DB}" \
      --mode burst --clients "${CLIENTS}" --max-connections "${MAX_CONN}" \
      --hold 0.5 \
      --activity-dsn "${DSN_DIRECT}"
}

scenario_timeout_contract() {
  # 2) Timeout contract: holders keep their slots for 30s while
  #    wait_queue_timeout is (say) 10s -> exactly (CLIENTS - MAX_CONN)
  #    clients must fail, at ~10s, with the pool-full error.
  run "${BIN}" -H "${HOST}" -p "${PGPORT}" -U "${USER}" -d "${DB}" \
      --mode burst --clients "${CLIENTS}" --max-connections "${MAX_CONN}" \
      --hold 30 --expect-failures $((CLIENTS - MAX_CONN)) \
      --activity-dsn "${DSN_DIRECT}"
}

scenario_fifo_order() {
  # 3) FIFO order: small pool so admission order is observable; clients are
  #    launched 50ms apart so their enqueue order is known. With
  #    --require-fifo any admission-order inversion fails the run.
  run "${BIN}" -H "${HOST}" -p "${PGPORT}" -U "${USER}" -d "${DB}" \
      --mode stagger --clients 12 --max-connections 2 \
      --hold 2 --stagger 50 --require-fifo
}

scenario_churn_high_load() {
  # 4) Churn under sustained 2x oversubscription: 200 clients loop
  #    connect/SELECT/disconnect for 60s. Contract: nobody starves
  #    (every client keeps completing transactions, max gap <= 10s),
  #    occupancy never exceeds 100.
  run "${BIN}" -H "${HOST}" -p "${PGPORT}" -U "${USER}" -d "${DB}" \
      --mode churn --clients "${CLIENTS}" --max-connections "${MAX_CONN}" \
      --duration 60 --starve-limit 10 \
      --activity-dsn "${DSN_DIRECT}"
}

scenario_heavy_burst() {
  # 5) Heavy burst: 400 clients vs 100 slots (4x oversubscription).
  #    Adjust CLIENTS via env: CLIENTS=400 ./run_examples.sh heavy
  local n=${CLIENTS_HEAVY:-400}
  run "${BIN}" -H "${HOST}" -p "${PGPORT}" -U "${USER}" -d "${DB}" \
      --mode burst --clients "${n}" --max-connections "${MAX_CONN}" \
      --hold 0.5 \
      --activity-dsn "${DSN_DIRECT}"
}

sel=${1:-all}
for s in "${sel:-all}"; do
  case "${s}" in
    all)     scenario_burst_absorb; scenario_timeout_contract; scenario_fifo_order; scenario_churn_high_load; scenario_heavy_burst ;;
    burst)   scenario_burst_absorb ;;
    timeout) scenario_timeout_contract ;;
    fifo)    scenario_fifo_order ;;
    churn)   scenario_churn_high_load ;;
    heavy)   scenario_heavy_burst ;;
    *) echo "unknown scenario: ${s} (all|burst|timeout|fifo|churn|heavy)"; exit 2 ;;
  esac
done
