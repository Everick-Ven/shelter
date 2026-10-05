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
main_info="$app_path/Contents/Info.plist"
ui_entry="$app_path/Contents/Resources/ui/index.html"
helper_names=(
  "SHELTER Helper"
  "SHELTER Helper (GPU)"
  "SHELTER Helper (Plugin)"
  "SHELTER Helper (Renderer)"
)
helper_executables=()

for required in "$main_executable" "$framework/Chromium Embedded Framework" \
                "$main_info" "$ui_entry"; do
  if [[ ! -e "$required" ]]; then
    echo "Missing packaged macOS app component: $required" >&2
    exit 1
  fi
done
if [[ ! -x "$main_executable" ]]; then
  echo "The packaged SHELTER executable is not executable." >&2
  exit 1
fi

plutil -lint "$main_info"
codesign --verify --deep --strict "$app_path"

for helper_name in "${helper_names[@]}"; do
  helper_bundle="$app_path/Contents/Frameworks/$helper_name.app"
  helper_executable="$helper_bundle/Contents/MacOS/$helper_name"
  helper_info="$helper_bundle/Contents/Info.plist"
  for required in "$helper_executable" "$helper_info"; do
    if [[ ! -e "$required" ]]; then
      echo "Missing packaged CEF helper component: $required" >&2
      exit 1
    fi
  done
  if [[ ! -x "$helper_executable" ]]; then
    echo "The packaged CEF helper is not executable: $helper_executable" >&2
    exit 1
  fi
  plutil -lint "$helper_info"
  helper_package_type="$(/usr/libexec/PlistBuddy -c 'Print :CFBundlePackageType' "$helper_info")"
  if [[ "$helper_package_type" != "APPL" ]]; then
    echo "CEF helper bundle must have CFBundlePackageType APPL, got '$helper_package_type'." >&2
    exit 1
  fi
  helper_executables+=("$helper_executable")
done

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/shelter-runtime.XXXXXX")"
main_pid=""
export HOME="$work_dir/home"
export SHELTER_HELPER_BUNDLE_NAME="SHELTER Helper"
support_dir="$HOME/Library/Application Support/SHELTER"
mkdir -p "$HOME"

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
  for log_file in "$work_dir"/helper-*.stdout.log "$work_dir/app.stdout.log" \
                  "$support_dir/shelter.log" "$support_dir/shelter-helper.log" \
                  "$support_dir/shelter-cef.log"; do
    if [[ -f "$log_file" ]]; then
      echo "--- $log_file ---" >&2
      tail -n 100 "$log_file" >&2 || true
    fi
  done
}

cleanup() {
  local helper_pids pid
  if [[ -n "$main_pid" ]]; then
    kill "$main_pid" 2>/dev/null || true
    sleep 1
    wait "$main_pid" 2>/dev/null || true
  fi
  helper_pids="$(find_helper_pids || true)"
  while IFS= read -r pid; do
    [[ "$pid" =~ ^[0-9]+$ ]] && kill "$pid" 2>/dev/null || true
  done <<< "$helper_pids"
  rm -rf "$work_dir"
}
trap cleanup EXIT

cd "$work_dir"
for index in "${!helper_executables[@]}"; do
  helper_executable="${helper_executables[$index]}"
  if ! "$helper_executable" --shelter-helper-smoke-test \
    >"$work_dir/helper-$index.stdout.log" 2>&1; then
    echo "A packaged CEF helper could not load the bundled CEF framework: $helper_executable" >&2
    print_logs
    exit 1
  fi
done
smoke_count="$(grep -Fc "CEF helper framework load smoke test passed" \
  "$support_dir/shelter-helper.log" || true)"
if (( smoke_count < ${#helper_executables[@]} )); then
  echo "Expected a successful CEF loader smoke test for all four helper bundles; got $smoke_count." >&2
  print_logs
  exit 1
fi

"$main_executable" --disable-gpu --disable-gpu-compositing \
  >"$work_dir/app.stdout.log" 2>&1 &
main_pid=$!

for _ in $(seq 1 60); do
  if ! kill -0 "$main_pid" 2>/dev/null; then
    set +e
    wait "$main_pid"
    app_exit_code=$?
    set -e
    echo "SHELTER exited during startup (status $app_exit_code)." >&2
    print_logs
    exit 1
  fi

  if [[ -f "$support_dir/shelter.log" ]] && \
     grep -Fq "SHELTER UI main document loaded with status 200" \
       "$support_dir/shelter.log"; then
    renderer_pid="$(find_renderer_pid || true)"
    if [[ "$renderer_pid" =~ ^[0-9]+$ ]]; then
      echo "Packaged app loaded the SHELTER UI and started renderer PID $renderer_pid."
    else
      echo "Packaged app loaded the SHELTER UI in its CEF renderer."
    fi
    exit 0
  fi
  sleep 1
done

echo "Timed out waiting for the packaged app to load SHELTER UI (HTTP 200)." >&2
print_logs
exit 1
