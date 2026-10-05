#!/usr/bin/env bash
# Run a CI command while exposing its final output as check-run annotations on failure.
# This keeps useful compiler/test diagnostics available even if the hosted-runner log
# archive cannot be downloaded through the Actions UI/API.
set -uo pipefail

if (( $# < 2 )); then
  echo "usage: $0 <label> <command> [args...]" >&2
  exit 2
fi

label="$1"
shift
safe_label="${label//[^[:alnum:]_-]/-}"
log_file="$(mktemp "${TMPDIR:-/tmp}/shelter-${safe_label}.XXXXXX")"

if "$@" >"$log_file" 2>&1; then
  cat "$log_file"
  rm -f "$log_file"
  exit 0
else
  status=$?
  cat "$log_file"
  printf '::error title=SHELTER %s failed::Command exited with status %s\n' "$label" "$status"

  # Keep the end of the output, which contains the compiler diagnostic and build
  # summary, and split it into short annotations accepted by GitHub Checks.
  while IFS= read -r line; do
    line="${line:0:2000}"
    line="${line//'%'/'%25'}"
    line="${line//$'\r'/'%0D'}"
    line="${line//$'\n'/'%0A'}"
    printf '::error title=SHELTER %s diagnostic::%s\n' "$label" "$line"
  done < <(tail -n 30 "$log_file")

  rm -f "$log_file"
  exit "$status"
fi
