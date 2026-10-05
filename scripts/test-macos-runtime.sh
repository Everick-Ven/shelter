#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <SHELTER.app>" >&2
  exit 2
fi

app_path="$1"
if [[ "$app_path" != /* ]]; then
  app_path="$PWD/$app_path"
fi
app_path="$(cd "$app_path" && pwd -P)"
main_executable="$app_path/Contents/MacOS/shelter"
framework="$app_path/Contents/Frameworks/Chromium Embedded Framework.framework"
helper_bundle="$app_path/Contents/Frameworks/SHELTER Helper.app"
helper_executable="$helper_bundle/Contents/MacOS/SHELTER Helper"
main_info="$app_path/Contents/Info.plist"
helper_info="$helper_bundle/Contents/Info.plist"

for required in "$main_executable" "$framework/Chromium Embedded Framework" \
                "$helper_executable" "$main_info" "$helper_info" \
                "$app_path/Contents/Resources/ui/index.html"; do
  if [[ ! -e "$required" ]]; then
    echo "Missing packaged macOS app component: $required" >&2
    exit 1
  fi
done
if [[ ! -x "$main_executable" || ! -x "$helper_executable" ]]; then
  echo "The main and CEF helper executables must both be executable." >&2
  exit 1
fi

plutil -lint "$main_info" "$helper_info"
helper_package_type="$(/usr/libexec/PlistBuddy -c 'Print :CFBundlePackageType' "$helper_info")"
if [[ "$helper_package_type" != "APPL" ]]; then
  echo "CEF helper bundle must have CFBundlePackageType APPL, got '$helper_package_type'." >&2
  exit 1
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/shelter-runtime.XXXXXX")"
export SHELTER_HELPER_BUNDLE_NAME="SHELTER Helper"
export SHELTER_MAIN_EXECUTABLE="$main_executable"

find_main_pids() {
  local pids
  pids="$(ps -axo pid=,command= | awk '
    index($0, ENVIRON["SHELTER_MAIN_EXECUTABLE"]) > 0 { print $1 }
  ')"
  if [[ -z "$pids" ]]; then
    pids="$(pgrep -x shelter || pgrep -x SHELTER || true)"
  fi
  printf '%s\n' "$pids" | awk 'NF'
}

find_helper_pids() {
  ps -axo pid=,command= | awk '
    index($0, ENVIRON["SHELTER_HELPER_BUNDLE_NAME"]) > 0 { print $1 }
  '
}

find_renderer_pid() {
  ps -axo pid=,command= | awk '
    index($0, ENVIRON["SHELTER_HELPER_BUNDLE_NAME"]) > 0 &&
    $0 ~ /--type=renderer([[:space:]]|$)/ { print $1; exit }
  '
}

print_logs() {
  for log_file in "$work_dir/open.log" "$work_dir/shelter.log" \
                  "$work_dir/shelter-cef.log"; do
    if [[ -f "$log_file" ]]; then
      echo "--- $log_file ---" >&2
      tail -n 100 "$log_file" >&2 || true
    fi
  done
}

cleanup() {
  local main_pids helper_pids pid
  main_pids="$(find_main_pids || true)"
  while IFS= read -r pid; do
    [[ "$pid" =~ ^[0-9]+$ ]] && kill "$pid" 2>/dev/null || true
  done <<< "$main_pids"
  sleep 1
  helper_pids="$(find_helper_pids || true)"
  while IFS= read -r pid; do
    [[ "$pid" =~ ^[0-9]+$ ]] && kill "$pid" 2>/dev/null || true
  done <<< "$helper_pids"
  rm -rf "$work_dir"
}
trap cleanup EXIT

cd "$work_dir"
if ! open -n "$app_path" >"$work_dir/open.log" 2>&1; then
  echo "Launch Services could not open $app_path." >&2
  print_logs
  exit 1
fi

main_seen=0
for _ in $(seq 1 60); do
  renderer_pid="$(find_renderer_pid || true)"
  if [[ "$renderer_pid" =~ ^[0-9]+$ ]]; then
    echo "Packaged macOS app started its CEF renderer (PID $renderer_pid)."
    exit 0
  fi

  main_pids="$(find_main_pids || true)"
  if [[ -n "$main_pids" ]]; then
    main_seen=1
  elif (( main_seen )); then
    echo "SHELTER exited before starting a CEF renderer." >&2
    print_logs
    exit 1
  fi
  sleep 1
done

echo "Timed out waiting for the packaged SHELTER app to start a CEF renderer." >&2
print_logs
exit 1
