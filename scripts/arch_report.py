#!/usr/bin/env python3
"""
OMS V1 — Architecture Progress Report

Scans the project source tree and generates a per-module completion
estimate, listing what is implemented and what is missing.
"""

import os
import re
import math

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ── Module descriptors ──────────────────────────────────────────────────────
# Each module: (path, name, weight, implemented_keywords, missing_items, total_loc_estimate)
MODULES = [
    {
        "path": "oms",
        "name": "OMS API",
        "weight": 25,
        "implemented": [
            "HTTP REST endpoints", "WebSocket server", "Connection handling",
            "Order routing", "Execution reporting", "JSON serialization"
        ],
        "missing": [
            "Rate Limiting", "Request validation middleware",
            "TLS/SSL support", "API key authentication"
        ],
        "keywords_impl": ["handleHttpRequest", "handleWebSocket", "buildResponse",
                          "processOrder", "publishExecution", "orderToJson",
                          "handleAuth", "handlePing"],
    },
    {
        "path": "riskmanager",
        "name": "Risk Manager",
        "weight": 15,
        "implemented": [
            "Order validation", "Symbol whitelist", "Exchange whitelist",
            "Buying power check", "Position limit check",
            "Exposure limit check", "Daily loss limit check"
        ],
        "missing": [
            "Cross-portfolio risk", "Real-time margin calculation",
            "VaR estimation", "Stress testing"
        ],
        "keywords_impl": ["validateOrder", "checkRequiredFields",
                          "hasBuyingPower", "withinPositionLimits",
                          "withinExposureLimits", "withinDailyLoss"],
    },
    {
        "path": "oadapter",
        "name": "Order Adapter",
        "weight": 15,
        "implemented": [
            "Exchange registration", "Exchange routing",
            "Order placement", "Order cancellation",
            "Order modification"
        ],
        "missing": [
            "Broker failover", "Smart order routing",
            "Latency monitoring", "Order retry logic"
        ],
        "keywords_impl": ["registerExchange", "routeOrder",
                          "cancelOrder", "modifyOrder",
                          "connectExchange", "disconnectExchange"],
    },
    {
        "path": "database",
        "name": "Database",
        "weight": 20,
        "implemented": [
            "Connection pooling", "Prepared statements",
            "CRUD operations", "Schema management",
            "Order persistence", "Execution persistence",
            "Position tracking", "Portfolio management",
            "Risk events", "Audit logging",
            "Session management", "Metrics collection",
            "Crash recovery"
        ],
        "missing": [
            "Read replicas", "Sharding", "Connection retry with backoff"
        ],
        "keywords_impl": ["getConnection", "returnConnection",
                          "storeOrder", "updateOrder", "getOrders",
                          "storeExecution", "updatePosition",
                          "setPortfolio", "storeRiskEvent",
                          "storeAudit", "storeSession",
                          "storeMetrics", "loadOpenOrders",
                          "initPreparedStatements"],
    },
    {
        "path": "exchange",
        "name": "Exchange Layer",
        "weight": 15,
        "implemented": [
            "IExchange interface", "Exchange1 (immediate fill)",
            "Exchange2 (partial fill)", "Exchange3 (acknowledge)",
            "Order lifecycle simulation"
        ],
        "missing": [
            "Market data feed", "Real exchange connectivity",
            "FIX protocol", "Order book management"
        ],
        "keywords_impl": ["IExchange", "Exchange1", "Exchange2", "Exchange3",
                          "placeOrder", "cancelOrder", "modifyOrder"],
    },
    {
        "path": "include",
        "name": "Core Models",
        "weight": 5,
        "implemented": [
            "Order model", "Position model", "Portfolio model",
            "Execution report model", "Risk result model",
            "Enum serialization"
        ],
        "missing": [],
        "keywords_impl": ["struct Order", "struct Position", "struct Portfolio",
                          "struct ExecutionReport", "struct OrderRequest",
                          "stateToString", "sideToString", "typeToString"],
    },
    {
        "path": "tests",
        "name": "Test Suite",
        "weight": 5,
        "implemented": [
            "REST API tests", "WebSocket tests",
            "Database persistence tests", "Stress tests",
            "Final reporting"
        ],
        "missing": [
            "Unit tests", "Risk manager tests", "Exchange adapter tests"
        ],
        "keywords_impl": ["test_rest_health", "test_rest_status",
                          "test_websocket_full", "test_database_persistence",
                          "test_stress", "def test_rest", "# REST TESTS"],
    },
]


def count_lines(filepath):
    try:
        with open(filepath, "r") as f:
            return sum(1 for _ in f)
    except Exception:
        return 0


def find_files(base_dir, extensions):
    matches = []
    if not os.path.isdir(base_dir):
        return matches
    for root, _dirs, files in os.walk(base_dir):
        for f in files:
            if any(f.endswith(ext) for ext in extensions):
                matches.append(os.path.join(root, f))
    return matches


def count_todos(base_dir):
    total = 0
    for root, _dirs, files in os.walk(base_dir):
        for f in files:
            if f.endswith((".cpp", ".h", ".py", ".sql")):
                path = os.path.join(root, f)
                try:
                    with open(path, "r") as fp:
                        for line in fp:
                            if "TODO" in line or "FIXME" in line or "HACK" in line:
                                total += 1
                except Exception:
                    pass
    return total


def check_keywords(filepath, keywords):
    if not os.path.isfile(filepath):
        return 0, keywords
    try:
        with open(filepath, "r") as f:
            content = f.read()
    except Exception:
        return 0, keywords
    found = sum(1 for kw in keywords if kw in content)
    return found, keywords


def check_keywords_in_dir(dirpath, keywords):
    files = find_files(dirpath, [".cpp", ".h", ".py"])
    combined = ""
    for f in files:
        try:
            with open(f, "r") as fp:
                combined += fp.read()
        except Exception:
            pass
    found = sum(1 for kw in keywords if kw in combined)
    return found, keywords


def estimate_completion(mod):
    base_dir = os.path.join(PROJECT_DIR, mod["path"])
    keywords = mod["keywords_impl"]
    found, total = check_keywords_in_dir(base_dir, keywords)
    keyword_score = found / max(len(total), 1)

    source_files = find_files(base_dir, [".cpp", ".h"])
    if not source_files and mod["path"] == "tests":
        source_files = find_files(base_dir, [".py"])
    loc = sum(count_lines(f) for f in source_files)

    # Implemented vs missing items ratio
    imp_count = len(mod["implemented"])
    mis_count = len(mod["missing"])
    impl_ratio = imp_count / max(imp_count + mis_count, 1)

    # Size ratio — each keyword implies ~80 LOC average
    expected_loc = len(keywords) * 80
    size_ratio = min(loc / max(expected_loc, 1), 1.5)
    size_score = min(size_ratio, 1.0)

    # Combined: keyword presence (40%), impl vs missing (40%), size (20%)
    score = keyword_score * 0.40 + impl_ratio * 0.40 + size_score * 0.20
    score = min(max(score * 100, 5), 100)
    return round(score), loc


def print_section(title, items, status="Implemented", indent=4):
    prefix = " " * indent
    print(f"{prefix}{status}")
    for item in items:
        print(f"{prefix}  ✓ {item}")


def print_missing(title, items, indent=4):
    if not items:
        return
    prefix = " " * indent
    print(f"{prefix}Missing")
    for item in items:
        print(f"{prefix}  ✗ {item}")


def generate_report():
    print()
    print("=" * 60)
    print("  OMS V1 ARCHITECTURE PROGRESS REPORT")
    print("=" * 60)
    print()

    total_weight = 0
    weighted_score = 0
    total_loc = 0

    for mod in MODULES:
        score, loc = estimate_completion(mod)
        total_loc += loc
        weighted_score += score * mod["weight"]
        total_weight += mod["weight"]

        print(f"  {mod['name']}")
        print(f"  {'─' * (len(mod['name']) + 2)}")
        print(f"  Status    {score}%")
        print(f"  LOC       {loc}")
        print()

        if mod["implemented"]:
            print_section(mod["name"], mod["implemented"], "Implemented")
        if mod["missing"]:
            print_missing(mod["name"], mod["missing"])

        print()
        print("-" * 60)
        print()

    overall = round(weighted_score / max(total_weight, 1))

    # Count TODOs
    todo_count = count_todos(PROJECT_DIR)

    # Total project LOC
    all_source = find_files(PROJECT_DIR, [".cpp", ".h", ".py", ".sql"])
    total_project_loc = sum(count_lines(f) for f in all_source)

    print()
    print("  OVERALL PROJECT SUMMARY")
    print("  ───────────────────────────────────────────────")
    print(f"  Overall Completion    {overall}%")
    print(f"  Total Source LOC      {total_project_loc}")
    print(f"  Modules Analyzed      {len(MODULES)}")
    print(f"  TODO / FIXME Items    {todo_count}")
    print()

    if overall >= 85:
        print("  Ready for")
        print("    Development")
        print("    Testing")
        print("    Paper Trading")
        print()
        print("  Not Yet Ready For")
        print("    Live Production Trading")
    elif overall >= 60:
        print("  Ready for")
        print("    Development")
        print("    Testing")
        print()
        print("  Not Yet Ready For")
        print("    Paper Trading")
        print("    Live Production Trading")
    else:
        print("  Status: Early Development")
        print("  Complete core modules before testing.")

    print()
    print("=" * 60)
    print()

    return overall


if __name__ == "__main__":
    generate_report()
