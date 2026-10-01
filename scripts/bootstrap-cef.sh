#!/usr/bin/env bash
set -euo pipefail
V='154.0.32+g682c378+chromium-154.0.8037.58'
P="${CEF_PLATFORM:-$(uname -s)}"; case "$P" in Darwin|darwin|macos) P=macosx64;; MINGW*|MSYS*|windows) P=windows64;; *) P=linux64;; esac
A="cef_binary_${V//+/%2B}_${P}_minimal.tar.bz2"; R="$(cd "$(dirname "$0")/..";pwd)"; mkdir -p "$R/.cache"
curl --fail --location --retry 3 -o "$R/.cache/$A" "https://cef-builds.spotifycdn.com/$A"
rm -rf "$R/cef"; mkdir "$R/cef"; tar -xjf "$R/.cache/$A" -C "$R/cef" --strip-components=1
echo "CEF installed: $R/cef"
