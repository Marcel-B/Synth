#!/usr/bin/env bash
# Appends today's download count of every release package to a CSV (date,release,file,downloads), so the counts,
# which GitHub only shows as running totals through its API, build up into a history. Run daily by release-stats.yml.
#
#   scripts/release-stats.sh stats/downloads.csv [releases.json]
#
# Without a JSON file it asks the API through gh (GH_TOKEN, GITHUB_REPOSITORY or Marcel-B/Synth). A day already in the
# file is not written twice, so a second run on the same day changes nothing.
set -euo pipefail

csv="$1"
today="$(date -u +%F)"

if [[ $# -ge 2 ]]; then
  releases="$(cat "$2")"
else
  releases="$(gh api --paginate "repos/${GITHUB_REPOSITORY:-Marcel-B/Synth}/releases" | jq -s 'add')"
fi

mkdir -p "$(dirname "$csv")"
[[ -s "$csv" ]] || echo "date,release,file,downloads" > "$csv"
if grep -q "^$today," "$csv"; then
  echo "Für $today stehen schon Zahlen in $csv."
  exit 0
fi

jq -r --arg day "$today" '.[] | .tag_name as $tag | .assets[] | [$day, $tag, .name, .download_count] | @csv' \
  <<<"$releases" | tr -d '"' >> "$csv"

echo "Downloads am $today:"
grep "^$today," "$csv" | awk -F, '{ sum[$2] += $4 } END { for (r in sum) printf "  %s: %d\n", r, sum[r] }' | sort
