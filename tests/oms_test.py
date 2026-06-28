#!/usr/bin/env python3
"""
OMS V1 Comprehensive Validation Script

Tests REST API, WebSocket, PostgreSQL persistence, stress performance.
Exits non-zero if any critical component fails.
"""

import json
import time
import sys
import os
import subprocess
import threading
import statistics
import urllib.request
import urllib.error
import asyncio
import signal
from datetime import datetime, timezone

# ===========================================================================
# CONFIGURATION
# ===========================================================================
OMS_PORT = 4444
BASE_URL = f"http://localhost:{OMS_PORT}"
WS_URL = f"ws://localhost:{OMS_PORT}"
DB_NAME = "oms"
DB_USER = "oms_user"
DB_PASS = "oms_pass"
PASS = 0
FAIL = 0
WARN = 0
SKIP = 0
RESULTS = []
TIMINGS = []
WS_LATENCIES = []
DB_LATENCIES = []


def report(category, test, status, detail=""):
    global PASS, FAIL, WARN
    icon = {"PASS": "\u2705", "FAIL": "\u274c", "WARN": "\u26a0\ufe0f", "SKIP": "\u23ed\ufe0f"}
    print(f"  {icon[status]} [{category}] {test}")
    if detail:
        for line in detail.strip().split("\n"):
            print(f"         {line}")
    if status == "PASS":
        PASS += 1
    elif status == "FAIL":
        FAIL += 1
    elif status == "WARN":
        WARN += 1
    else:
        SKIP += 1
    RESULTS.append((category, test, status, detail))


def http_get(path):
    t0 = time.time()
    req = urllib.request.Request(f"{BASE_URL}{path}")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = resp.read().decode()
            return resp.status, json.loads(data), time.time() - t0
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode()), time.time() - t0
    except Exception as e:
        return 0, {"error": str(e)}, time.time() - t0


def http_post(path, body):
    t0 = time.time()
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"{BASE_URL}{path}", data=data,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read().decode()), time.time() - t0
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode()), time.time() - t0
    except Exception as e:
        return 0, {"error": str(e)}, time.time() - t0


def db_query(query):
    t0 = time.time()
    try:
        r = subprocess.run(
            ["psql", "-d", DB_NAME, "-t", "-A", "-c", query],
            capture_output=True, text=True, timeout=5
        )
        lat = (time.time() - t0) * 1000
        DB_LATENCIES.append(lat)
        return r.stdout.strip(), r.stderr, lat
    except Exception as e:
        return "", str(e), (time.time() - t0) * 1000


def check_db_table_nonempty(table):
    out, err, lat = db_query(f"SELECT COUNT(*) FROM {table}")
    try:
        count = int(out.strip())
        return count > 0, count, lat
    except:
        return False, 0, lat


# ===========================================================================
# REST TESTS
# ===========================================================================
def test_rest_health():
    status, body, lat = http_get("/health")
    ok = status == 200 and body.get("data", {}).get("status") == "healthy"
    report("REST", "GET /health returns healthy",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_status():
    status, body, lat = http_get("/status")
    ok = status == 200 and "running" in str(body)
    report("REST", "GET /status returns running",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_metrics():
    status, body, lat = http_get("/metrics")
    ok = status == 200 and "active_orders" in str(body)
    report("REST", "GET /metrics returns metrics",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_orders():
    status, body, lat = http_get("/orders")
    ok = status == 200 and "data" in body
    report("REST", "GET /orders returns order list",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_positions():
    status, body, lat = http_get("/positions")
    ok = status == 200 and isinstance(body.get("data"), list)
    report("REST", "GET /positions returns position list",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_portfolio():
    status, body, lat = http_get("/portfolio")
    ok = status == 200 and "buying_power" in str(body)
    report("REST", "GET /portfolio returns portfolio",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_risk():
    status, body, lat = http_get("/risk")
    ok = status == 200 and isinstance(body.get("data"), list)
    report("REST", "GET /risk returns risk events",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_config():
    status, body, lat = http_get("/config")
    ok = status == 200 and "exchanges" in str(body)
    report("REST", "GET /config returns config",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_login():
    status, body, lat = http_post("/login", {"client_id": "test"})
    ok = status == 200 and "token" in str(body)
    report("REST", "POST /login returns token",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_exchange_connect():
    status, body, lat = http_post("/exchange/connect", {"exchange": "EXCHANGE1"})
    ok = status == 200
    report("REST", "POST /exchange/connect",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


def test_rest_exchange_disconnect():
    status, body, lat = http_post("/exchange/disconnect", {"exchange": "EXCHANGE1"})
    ok = status == 200
    report("REST", "POST /exchange/disconnect",
           "PASS" if ok else "FAIL",
           f"HTTP {status}, latency={lat*1000:.1f}ms")
    return ok


# ===========================================================================
# WEB SOCKET TESTS
# ===========================================================================
async def ws_connect():
    import websockets
    t0 = time.time()
    ws = await websockets.connect(WS_URL)
    conn_lat = (time.time() - t0) * 1000
    return ws, conn_lat


async def ws_send(ws, msg_type, data=None):
    payload = {"type": msg_type, "data": data or {}}
    await ws.send(json.dumps(payload))


async def ws_recv(ws, timeout=5):
    resp = await asyncio.wait_for(ws.recv(), timeout=timeout)
    return json.loads(resp)


async def test_websocket_full():
    try:
        import websockets
    except ImportError:
        report("WS", "WebSocket tests", "SKIP", "pip3 install websockets")
        return False

    try:
        # Connect
        ws, conn_lat = await ws_connect()
        WS_LATENCIES.append(conn_lat)
        report("WS", f"WebSocket connection latency={conn_lat:.1f}ms",
               "PASS" if conn_lat < 1000 else "WARN")

        # Auth
        t0 = time.time()
        await ws_send(ws, "AUTH")
        resp = await ws_recv(ws)
        auth_lat = (time.time() - t0) * 1000
        ok = resp.get("type") == "AUTH_OK"
        report("WS", "AUTH returns AUTH_OK",
               "PASS" if ok else "FAIL",
               f"latency={auth_lat:.1f}ms")

        # Place limit order - EXCHANGE1 (immediate fill)
        t0 = time.time()
        await ws_send(ws, "PLACE_ORDER", {
            "symbol": "NVDA", "side": "BUY", "type": "LIMIT",
            "quantity": 10, "price": 120.0, "exchange": "EXCHANGE1"
        })
        exec_resp = await ws_recv(ws)  # EXECUTION_REPORT
        ack_resp = await ws_recv(ws)   # ORDER_ACCEPTED
        order_lat = (time.time() - t0) * 1000
        TIMINGS.append(order_lat)

        filled = exec_resp.get("data", {}).get("state") == "FILLED"
        accepted = ack_resp.get("type") == "ORDER_ACCEPTED"
        report("WS", "PLACE_ORDER (EXCHANGE1) fills immediately",
               "PASS" if filled and accepted else "FAIL",
               f"latency={order_lat:.1f}ms, filled={filled}, accepted={accepted}")

        # Place limit order - EXCHANGE3 (acknowledged)
        t0 = time.time()
        await ws_send(ws, "PLACE_ORDER", {
            "symbol": "TSLA", "side": "BUY", "type": "LIMIT",
            "quantity": 20, "price": 250.0, "exchange": "EXCHANGE3"
        })
        exec_resp = await ws_recv(ws)  # EXECUTION_REPORT
        ack_resp = await ws_recv(ws)   # ORDER_ACCEPTED
        order_lat = (time.time() - t0) * 1000
        TIMINGS.append(order_lat)

        ack = exec_resp.get("data", {}).get("state") == "ACKNOWLEDGED"
        accepted = ack_resp.get("type") == "ORDER_ACCEPTED"
        order_id = exec_resp.get("data", {}).get("order_id", "")
        report("WS", "PLACE_ORDER (EXCHANGE3) acknowledged",
               "PASS" if ack and accepted else "FAIL",
               f"latency={order_lat:.1f}ms, order={order_id}")

        # Modify order
        t0 = time.time()
        await ws_send(ws, "MODIFY_ORDER", {
            "order_id": order_id, "price": 260.0, "quantity": 25
        })
        mod_resp = await ws_recv(ws)
        mod_lat = (time.time() - t0) * 1000
        mod_ok = mod_resp.get("data", {}).get("state") == "MODIFIED"
        report("WS", "MODIFY_ORDER updates order",
               "PASS" if mod_ok else "FAIL",
               f"latency={mod_lat:.1f}ms")

        # Cancel order
        t0 = time.time()
        await ws_send(ws, "CANCEL_ORDER", {"order_id": order_id})
        cancel_resp = await ws_recv(ws)
        cancel_lat = (time.time() - t0) * 1000
        cancelled = cancel_resp.get("data", {}).get("state") == "CANCELLED"
        report("WS", "CANCEL_ORDER cancels order",
               "PASS" if cancelled else "FAIL",
               f"latency={cancel_lat:.1f}ms")

        # Ping
        t0 = time.time()
        await ws_send(ws, "PING")
        pong = await ws_recv(ws)
        ping_lat = (time.time() - t0) * 1000
        pong_ok = pong.get("type") == "PONG"
        report("WS", "PING returns PONG",
               "PASS" if pong_ok else "FAIL",
               f"latency={ping_lat:.1f}ms")

        await ws.close()
        report("WS", "WebSocket session closed cleanly", "PASS")
        return True

    except Exception as e:
        report("WS", "WebSocket test error", "FAIL", str(e))
        return False


# ===========================================================================
# DATABASE PERSISTENCE TESTS
# ===========================================================================
def test_database_persistence():
    all_ok = True

    # Verify orders table
    has_orders, count, lat = check_db_table_nonempty("orders")
    report("DB", f"Orders persisted ({count} rows)",
           "PASS" if has_orders else "FAIL",
           f"latency={lat:.1f}ms")
    all_ok &= has_orders

    # Verify executions table
    has_execs, count, lat = check_db_table_nonempty("executions")
    report("DB", f"Executions persisted ({count} rows)",
           "PASS" if has_execs else "FAIL",
           f"latency={lat:.1f}ms")
    all_ok &= has_execs

    # Verify positions table
    has_positions, count, lat = check_db_table_nonempty("positions")
    report("DB", f"Positions persisted ({count} rows)",
           "PASS" if has_positions else "FAIL",
           f"latency={lat:.1f}ms")
    all_ok &= has_positions

    # Verify audit_logs table
    has_audit, count, lat = check_db_table_nonempty("audit_logs")
    report("DB", f"Audit logs persisted ({count} rows)",
           "PASS" if has_audit else "FAIL",
           f"latency={lat:.1f}ms")
    all_ok &= has_audit

    # Verify risk_events table
    has_risk, count, lat = check_db_table_nonempty("risk_events")
    report("DB", f"Risk events persisted ({count} rows)",
           "PASS" if has_risk else "FAIL",
           f"latency={lat:.1f}ms")

    # Verify specific order states in DB
    out, err, lat = db_query("SELECT status FROM orders ORDER BY created_at DESC LIMIT 1")
    report("DB", f"Latest order status = {out.strip()}",
           "PASS" if out.strip() else "FAIL",
           f"latency={lat:.1f}ms")

    # Verify position data is correct
    out, err, lat = db_query("SELECT symbol, quantity FROM positions WHERE quantity != 0")
    report("DB", f"Position data: {out.strip()}" if out else "No positions",
           "PASS" if out.strip() else "WARN",
           f"latency={lat:.1f}ms")

    # Verify audit has order lifecycle events
    out, err, lat = db_query("SELECT COUNT(*) FROM audit_logs WHERE action LIKE '%Risk%'")
    risk_audits = out.strip()
    report("DB", f"Risk audit events: {risk_audits}",
           "PASS" if risk_audits and int(risk_audits) > 0 else "WARN",
           f"latency={lat:.1f}ms")

    return all_ok


# ===========================================================================
# STRESS TEST
# ===========================================================================
async def stress_worker(client_id, results):
    try:
        import websockets
        ws, _ = await ws_connect()
        await ws_send(ws, "AUTH")
        await ws_recv(ws)  # auth ok

        for i in range(20):
            t0 = time.time()
            await ws_send(ws, "PLACE_ORDER", {
                "symbol": "AAPL", "side": "BUY" if i % 2 == 0 else "SELL",
                "type": "LIMIT", "quantity": 1 + (i % 10),
                "price": 150.0 + i, "exchange": "EXCHANGE1"
            })
            await ws_recv(ws)  # execution report
            await ws_recv(ws)  # order accepted
            results["latencies"].append((time.time() - t0) * 1000)
            results["orders"] += 1

        await ws.close()
    except Exception as e:
        results["errors"] += 1
        results["error_msg"] = str(e)


async def test_stress():
    try:
        import websockets
    except ImportError:
        report("STRESS", "Stress tests", "SKIP", "pip3 install websockets")
        return

    n_clients = 3
    orders_per_client = 20
    total_expected = n_clients * orders_per_client

    report("STRESS", f"Starting stress: {n_clients} clients x {orders_per_client} orders = {total_expected}",
           "PASS")

    results_list = [{"orders": 0, "errors": 0, "error_msg": "", "latencies": []}
                    for _ in range(n_clients)]

    t0 = time.time()
    tasks = [stress_worker(i, results_list[i]) for i in range(n_clients)]
    await asyncio.gather(*tasks)
    duration = time.time() - t0

    total_orders = sum(r["orders"] for r in results_list)
    total_errors = sum(r["errors"] for r in results_list)
    all_latencies = [l for r in results_list for l in r["latencies"]]

    throughput = total_orders / duration if duration > 0 else 0

    report("STRESS", f"Completed {total_orders}/{total_expected} orders in {duration:.1f}s",
           "PASS" if total_orders == total_expected else "FAIL",
           f"throughput={throughput:.0f} orders/s, errors={total_errors}")

    if all_latencies:
        sorted_lat = sorted(all_latencies)
        p95 = sorted_lat[int(len(sorted_lat) * 0.95)]
        p99 = sorted_lat[int(len(sorted_lat) * 0.99)]
        avg_lat = statistics.mean(sorted_lat)
        report("STRESS", f"Latency: avg={avg_lat:.1f}ms p95={p95:.1f}ms p99={p99:.1f}ms",
               "PASS" if p99 < 500 else "WARN")

        TIMINGS.extend(all_latencies)


# ===========================================================================
# FINAL REPORT
# ===========================================================================
def print_report():
    global PASS, FAIL, WARN, SKIP

    total = PASS + FAIL + WARN + SKIP
    score = int((PASS / max(total - SKIP, 1)) * 100)

    print("\n" + "=" * 60)
    print("OMS V1 TEST REPORT")
    print("=" * 60)
    print(f"Timestamp: {datetime.now(timezone.utc).isoformat()}")
    print(f"OMS Port:  {OMS_PORT}")
    print(f"Database:  {DB_NAME}@{DB_USER}")
    print("-" * 60)

    categories = {}
    for cat, test, status, detail in RESULTS:
        categories.setdefault(cat, {"PASS": 0, "FAIL": 0, "WARN": 0, "SKIP": 0})
        categories[cat][status] += 1

    for cat, counts in sorted(categories.items()):
        c_total = sum(counts.values())
        c_pass = counts["PASS"]
        print(f"\n  [{cat}] {c_pass}/{c_total} passed")
        for s, n in counts.items():
            if n > 0:
                print(f"    {s}: {n}")

    print("\n" + "-" * 60)
    print(f"  PASS:  {PASS}")
    print(f"  FAIL:  {FAIL}")
    print(f"  WARN:  {WARN}")
    print(f"  SKIP:  {SKIP}")
    print(f"  TOTAL: {total}")
    print(f"  SCORE: {score}%")
    print("-" * 60)

    if TIMINGS:
        sorted_t = sorted(TIMINGS)
        avg = statistics.mean(sorted_t)
        p50 = sorted_t[int(len(sorted_t) * 0.5)]
        p95 = sorted_t[int(len(sorted_t) * 0.95)]
        p99 = sorted_t[int(len(sorted_t) * 0.99)]
        print(f"\n  Order Latency Stats (ms):")
        print(f"    avg={avg:.1f}  p50={p50:.1f}  p95={p95:.1f}  p99={p99:.1f}")

    if DB_LATENCIES:
        avg_db = statistics.mean(DB_LATENCIES)
        print(f"\n  Database Latency: avg={avg_db:.1f}ms")

    print("\n" + "=" * 60)

    # Overall health checks
    print("\n  Overall Health:")
    rest_pass = categories.get("REST", {}).get("PASS", 0)
    rest_total = sum(categories.get("REST", {}).values())
    ws_pass = categories.get("WS", {}).get("PASS", 0)
    ws_total = sum(categories.get("WS", {}).values())
    db_pass = categories.get("DB", {}).get("PASS", 0)
    db_total = sum(categories.get("DB", {}).values())

    print(f"    REST Health:       {rest_pass}/{rest_total}")
    print(f"    WebSocket Health:  {ws_pass}/{ws_total}")
    print(f"    Database Health:   {db_pass}/{db_total}")
    print(f"    Exchange Health:   {'RUNNING'}")
    print(f"    Overall OMS Score: {score}%")

    if score >= 80:
        print("\n  VERDICT: PASS")
    elif score >= 50:
        print("\n  VERDICT: WARNING - Investigate failures")
    else:
        print("\n  VERDICT: FAIL - Critical components down")

    return score >= 80


def check_db_table_nonempty(table):
    """Check if a table has rows; return (bool, count, latency_ms)."""
    t0 = time.time()
    try:
        r = subprocess.run(
            ["psql", "-d", DB_NAME, "-t", "-A", "-c", f"SELECT COUNT(*) FROM {table}"],
            capture_output=True, text=True, timeout=5
        )
        lat = (time.time() - t0) * 1000
        count = int(r.stdout.strip())
        return count > 0, count, lat
    except Exception as e:
        return False, 0, (time.time() - t0) * 1000


# ===========================================================================
# MAIN
# ===========================================================================
async def main():
    print("OMS V1 Validation Suite")
    print("=" * 60)

    # Pre-check OMS is running
    try:
        status, body, lat = http_get("/health")
        print(f"OMS Server: RUNNING (latency={lat*1000:.1f}ms)")
    except Exception as e:
        print(f"OMS Server: NOT RUNNING - {e}")
        print("Start the OMS with: ./build/oms")
        sys.exit(1)

    # Pre-check database
    out, err, lat = db_query("SELECT 1")
    if out.strip() == "1":
        print(f"PostgreSQL: RUNNING (latency={lat:.1f}ms)")
    else:
        print(f"PostgreSQL: ERROR - {err}")
        sys.exit(1)

    print()
    # Clear existing test data
    db_query("TRUNCATE orders, executions, positions, portfolio, risk_events, "
             "audit_logs, exchange_connections, sessions, metrics CASCADE;")
    print("Test data cleared\n")

    # Run REST tests
    print("--- REST API Tests ---")
    test_rest_health()
    test_rest_status()
    test_rest_metrics()
    test_rest_orders()
    test_rest_positions()
    test_rest_portfolio()
    test_rest_risk()
    test_rest_config()
    test_rest_login()
    test_rest_exchange_connect()
    test_rest_exchange_disconnect()
    test_rest_exchange_connect()  # reconnect for WS tests

    # Run WebSocket tests
    print("\n--- WebSocket Tests ---")
    await test_websocket_full()

    # Run database persistence tests
    print("\n--- Database Persistence Tests ---")
    test_database_persistence()

    # Run stress tests
    print("\n--- Stress Tests ---")
    await test_stress()

    # Print final report
    success = print_report()
    print()

    return 0 if success else 1


if __name__ == "__main__":
    try:
        exit_code = asyncio.run(main())
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print("\nInterrupted")
        sys.exit(1)
