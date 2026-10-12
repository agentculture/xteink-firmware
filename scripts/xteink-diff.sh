#!/usr/bin/env bash
# Print the xteink fork's diff against upstream CrossPoint develop, for review
# against the allowed areas in docs/xteink/CONTRIBUTING.md.
set -euo pipefail
remote="${XTEINK_UPSTREAM_REMOTE:-upstream}"
if ! git remote get-url "$remote" >/dev/null 2>&1; then
  echo "error: no '$remote' remote; add it with:" >&2
  echo "  git remote add $remote https://github.com/crosspoint-reader/crosspoint-reader.git" >&2
  exit 2
fi
git fetch --quiet "$remote" develop
base="$(git merge-base HEAD "$remote/develop")"
echo "xteink fork diff vs $remote/develop (merge-base ${base:0:10}):"
git diff --stat "$base" HEAD
