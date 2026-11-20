#!/usr/bin/env bash
# Verify that a Wine checkout is exactly the source named by wine.lock. SPDX-License-Identifier: LGPL-2.1-or-later
# Usage: check-wine-source.sh WINE_CHECKOUT [LOCKFILE]   exit 0 = match, 1 = mismatch, 2 = usage/lock error
# Accepts the locked commit, or any commit with the locked tree (rewritten history, same files); the base commit must be an ancestor; the worktree must be clean.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
checkout=${1:-}
lock=${2:-$root/wine.lock}
die() { echo "$*" >&2; exit "${2:-1}"; }
[[ -n $checkout ]] || die 'Usage: check-wine-source.sh WINE_CHECKOUT [LOCKFILE]' 2
[[ -f $lock ]] || die "Missing lock file: $lock" 2
git -C "$checkout" rev-parse --git-dir >/dev/null 2>&1 || die "Not a git checkout: $checkout" 2
declare -A want
while IFS='=' read -r key value; do
    [[ -z $key || $key == \#* ]] && continue
    want[$key]=$value
done < "$lock"
for key in commit tree base_commit; do
    [[ ${want[$key]:-} =~ ^[0-9a-f]{40}$ ]] || die "wine.lock: bad or missing $key" 2
done
head=$(git -C "$checkout" rev-parse HEAD)
tree=$(git -C "$checkout" rev-parse 'HEAD^{tree}')
[[ $tree == "${want[tree]}" ]] || die "Wine tree mismatch: have $tree, lock wants ${want[tree]}"
[[ $head == "${want[commit]}" ]] || echo "note: commit $head differs from locked ${want[commit]}; tree matches" >&2
git -C "$checkout" merge-base --is-ancestor "${want[base_commit]}" HEAD || die "Base ${want[base_commit]} is not an ancestor of HEAD"
[[ -z $(git -C "$checkout" status --porcelain) ]] || die 'Wine worktree is not clean'
echo "OK wine source $head tree $tree"
