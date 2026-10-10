#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify selected admission receipts, not source/run provenance or durability."""
import json
import re
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    text = Path(path).read_text(encoding="utf-8")
    rows = []
    for line in text.splitlines():
        if line.startswith(("ADMISSION_", "BINDING_CASE ", "SERVICE_", "FENCE_CASE ")):
            fields = dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^ ]+)", line))
            rows.append((line.split()[0], fields))
    return text, rows


def verify(binding_path, service_path):
    binding_text, binding = read(binding_path)
    service_text, service = read(service_path)
    for text in (binding_text, service_text):
        require("native-acceptance=Pending" in text, "missing scope marker")
    traces = 0
    observations = 0
    for rows in (binding, service):
        headers = {r["case"]: r for kind, r in rows if kind == "ADMISSION_TRACE"}
        require(len(headers) == sum(kind == "ADMISSION_TRACE" for kind, _ in rows), "duplicate trace tag")
        for tag, header in headers.items():
            records = [r for kind, r in rows if kind == "ADMISSION_RECORD" and r["case"] == tag]
            require(header["overflow"] == "0", f"trace overflow: {tag}")
            require(len(records) == int(header["count"]) <= 256, f"trace count: {tag}")
            for r in records:
                if r["call"] == "identity-mount":
                    require(int(r["ino"]) > 0 and int(r["mount"]) > 0, "invalid identity observation")
                    observations += 1
                if r["call"] != "failure":
                    require(r["errno"] == "0", "errno on successful observation")
            traces += 1
        require(all(r["case"] in headers for k, r in rows if k == "ADMISSION_RECORD"), "unframed record")
    def records(tag):
        return [r for k, r in binding if k == "ADMISSION_RECORD" and r["case"] == tag]
    exports = [r["index"] for r in records("fresh-and-cached-export") if r["call"] == "binding-export"]
    require(exports == ["0", "1"], "fresh/cache export sequence")
    require(any(r["call"] == "ancestor-stop" for r in records("sibling-admission")), "ancestor termination missing")
    foreign = records("foreign-owner")
    require(any(r["call"] == "epoch-check" and r["owner_match"] == "0" for r in foreign), "foreign epoch receipt")
    epochs = records("release-and-reacquire")
    require(any(r["call"] == "epoch-check" and r["held"] == "0" for r in epochs), "release receipt")
    require(any(r["call"] == "epoch-check" and r["held"] == "1" and r["epoch_match"] == "0" for r in epochs), "reacquire receipt")
    mounts = records("mount-rejections")
    require(any(r["call"] == "failure" and r["injected"] == "1" for r in mounts), "injected mount failure missing")
    require(any(r["call"] == "failure" and r["injected"] == "0" for r in mounts), "actual mount failure missing")
    fences = {r["case"]: r for kind, r in service if kind == "FENCE_CASE"}
    require(set(fences) == {"nochange", "final-drift", "nochange-drift"}, "fence matrix incomplete")
    for tag, r in fences.items():
        require(r["checks"] == "2", f"initial/final fence count: {tag}")
        if tag != "nochange":
            require(r["memory_preserved"] == r["primary_preserved"] == "1" and r["state"] == "RecoveryRequired", "drift preservation")
    primary = {r["case"]: r for kind, r in service if kind == "SERVICE_PRIMARY"}
    for prefix in ("fence", "nochange"):
        a, b = primary[prefix + "-before"], primary[prefix + "-after"]
        require(all(a[key] == b[key] for key in ("sha256", "bytes", "sequence", "revision")), "primary preservation mismatch")
        require(re.fullmatch(r"[0-9a-f]{64}", a["sha256"]), "invalid primary digest")
    allocation = [r for k, r in service if k == "SERVICE_ALLOCATION"]
    require(len(allocation) == 1 and int(allocation[0]["failures"]) > 0 and allocation[0]["fd_baseline"] == allocation[0]["fd_final"], "allocation/FD cleanup")
    require("ADMISSION_OVERFLOW_TEST capacity=256 detected=1 synthetic=1" in binding_text, "overflow negative test missing")
    return {"status": "verified", "traces": traces, "identity_observations": observations,
            "fence_cases": len(fences), "native_acceptance": "Pending",
            "scope": "selected observations and consistency only; mount namespace case remains Skipped"}


if __name__ == "__main__":
    try:
        require(len(sys.argv) == 3, "usage: verifier BINDING_RUN_LOG SERVICE_RUN_LOG")
        print(json.dumps(verify(*sys.argv[1:]), sort_keys=True))
    except (ValueError, KeyError, OSError) as error:
        print(f"admission receipt verification failed: {error}", file=sys.stderr)
        sys.exit(1)
