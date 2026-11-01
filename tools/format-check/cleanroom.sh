#!/bin/bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# cleanroom.sh <role> <dir> <model>: one clean-room agent of docs/format-check.md, step 1. Role D, R, W
# or I, or a brief file for a trial. <dir> must not exist and must be outside any git checkout. It gets:
#
#   room/            what the agent sees, from make_room.py, and what it writes
#   home/            the agent's empty home directory
#   transcript.jsonl claude's stream-json output, every tool call and its result
#   stderr.txt       claude's error output
#   audit.txt        audit.py's list of the tool calls, those that may reach outside the room marked
#   meta.txt         role, model, commit, claude's version, the image, the time
#
# The agent runs in podman, with the room as /room, the home as /home/agent and the host's claude binary,
# nothing else of the host. Credentials: ANTHROPIC_API_KEY, then claude runs with --bare, or
# CLAUDE_CODE_OAUTH_TOKEN from `claude setup-token`; never the login of ~/.claude, which a refresh in
# the container could take from the host. The image, seqlz-cleanroom, is built from Containerfile on
# the first run. Exits 1 if the audit marked a call or the run did not finish.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
[[ $# -eq 3 ]] || { sed -n '4,/^set /s/^# \?//p' "$0" >&2; exit 2; }
role=$1
dir=$(realpath -m "$2")
model=$3
case $role in
D | R | W | I) ;;
*) [[ -f $role ]] || { echo "$role: no role D, R, W or I, and no brief file" >&2; exit 2; }
   role=$(realpath "$role") ;;
esac

[[ ! -e $dir ]] || { echo "$dir exists" >&2; exit 2; }
parent=$(dirname "$dir")
mkdir -p "$parent"
if git -C "$parent" rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    echo "$dir is inside a git checkout, where the agent would find it" >&2
    exit 2
fi
auth=()
bare=()
if [[ -n ${ANTHROPIC_API_KEY:-} ]]; then
    auth=(-e ANTHROPIC_API_KEY)
    bare=(--bare)
elif [[ -n ${CLAUDE_CODE_OAUTH_TOKEN:-} ]]; then
    auth=(-e CLAUDE_CODE_OAUTH_TOKEN)
else
    echo "set ANTHROPIC_API_KEY, or CLAUDE_CODE_OAUTH_TOKEN from 'claude setup-token'" >&2
    exit 2
fi
claude_bin=$(realpath "$(command -v claude)")

python3 "$here/make_room.py" "$role" "$dir/room"
mkdir "$dir/home"
if ! podman image exists seqlz-cleanroom; then
    podman build -t seqlz-cleanroom -f "$here/Containerfile" "$here"
fi
{
    echo "role: $role"
    echo "model: $model"
    echo "commit: $(git -C "$repo" rev-parse HEAD)$(git -C "$repo" diff --quiet HEAD -- docs/format.md || echo ', docs/format.md changed')"
    echo "claude: $("$claude_bin" --version)"
    echo "image: $(podman image inspect seqlz-cleanroom --format '{{.Id}}')"
    echo "auth: ${auth[1]}"
    echo "start: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$dir/meta.txt"

# --dangerously-skip-permissions: nobody answers prompts, and the container is the boundary;
# label=disable instead of :Z, which would relabel the host's claude binary
status=0
podman run --rm --userns=keep-id --security-opt label=disable \
    -v "$dir/room:/room" -v "$dir/home:/home/agent" -v "$claude_bin:/usr/local/bin/claude:ro" \
    -e HOME=/home/agent -e DISABLE_AUTOUPDATER=1 "${auth[@]}" -w /room seqlz-cleanroom \
    claude "${bare[@]}" -p "Read BRIEF.md in this directory and do what it says." \
    --disallowedTools WebFetch WebSearch --strict-mcp-config --dangerously-skip-permissions \
    --model "$model" --output-format stream-json --verbose \
    > "$dir/transcript.jsonl" 2> "$dir/stderr.txt" || status=$?
echo "end: $(date -u +%Y-%m-%dT%H:%M:%SZ), exit $status" >> "$dir/meta.txt"

audit=0
python3 "$here/audit.py" "$dir/transcript.jsonl" > "$dir/audit.txt" || audit=$?
grep '^!' "$dir/audit.txt" || true
tail -n 2 "$dir/audit.txt"
if [[ $audit -ne 0 ]]; then
    echo "$dir: the run failed or the audit marked calls, see audit.txt and stderr.txt" >&2
    exit 1
fi
