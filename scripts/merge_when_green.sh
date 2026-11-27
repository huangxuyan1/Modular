#!/usr/bin/env bash
set -euo pipefail

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

if [[ $# != 3 || ! $1 =~ ^[1-9][0-9]*$ || ! $2 =~ ^[0-9a-f]{40}$ ||
      -z $3 || $3 == *$'\n'* || $3 == *$'\r'* ]]; then
  fail "Usage: $0 <pr-number> <expected-head-sha> \"<subject>\" (full lowercase SHA, single-line subject)"
fi
pr=$1
expected_head=$2
subject=$3

run=$(gh run list --workflow ci.yml --event pull_request --commit "$expected_head" --limit 1 \
  --json databaseId,headSha --jq '.[0] | [.databaseId, .headSha] | @tsv') ||
  fail "Could not look up CI for $expected_head."
IFS=$'\t' read -r run_id run_head <<< "$run"
[[ $run_id =~ ^[0-9]+$ && $run_head == "$expected_head" ]] ||
  fail "No pull-request CI run found for $expected_head."

printf 'Waiting for CI run %s at %s.\n' "$run_id" "$expected_head"
gh run watch "$run_id" || fail "Could not watch CI run $run_id."
conclusion=$(gh run view "$run_id" --json conclusion --jq '.conclusion') ||
  fail "Could not verify the conclusion of CI run $run_id."
[[ $conclusion == success ]] || fail "CI run $run_id did not succeed (${conclusion:-no conclusion})."

pr_info=$(gh pr view "$pr" --json headRefOid,mergeStateStatus,baseRefName,state \
  --jq '[.headRefOid, .mergeStateStatus, .baseRefName, .state] | @tsv') ||
  fail "Could not inspect PR #$pr."
IFS=$'\t' read -r pr_head merge_state base state <<< "$pr_info"
[[ $pr_head == "$expected_head" ]] ||
  fail "PR #$pr head is ${pr_head:-unknown}; expected $expected_head."
[[ $merge_state == CLEAN && $base == main && $state == OPEN ]] ||
  fail "PR #$pr is not an open, CLEAN PR into main (state=$state, mergeStateStatus=$merge_state, base=$base)."

printf 'Squash-merging PR #%s as %s (#%s).\n' "$pr" "$subject" "$pr"
gh pr merge "$pr" --squash --match-head-commit "$expected_head" --subject="$subject (#$pr)" ||
  fail "Squash merge command failed for PR #$pr; check GitHub before retrying."
state=$(gh pr view "$pr" --json state --jq '.state') ||
  fail "Could not confirm the remote merge of PR #$pr. Local main was not updated."
[[ $state == MERGED ]] || fail "PR #$pr is not merged (state=$state). Local main was not updated."
printf 'PR #%s merged on GitHub.\n' "$pr"

branch=$(git branch --show-current) ||
  fail "PR #$pr merged on GitHub, but the current local branch could not be determined."
if [[ $branch != main ]]; then
  printf 'Skipping local main update (current checkout: %s).\n' "${branch:-detached HEAD}"
  exit 0
fi

printf 'Fast-forwarding local main from origin/main.\n'
if ! git fetch --no-recurse-submodules origin refs/heads/main ||
   ! git merge --ff-only --no-autostash --no-overwrite-ignore FETCH_HEAD; then
  fail "PR #$pr merged on GitHub, but local main could not be fast-forwarded."
fi
printf 'Local main is up to date with origin/main.\n'
