#!/usr/bin/env bash
# Version-controlled snapshot of the local Hindsight memory bank (dsh-cbn).
#
# Usage: ./tools/hind-backup.sh
# Output: dev-memory/dsh-cbn/snapshot.json (full content) + template.json (bank config)
# Git history over repeated runs is the versioning; push the repo for off-machine safety.
#
# RESTORE (after volume loss or fresh container):
#   1. docker run -d --name hindsight --restart unless-stopped -p 8888:8888 -p 9999:9999 \
#        -v hindsight-data:/home/hindsight/.pg0  [env vars per tools/hind-backup.sh header in bank memory]
#   2. POST the bank template:  curl -X POST .../banks/dsh-cbn/import  (recreate bank if missing)
#   3. Replay memories:         POST .../banks/dsh-cbn/memories for each item in snapshot.memories
#                               (server recomputes embeddings; no LLM fact extraction needed)
#   4. (Optional) Re-retain snapshot.documents[*].text for full document/chunk fidelity.
set -euo pipefail

API="${HINDSIGHT_API:-http://127.0.0.1:8888/v1/default/banks/dsh-cbn}"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/dev-memory/dsh-cbn"
mkdir -p "$OUT_DIR"

# 1. Server must be up
code=$(curl -s --max-time 5 -o /dev/null -w '%{http_code}' http://127.0.0.1:8888/health)
if [ "$code" != "200" ]; then echo "hindsight API not healthy (HTTP $code) - run: hind start" >&2; exit 1; fi

# 2. Bank template (missions/config) - small, always diffable
curl -s --max-time 30 "$API/export" -o "$OUT_DIR/template.json"

# 3. Full content snapshot: memories + documents (with text) + entity graph + stats
API="$API" OUT="$OUT_DIR/snapshot.json" python3 <<'PY'
import json, os, sys, urllib.request

api = os.environ["API"]
out = os.environ["OUT"]

def get(path, params=None):
    url = api + path
    if params:
        url += "?" + "&".join(f"{k}={v}" for k, v in params.items())
    with urllib.request.urlopen(url, timeout=120) as r:
        return json.load(r)

def page_all(path):
    items, offset, total = [], 0, None
    while total is None or offset < total:
        d = get(path, {"limit": 500, "offset": offset})
        items.extend(d["items"])
        total = d["total"]
        offset += 500
    return items, total

memories, mem_total = page_all("/memories/list")
memories.sort(key=lambda m: m["id"])

docs_meta, doc_total = page_all("/documents")
docs = []
for dm in sorted(docs_meta, key=lambda d: d["id"]):
    d = get(f"/documents/{dm['id']}")
    docs.append({"id": d["id"], "created_at": d.get("created_at"),
                 "text_length": d.get("text_length"), "text": d.get("original_text", "")})

try:
    entities = get("/entities/graph")
except Exception as e:
    entities = {"error": str(e)}

stats = get("/stats")
snap = {
    "hindsight_snapshot_version": 1,
    "created_at": __import__("datetime").datetime.now(__import__("datetime").timezone.utc).isoformat(),
    "counts": {"memories": mem_total, "documents": doc_total},
    "stats_at_snapshot": stats,
    "memories": memories,
    "documents": docs,
    "entities": entities,
}
with open(out, "w") as f:
    json.dump(snap, f, indent=1, ensure_ascii=False, sort_keys=True)
print(f"snapshot: {mem_total} memories, {doc_total} documents -> {out}")
PY

echo "backup complete: $OUT_DIR"
ls -lh "$OUT_DIR"
