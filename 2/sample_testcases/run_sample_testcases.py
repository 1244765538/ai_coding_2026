#!/usr/bin/env python3
import json
import os
import sys
import urllib.error
import urllib.request


BASE_URL = os.environ.get(
    "BASE_URL", "http://localhost:" + os.environ.get("PORT", "8000")
).rstrip("/")
PASSED = 0
FAILED = 0


def request(method, path, body=None):
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode()
    call = urllib.request.Request(
        BASE_URL + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method=method,
    )
    with urllib.request.urlopen(call, timeout=3) as response:
        return response.status, json.loads(response.read().decode())


def check(name, function):
    global PASSED, FAILED
    try:
        function()
        PASSED += 1
        print(f"[PASS] {name}")
    except Exception as exc:
        FAILED += 1
        print(f"[FAIL] {name}: {exc}")


def health():
    status, data = request("GET", "/health")
    assert status == 200 and data == {"status": "ok"}


def reset_and_create():
    status, data = request("POST", "/reset", {})
    assert status == 200 and data == {"ok": True}
    status, poll = request(
        "POST",
        "/polls",
        {"title": "Lunch", "options": ["Rice", "Noodles"]},
    )
    assert status == 200
    assert poll == {
        "id": "poll_1",
        "title": "Lunch",
        "status": "open",
        "results": [
            {"option": "Rice", "votes": 0},
            {"option": "Noodles", "votes": 0},
        ],
        "total_votes": 0,
    }


def first_vote():
    request("POST", "/reset", {})
    request("POST", "/polls", {"title": "Color", "options": ["Red", "Blue"]})
    status, poll = request("PUT", "/polls/poll_1/votes/user_1", {"option": "Blue"})
    assert status == 200
    assert poll["total_votes"] == 1
    assert poll["results"][0] == {"option": "Blue", "votes": 1}


check("健康检查", health)
check("重置并创建投票", reset_and_create)
check("首次投票", first_vote)
print(f"{PASSED} passed, {FAILED} failed")
print("默认样例仅验证服务连通性,部分契约的约定实现方式,不做业务层面校验测试")
print("样例全部通过,不代表任何用例通过效果")
sys.exit(1 if FAILED else 0)
