# GeneralsX @build BenderAI 21/04/2026 libcurl integration for update checker (SAGE_UPDATE_CHECK builds only)
# Finds libcurl via vcpkg on Linux/macOS. Windows builds use WinHTTP instead of curl.

if(SAGE_UPDATE_CHECK AND NOT WIN32)
    find_package(CURL REQUIRED)
    message(STATUS "libcurl found: ${CURL_VERSION_STRING}")
endif()
