#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""audit.py <transcript.jsonl>: every tool call of a clean-room agent, one per line, from the
transcript claude writes with --output-format stream-json. A line that may reach outside the room is
marked "!": a tool cleanroom.sh does not give, e.g. a web tool, a file tool on a path outside /room
and /tmp, a shell command with a network tool, a URL, or a path of the host such as /home. The marks
are a filter for the reader, not a verdict: the runner reads each one. Exits 1 if there is any."""

import json
import re
import sys

ROOM = ("/room", "/tmp")
WEB_TOOLS = {"WebFetch", "WebSearch"}
# what cleanroom.sh gives the agent; any other tool in the transcript is marked
TOOLS = {"Bash", "Read", "Write", "Edit", "Glob", "Grep"}
FILE_TOOLS = {"Read": "file_path", "Write": "file_path", "Edit": "file_path", "Glob": "path", "Grep": "path"}
NETWORK = re.compile(r"\b(curl|wget|git|pip3?|npm|npx|gh|ssh|scp|nc|ncat|telnet|dnf|yum)\b|https?://|\bftp://")
# ~ only as the home directory, ~/ or ~ alone, not C's bitwise not as in ~0U
HOST_PATHS = re.compile(r"(?<![\w.~/-])(/(home|root|mnt|media|run|srv|opt|var|etc|proc)\b|~(?=/|\s|$)|\$HOME\b)")


def check(name, args):
    """the reasons a call is marked, empty if none"""
    if name not in TOOLS:
        return ["web tool" if name in WEB_TOOLS else "a tool cleanroom.sh does not give"]
    if name in FILE_TOOLS:
        path = args.get(FILE_TOOLS[name])
        return [f"path outside the room: {path}"] if path and not path.startswith(ROOM) else []
    if name == "Bash":
        cmd = args.get("command", "")
        return ([f"network: {m.group(0)}" for m in NETWORK.finditer(cmd)]
                + [f"path of the host: {m.group(0)}" for m in HOST_PATHS.finditer(cmd)])
    return []


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    calls = marked = 0
    result = None
    with open(sys.argv[1], encoding="utf-8") as f:
        lines = f.readlines()
    for line in lines:
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("type") == "result":
            result = event
        if event.get("type") != "assistant":
            continue
        for item in event.get("message", {}).get("content", []):
            if item.get("type") != "tool_use":
                continue
            calls += 1
            args = item.get("input", {})
            why = ", ".join(dict.fromkeys(check(item["name"], args)))
            marked += bool(why)
            what = args.get("command") or args.get(FILE_TOOLS.get(item["name"], ""), "") or json.dumps(args)
            print(f"{'!' if why else ' '} {item['name']}: {' '.join(str(what).split())[:200]}"
                  + (f"   <- {why}" if why else ""))
    print(f"{calls} tool calls, {marked} marked")
    if not result:
        print("no result in the transcript: the run did not finish")
    elif result.get("is_error"):
        print(f"the run failed: {' '.join(str(result.get('result')).split())[:200]}")
    else:
        print(f"result: {result.get('num_turns')} turns, {result.get('duration_ms', 0) / 60000:.1f} min, "
              f"{result.get('total_cost_usd')} USD")
    sys.exit(1 if marked or not result or result.get("is_error") else 0)


if __name__ == "__main__":
    main()
