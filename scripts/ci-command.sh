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

  # Include compiler/test errors even when they occur before the end of a
  # parallel build log. Fall back to the tail for failures without diagnostics.
  diagnostics_file="$(mktemp "${TMPDIR:-/tmp}/shelter-${safe_label}-diagnostics.XXXXXX")"
  if [[ "$label" == "runtime" ]]; then
    grep -Ei 'error|fatal|SHELTER_WEB_SMOKE_(CURL_PROBE|REQUESTED|NAVIGATE|VIEW_CREATED|OVERLAY_ATTACHED|CLIENT_AFTER_CREATED|BROWSER_CREATED|BEFORE_BROWSE|LOAD_START|LOADING_STATE|LOAD_ERROR|DOCUMENT_LOADED|RENDERER_TERMINATED)|SHELTER_DASHBOARD_LAYOUT' \
      "$log_file" | tail -n 20 >"$diagnostics_file" || true
  else
    grep -Ei 'error|fatal|undefined reference|unresolved external|no such file|not found' \
      "$log_file" | tail -n 20 >"$diagnostics_file" || true
  fi
  if [[ ! -s "$diagnostics_file" ]]; then
    tail -n 30 "$log_file" >"$diagnostics_file"
  fi

  # Split the selected output into short annotations accepted by GitHub Checks.
  while IFS= read -r line; do
    line="${line:0:2000}"
    line="${line//'%'/'%25'}"
    line="${line//$'\r'/'%0D'}"
    line="${line//$'\n'/'%0A'}"
    printf '::error title=SHELTER %s diagnostic::%s\n' "$label" "$line"
  done < "$diagnostics_file"

  rm -f "$log_file" "$diagnostics_file"
  exit "$status"
fi
