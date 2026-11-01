#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""audit.py <transcript.jsonl>: every tool call of a clean-room agent, one per line, from the
transcript claude writes with --output-format stream-json. A line that may reach outside the room is
marked "!": a web tool, a file tool on a path outside /room and /tmp, a shell command with a network
tool, a URL, or a path of the host such as /home. The marks are a filter for the reader, not a verdict:
the runner reads each one. Exits 1 if there is any."""

import json
import re
import sys

ROOM = ("/room", "/tmp")
WEB_TOOLS = {"WebFetch", "WebSearch"}
FILE_TOOLS = {"Read": "file_path", "Write": "file_path", "Edit": "file_path", "NotebookEdit": "notebook_path",
              "Glob": "path", "Grep": "path"}
NETWORK = re.compile(r"\b(curl|wget|git|pip3?|npm|npx|gh|ssh|scp|nc|ncat|telnet|dnf|yum)\b|https?://|\bftp://")
HOST_PATHS = re.compile(r"(?<![\w.~/-])(/(home|root|mnt|media|run|srv|opt|var|etc|proc)\b|~|\$HOME\b)")


def check(name, args):
    """the reason a call is marked, or None"""
    if name in WEB_TOOLS:
        return "web tool"
    if name in FILE_TOOLS:
        path = args.get(FILE_TOOLS[name])
        if path and not path.startswith(ROOM):
            return f"path outside the room: {path}"
        return None
    if name == "Bash":
        cmd = args.get("command", "")
        if NETWORK.search(cmd):
            return "network: " + NETWORK.search(cmd).group(0)
        if HOST_PATHS.search(cmd):
            return "path of the host: " + HOST_PATHS.search(cmd).group(0)
    return None


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
            why = check(item["name"], args)
            marked += why is not None
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
