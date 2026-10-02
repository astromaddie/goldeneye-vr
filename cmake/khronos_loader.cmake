# The Khronos OpenXR loader for Android, for the Steam Frame build
# (GEVR_STEAM_FRAME). Lepton, the Frame's Android container, has no OpenXR
# runtime broker, so the loader must come with the app (it finds SteamVR through
# /vendor/etc/openxr/1/active_runtime.json); Meta's Quest loader does not work
# there.
#
# Fetched once at configure time from Maven Central, checked against a pinned
# SHA-256, and unpacked into third_party/openxr-khronos-<version> (git-ignored).
# Sets GEVR_KHRONOS_LOADER_SO to the arm64-v8a libopenxr_loader.so.
#
# The headers in OpenXR/Include stay as they are; the loader is backwards
# compatible with the 1.1.54 headers the code is written against.

set(GEVR_KHRONOS_LOADER_VERSION "1.1.63")
set(GEVR_KHRONOS_LOADER_SHA256 "622419d2f6741c3443a3beb4779af0764318edd01830de967f24c741ebcded73")
set(GEVR_KHRONOS_LOADER_URL
    "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/${GEVR_KHRONOS_LOADER_VERSION}/openxr_loader_for_android-${GEVR_KHRONOS_LOADER_VERSION}.aar")

set(_gevr_loader_dir "${CMAKE_SOURCE_DIR}/third_party/openxr-khronos-${GEVR_KHRONOS_LOADER_VERSION}")
set(GEVR_KHRONOS_LOADER_SO "${_gevr_loader_dir}/jni/arm64-v8a/libopenxr_loader.so")

if(NOT EXISTS "${GEVR_KHRONOS_LOADER_SO}")
  set(_gevr_loader_aar "${_gevr_loader_dir}/openxr_loader_for_android.aar")
  message(STATUS "Downloading ${GEVR_KHRONOS_LOADER_URL}")
  file(DOWNLOAD "${GEVR_KHRONOS_LOADER_URL}" "${_gevr_loader_aar}"
       EXPECTED_HASH SHA256=${GEVR_KHRONOS_LOADER_SHA256}
       TLS_VERIFY ON
       STATUS _gevr_loader_status)
  list(GET _gevr_loader_status 0 _gevr_loader_code)
  if(NOT _gevr_loader_code EQUAL 0)
    file(REMOVE "${_gevr_loader_aar}")
    message(FATAL_ERROR "Khronos OpenXR loader download failed or its SHA-256 differs: ${_gevr_loader_status}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${_gevr_loader_aar}" DESTINATION "${_gevr_loader_dir}"
       PATTERNS "jni/arm64-v8a/libopenxr_loader.so")
  file(REMOVE "${_gevr_loader_aar}")
endif()

if(NOT EXISTS "${GEVR_KHRONOS_LOADER_SO}")
  message(FATAL_ERROR "libopenxr_loader.so not found in the Khronos AAR: ${GEVR_KHRONOS_LOADER_SO}")
endif()
