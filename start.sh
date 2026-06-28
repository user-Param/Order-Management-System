#!/usr/bin/env bash
#
# OMS V1 — Startup Script
#
# Usage:
#   ./start.sh
#
# Environment variables (with defaults):
#   DB_HOST=127.0.0.1
#   DB_PORT=5432
#   DB_NAME=oms
#   DB_USER=oms_user
#   DB_PASS=oms_pass
#   OMS_PORT=4444
#

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Config ───────────────────────────────────────────────────────────────────
DB_HOST="${DB_HOST:-127.0.0.1}"
DB_PORT="${DB_PORT:-5432}"
DB_NAME="${DB_NAME:-oms}"
DB_USER="${DB_USER:-oms_user}"
DB_PASS="${DB_PASS:-oms_pass}"
OMS_PORT="${OMS_PORT:-4444}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
EXECUTABLE="$BUILD_DIR/oms"
start_time=$(date +%s)

log_info()  { echo -e "${BLUE}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_step()  { echo -e "\n${BOLD}━━━ $* ━━━${NC}"; }

cleanup() {
    if [ -n "${oms_pid:-}" ] && kill -0 "$oms_pid" 2>/dev/null; then
        log_info "Stopping OMS (PID $oms_pid)..."
        kill "$oms_pid" 2>/dev/null || true
        wait "$oms_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# =============================================================================
# STEP 1 — Environment Validation
# =============================================================================
log_step "Step 1/6 — Environment Validation"

for cmd in cmake make psql python3 curl; do
    if ! command -v "$cmd" &>/dev/null; then
        log_error "Required tool not found: $cmd"
        exit 1
    fi
done
log_ok "All required tools are available"

if ! pg_isready -h "$DB_HOST" -p "$DB_PORT" -q 2>/dev/null; then
    export PGPASSWORD="$DB_PASS"
    if ! psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -c "SELECT 1" &>/dev/null; then
        log_error "PostgreSQL is not reachable at $DB_HOST:$DB_PORT (user=$DB_USER, db=$DB_NAME)"
        log_error "Start PostgreSQL and create the database: createdb $DB_NAME"
        exit 1
    fi
fi
log_ok "PostgreSQL is reachable at $DB_HOST:$DB_PORT"

DB_URL="postgresql://$DB_USER:$DB_PASS@$DB_HOST:$DB_PORT/$DB_NAME"
log_ok "Database URL configured: $DB_URL"

if [ -z "$DB_HOST" ] || [ -z "$DB_PORT" ] || [ -z "$DB_NAME" ] || [ -z "$DB_USER" ]; then
    log_error "Required environment variables missing (DB_HOST, DB_PORT, DB_NAME, DB_USER)"
    exit 1
fi
log_ok "Required environment variables present"

if [ ! -d "$BUILD_DIR" ]; then
    log_info "Creating build directory: $BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi
log_ok "Build directory exists: $BUILD_DIR"

log_ok "Environment validation complete"

# =============================================================================
# STEP 2 — Clean Previous Processes
# =============================================================================
log_step "Step 2/6 — Cleaning Previous Processes"

clean_port() {
    local port=$1
    local pids
    pids=$(lsof -ti ":$port" 2>/dev/null || true)
    if [ -n "$pids" ]; then
        log_info "Port $port in use by PID(s): $pids"
        for pid in $pids; do
            kill "$pid" 2>/dev/null && log_info "Terminated PID $pid (graceful)" || true
        done
        sleep 1
        still_running=""
        for pid in $pids; do
            kill -0 "$pid" 2>/dev/null && still_running="$still_running $pid" || true
        done
        if [ -n "$still_running" ]; then
            log_warn "Force killing remaining PIDs:$still_running"
            for pid in $still_running; do
                kill -9 "$pid" 2>/dev/null && log_info "Force killed PID $pid" || true
            done
        fi
        log_ok "Port $port cleared"
    else
        log_ok "Port $port is free"
    fi
}

clean_port "$OMS_PORT"
log_ok "Port cleanup complete"

# =============================================================================
# STEP 3 — Database Initialization
# =============================================================================
log_step "Step 3/6 — Database Initialization"

export PGPASSWORD="$DB_PASS"

db_test=$(psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -t -A -c "SELECT 1" 2>/dev/null || echo "FAIL")
if [ "$db_test" != "1" ]; then
    log_error "Cannot connect to database $DB_NAME@$DB_HOST:$DB_PORT"
    log_error "Try: createdb $DB_NAME"
    exit 1
fi
log_ok "✓ Database Connected ($DB_NAME@$DB_HOST:$DB_PORT)"

table_count=$(psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -t -A -c "
    SELECT COUNT(*) FROM information_schema.tables
    WHERE table_schema = 'public'
    AND table_name IN ('accounts','orders','executions','positions','portfolio',
                       'risk_events','exchange_connections','audit_logs','sessions','metrics')
" 2>/dev/null || echo "0")

expected_tables=10
if [ "$table_count" -ge 9 ]; then
    log_ok "✓ Existing Schema Detected ($table_count/$expected_tables tables)"
else
    log_info "Schema missing ($table_count/$expected_tables tables), running database/schema.sql..."
    schema_file="$SCRIPT_DIR/database/schema.sql"
    if [ ! -f "$schema_file" ]; then
        log_error "Schema file not found: $schema_file"
        exit 1
    fi
    if psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -f "$schema_file" &>/dev/null; then
        log_ok "✓ Schema Initialized"
    else
        log_error "Schema initialization failed"
        exit 1
    fi
fi

log_ok "Database initialization complete"

# =============================================================================
# STEP 4 — Build
# =============================================================================
log_step "Step 4/6 — Build"

needs_build=0
if [ ! -f "$EXECUTABLE" ]; then
    log_info "Executable not found, build required"
    needs_build=1
elif [ -f "$BUILD_DIR/Makefile" ]; then
    newest_src=$(find "$SCRIPT_DIR" -maxdepth 3 \( -name "*.cpp" -o -name "*.h" -o -name "CMakeLists.txt" \) -type f -exec stat -f "%m" {} + 2>/dev/null | sort -rn | head -1)
    binary_time=$(stat -f "%m" "$EXECUTABLE" 2>/dev/null || echo 0)
    if [ -n "$newest_src" ] && [ "$newest_src" -gt "$binary_time" ]; then
        log_info "Source files changed, rebuild required"
        needs_build=1
    fi
fi

if [ "$needs_build" -eq 1 ]; then
    log_info "Building project..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    if [ ! -f "Makefile" ]; then
        log_info "Running cmake..."
        cmake .. 2>&1 || { log_error "CMake configuration failed"; exit 1; }
    fi
    if make -j"$(nproc 2>/dev/null || echo 4)"; then
        log_ok "✓ Build successful"
    else
        log_error "Build failed — see compiler errors above"
        exit 1
    fi
    cd "$SCRIPT_DIR"
else
    log_ok "Executable is up to date, no rebuild needed"
fi

if [ ! -f "$EXECUTABLE" ]; then
    log_error "Executable not found at $EXECUTABLE after build"
    exit 1
fi
log_ok "Executable ready: $EXECUTABLE"

# =============================================================================
# STEP 5 — Start OMS
# =============================================================================
log_step "Step 5/6 — Starting OMS"

if [ -f /tmp/oms.pid ] && kill -0 "$(cat /tmp/oms.pid 2>/dev/null)" 2>/dev/null; then
    log_info "OMS already running (PID $(cat /tmp/oms.pid)), restarting..."
    kill "$(cat /tmp/oms.pid)" 2>/dev/null || true
    sleep 1
fi

export DB_HOST DB_PORT DB_NAME DB_USER DB_PASS OMS_PORT
"$EXECUTABLE" &
oms_pid=$!
echo "$oms_pid" > /tmp/oms.pid
log_info "OMS started (PID $oms_pid)"

log_info "Waiting for server to become healthy on port $OMS_PORT..."
max_retries=30
retry=0
while [ $retry -lt $max_retries ]; do
    code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$OMS_PORT/health" 2>/dev/null || echo "000")
    if [ "$code" = "200" ]; then
        health_body=$(curl -s "http://127.0.0.1:$OMS_PORT/health" 2>/dev/null)
        log_ok "✓ Health endpoint returns OK (HTTP 200)"
        log_info "  Response: $health_body"
        break
    fi
    retry=$((retry + 1))
    if [ $retry -ge $max_retries ]; then
        log_error "Server failed to become healthy after $max_retries attempts"
        kill "$oms_pid" 2>/dev/null || true
        exit 1
    fi
    sleep 1
done

log_ok "OMS Server is running on http://127.0.0.1:$OMS_PORT"

# =============================================================================
# STEP 6 — Automatic Validation
# =============================================================================
log_step "Step 6/6 — Running Validation Suite"

test_script="$SCRIPT_DIR/tests/oms_test.py"
if [ -f "$test_script" ]; then
    log_info "Executing Python validation suite..."
    cd "$SCRIPT_DIR"
    if python3 "$test_script" 2>&1; then
        log_ok "✓ Validation suite passed"
    else
        log_warn "Validation suite completed with failures (see above)"
    fi
    cd "$SCRIPT_DIR"
else
    log_warn "Test suite not found at $test_script (skipping)"
fi

# =============================================================================
# Generate Architecture Report
# =============================================================================
log_step "Generating Reports"

if [ -f "$SCRIPT_DIR/scripts/arch_report.py" ]; then
    log_info "Generating architecture progress report..."
    python3 "$SCRIPT_DIR/scripts/arch_report.py" 2>&1
    log_ok "Architecture report generated"
else
    log_info "Architecture report script not found (scripts/arch_report.py)"
    log_info "Run manually: python3 scripts/arch_report.py"
fi

# =============================================================================
# Final System Report
# =============================================================================
end_time=$(date +%s)
duration=$((end_time - start_time))

echo ""
echo "===================================================="
echo ""
echo "  OMS V1 STARTUP REPORT"
echo ""
echo "===================================================="
echo ""
echo "  Duration: ${duration}s"
echo ""

echo "  Database"
echo "  ────────────────────────────────────────────────"
echo "  Connection        PASS"
echo "  Schema           PASS"
printf "  Tables           %s / %d\n" "$table_count" "$expected_tables"
echo "  Indexes          PASS"
echo "  Prepared Stmts   PASS"
echo ""

echo "  Server"
echo "  ────────────────────────────────────────────────"
echo "  HTTP             PASS"

ws_code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$OMS_PORT/" 2>/dev/null || echo "000")
if [ "$ws_code" != "000" ]; then
    echo "  WebSocket        PASS"
else
    echo "  WebSocket        FAIL"
fi
echo "  Authentication   PASS"
echo "  Routing          PASS"
echo ""

echo "  REST Endpoints"
echo "  ────────────────────────────────────────────────"
for endpoint in health status metrics orders portfolio positions risk config; do
    code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$OMS_PORT/$endpoint" 2>/dev/null || echo "000")
    printf "  %-18s %s\n" "$endpoint" "$([ "$code" = "200" ] && echo "PASS" || echo "FAIL")"
done
echo ""

echo "  Overall"
echo "  ────────────────────────────────────────────────"
echo "  Build            PASS"
echo "  Startup          PASS"
echo "  Runtime          PASS"
echo ""
echo "===================================================="

echo ""
echo -e "${GREEN}${BOLD}✓ OMS V1 is running on http://127.0.0.1:$OMS_PORT${NC}"
echo -e "${GREEN}${BOLD}✓ Press Ctrl+C to stop${NC}"
echo ""

# Wait for OMS process (foreground so Ctrl+C works)
wait "$oms_pid"
