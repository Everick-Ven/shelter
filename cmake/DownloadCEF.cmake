# Copyright (c) 2016 The Chromium Embedded Framework Authors. All rights
# reserved. Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# Download the CEF binary distribution for |platform| and |version| to
# |download_dir|. The |CEF_ROOT| variable will be set in global scope pointing
# to the extracted location. The expected SHA-256 is pinned by the project and
# must be updated together with the CEF version.
# Visit https://cef-builds.spotifycdn.com/index.html for supported builds.

function(DownloadCEF platform channel version download_dir expected_sha256)
  string(LENGTH "${expected_sha256}" CEF_EXPECTED_HASH_LENGTH)
  if(NOT CEF_EXPECTED_HASH_LENGTH EQUAL 64 OR NOT "${expected_sha256}" MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "A pinned lowercase SHA-256 is required for CEF ${version} ${platform}")
  endif()

  if(channel STREQUAL "beta")
    set(channel_part "_beta")
  elseif(channel STREQUAL "minimal")
    # Минимальный дистрибутив (только Release, без tests/ и Debug) — в разы меньше.
    set(channel_part "_minimal")
  else()
    set(channel_part "")
  endif()

  set(CEF_DISTRIBUTION "cef_binary_${version}_${platform}${channel_part}")
  set(CEF_DOWNLOAD_DIR "${download_dir}")
  set(CEF_ROOT "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}" CACHE INTERNAL "CEF_ROOT" FORCE)
  set(CEF_DOWNLOAD_FILENAME "${CEF_DISTRIBUTION}.tar.bz2")
  set(CEF_DOWNLOAD_PATH "${CEF_DOWNLOAD_DIR}/${CEF_DOWNLOAD_FILENAME}")
  set(CEF_VERIFIED_MARKER "${CEF_ROOT}/.shelter-cef-archive-sha256")

  file(MAKE_DIRECTORY "${CEF_DOWNLOAD_DIR}")
  set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_DOWNLOAD_FILENAME}")
  string(REPLACE "+" "%2B" CEF_DOWNLOAD_URL_ESCAPED "${CEF_DOWNLOAD_URL}")

  if(NOT EXISTS "${CEF_DOWNLOAD_PATH}")
    message(STATUS "Downloading pinned CEF ${version} ${platform} archive...")
    file(
      DOWNLOAD "${CEF_DOWNLOAD_URL_ESCAPED}" "${CEF_DOWNLOAD_PATH}"
      EXPECTED_HASH "SHA256=${expected_sha256}"
      TLS_VERIFY ON
      SHOW_PROGRESS
      )
  endif()

  file(SHA256 "${CEF_DOWNLOAD_PATH}" CEF_ACTUAL_SHA256)
  if(NOT CEF_ACTUAL_SHA256 STREQUAL expected_sha256)
    message(FATAL_ERROR
      "CEF archive SHA-256 mismatch for ${CEF_DOWNLOAD_FILENAME}: "
      "expected ${expected_sha256}, got ${CEF_ACTUAL_SHA256}. Remove the archive and retry only after review.")
  endif()

  set(CEF_NEEDS_EXTRACTION TRUE)
  if(IS_DIRECTORY "${CEF_ROOT}" AND EXISTS "${CEF_VERIFIED_MARKER}")
    file(READ "${CEF_VERIFIED_MARKER}" CEF_MARKER_SHA256)
    string(STRIP "${CEF_MARKER_SHA256}" CEF_MARKER_SHA256)
    if(CEF_MARKER_SHA256 STREQUAL expected_sha256)
      set(CEF_NEEDS_EXTRACTION FALSE)
    endif()
  endif()

  if(CEF_NEEDS_EXTRACTION)
    if(IS_DIRECTORY "${CEF_ROOT}")
      message(STATUS "Re-extracting CEF because the cached tree has no matching verified-archive marker...")
      file(REMOVE_RECURSE "${CEF_ROOT}")
    endif()
    message(STATUS "Extracting verified CEF archive ${CEF_DOWNLOAD_PATH}...")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xjf "${CEF_DOWNLOAD_PATH}"
      WORKING_DIRECTORY "${CEF_DOWNLOAD_DIR}"
      RESULT_VARIABLE CEF_EXTRACT_RESULT
      )
    if(NOT CEF_EXTRACT_RESULT EQUAL 0 OR NOT IS_DIRECTORY "${CEF_ROOT}")
      message(FATAL_ERROR "Failed to extract verified CEF archive ${CEF_DOWNLOAD_PATH}")
    endif()
    file(WRITE "${CEF_VERIFIED_MARKER}" "${expected_sha256}\n")
  endif()
endfunction()
