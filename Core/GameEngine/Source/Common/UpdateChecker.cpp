/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: UpdateChecker.cpp ////////////////////////////////////////////////
// GeneralsX @feature BenderAI 21/04/2026 Non-blocking update checker via
// GitHub Releases API. Active for tagged release builds: SDL3+libcurl on
// Linux/macOS, WinHTTP+std::thread on Windows (MinGW parity builds link
// neither SDL3 nor libcurl).

#ifdef SAGE_UPDATE_CHECK

#include "Common/UpdateChecker.h"
#include "Common/GlobalData.h"

#include "gitinfo.h"

#if defined(_WIN32)
// GeneralsX @feature 12/08/2026 Native Windows backend for the MinGW build.
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <stdlib.h>
#else
#include <SDL3/SDL.h>
#include <curl/curl.h>
#endif

#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>

// ---------------------------------------------------------------------------
// GeneralsX @feature 12/08/2026 Repository the updater checks against.
// Windows binaries are published as parity releases on the fork (upstream
// releases carry no Windows asset), so the Windows build checks the fork.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#define UPDATE_RELEASES_REPO "aalbayaty/GeneralsX"
#else
#define UPDATE_RELEASES_REPO "fbraz3/GeneralsX"
#endif

#if defined(_WIN32)
// ---------------------------------------------------------------------------
// GeneralsX @feature 12/08/2026 Thread/atomic/env shims so the shared logic
// below reads identically to the SDL3 path (same pattern as the repo's other
// platform compat shims).
// ---------------------------------------------------------------------------
#define SDLCALL
typedef std::atomic<int> SDL_AtomicInt;
static int  SDL_GetAtomicInt(SDL_AtomicInt* a)        { return a->load(); }
static void SDL_SetAtomicInt(SDL_AtomicInt* a, int v) { a->store(v); }
static const char* SDL_getenv(const char* name)       { return getenv(name); }
#endif

// ---------------------------------------------------------------------------
// File-static implementation state (not exposed in the header to avoid
// leaking SDL3 includes to every consumer of UpdateChecker.h)
// ---------------------------------------------------------------------------
#if defined(_WIN32)
static bool          s_started   = false;
#else
static SDL_Thread*   s_thread    = nullptr;
#endif
// s_done defaults to 1 ("already done / not started") so poll() returns
// false immediately on early-return paths in start() that never launch a
// thread. It is reset to 0 just before the thread is created.
static SDL_AtomicInt s_done      = {1};
static SDL_AtomicInt s_hasUpdate = {0};
static char          s_latestTag[128] = {0};

// ---------------------------------------------------------------------------
// GitHub API endpoint for latest release
// ---------------------------------------------------------------------------
const char* UpdateChecker::getReleasesUrl()
{
    return "https://api.github.com/repos/" UPDATE_RELEASES_REPO "/releases/latest";
}

// ---------------------------------------------------------------------------
// Open the releases page of the repo this build updates from
// ---------------------------------------------------------------------------
void UpdateChecker::openReleasesPage()
{
#if defined(_WIN32)
    ::ShellExecuteA(nullptr, "open", "https://github.com/" UPDATE_RELEASES_REPO "/releases",
                    nullptr, nullptr, SW_SHOWNORMAL);
#else
    SDL_OpenURL("https://github.com/" UPDATE_RELEASES_REPO "/releases");
#endif
}

#if !defined(_WIN32)
// ---------------------------------------------------------------------------
// libcurl write callback: accumulates response body into a std::string*
// ---------------------------------------------------------------------------
static size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* body = static_cast<std::string*>(userdata);
    const size_t total = size * nmemb;
    // Guard against enormous responses (GitHub API is < 32 KB in practice)
    if (body->size() + total > 65536)
        return 0; // signal error to curl
    body->append(ptr, total);
    return total;
}
#endif

// ---------------------------------------------------------------------------
// HTTP GET of the releases-API endpoint into responseBody.
// Returns false on any network/TLS error (caller fails silently).
// ---------------------------------------------------------------------------
#if defined(_WIN32)
static bool fetchLatestReleaseJson(std::string& responseBody)
{
    // GeneralsX @feature 12/08/2026 WinHTTP implementation: TLS via schannel,
    // no third-party dependencies in the MinGW cross build.
    bool ok = false;
    HINTERNET hSession = WinHttpOpen(L"GeneralsX/update-checker",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        return false;
    // resolve / connect / send / receive timeouts (ms), matching the curl path
    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = nullptr;
    if (hConnect)
    {
        hRequest = WinHttpOpenRequest(hConnect, L"GET",
            L"/repos/" UPDATE_RELEASES_REPO L"/releases/latest",
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }
    if (hRequest)
    {
        const wchar_t* headers =
            L"Accept: application/vnd.github+json\r\n"
            L"X-GitHub-Api-Version: 2022-11-28\r\n";
        if (WinHttpSendRequest(hRequest, headers, (DWORD)-1,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (status == 200)
            {
                char buf[4096];
                DWORD read = 0;
                ok = true;
                while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0)
                {
                    // Guard against enormous responses (GitHub API is < 32 KB in practice)
                    if (responseBody.size() + read > 65536)
                    {
                        ok = false;
                        break;
                    }
                    responseBody.append(buf, read);
                }
            }
        }
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    fprintf(stderr, "[UpdateChecker] WinHTTP fetch %s. Response length=%zu\n",
            ok ? "succeeded" : "failed", responseBody.length());
    fflush(stderr);
    return ok && !responseBody.empty();
}
#else
static bool fetchLatestReleaseJson(std::string& responseBody)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    curl_easy_setopt(curl, CURLOPT_URL, UpdateChecker::getReleasesUrl());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "GeneralsX/update-checker");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);         // total timeout (s)
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // connect timeout (s)
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // Prevent libcurl from using process-wide POSIX signals in multi-threaded builds.
    // Required whenever curl is used from a non-main thread.
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    fprintf(stderr, "[UpdateChecker] curl_easy_perform returned %d. Response length=%zu\n", (int)res, responseBody.length());
    fflush(stderr);

    return res == CURLE_OK;
}
#endif

// ---------------------------------------------------------------------------
// Published-date extractor: parses "published_at": "VALUE" from JSON.
// Returns the ISO-8601 datetime string (e.g. "2026-04-01T12:00:00Z").
// ---------------------------------------------------------------------------
static bool extractPublishedAt(const std::string& json, char* outDate, int outDateSize)
{
    const char* key = "\"published_at\"";
    const char* pos = strstr(json.c_str(), key);
    if (!pos)
        return false;

    pos += strlen(key);
    while (*pos == ' ' || *pos == '\t' || *pos == ':')
        ++pos;
    if (*pos != '"')
        return false;
    ++pos;

    int i = 0;
    while (*pos && *pos != '"' && i < outDateSize - 1)
        outDate[i++] = *pos++;
    outDate[i] = '\0';
    return i > 0;
}

// ---------------------------------------------------------------------------
// Parse ISO-8601 UTC datetime "YYYY-MM-DDTHH:MM:SSZ" into time_t.
// Returns -1 on parse failure.
// ---------------------------------------------------------------------------
static time_t parseISO8601(const char* s)
{
    struct tm t = {};
    // sscanf is portable and avoids strptime (not available on all platforms)
    if (sscanf(s, "%d-%d-%dT%d:%d:%dZ",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) != 6)
        return (time_t)-1;
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    t.tm_isdst = 0;
#if defined(_WIN32)
    return _mkgmtime(&t);
#else
    return timegm(&t);
#endif
}

// ---------------------------------------------------------------------------
// Simple tag_name extractor: parses "tag_name": "VALUE" from JSON response.
// No external JSON library required.
// ---------------------------------------------------------------------------
static bool extractTagName(const std::string& json, char* outTag, int outTagSize)
{
    // Look for "tag_name":"VALUE" or "tag_name": "VALUE"
    const char* key = "\"tag_name\"";
    const char* pos = strstr(json.c_str(), key);
    if (!pos)
        return false;

    pos += strlen(key);

    // Skip whitespace and colon
    while (*pos == ' ' || *pos == '\t' || *pos == ':' || *pos == ' ')
        ++pos;

    if (*pos != '"')
        return false;
    ++pos; // skip opening quote

    int i = 0;
    while (*pos && *pos != '"' && i < outTagSize - 1)
    {
        outTag[i++] = *pos++;
    }
    outTag[i] = '\0';
    return i > 0;
}

// ---------------------------------------------------------------------------
// Background thread: performs HTTP GET and fills s_latestTag
// ---------------------------------------------------------------------------
static int SDLCALL threadFunc(void* /*userData*/)
{
    std::string responseBody;

    if (!fetchLatestReleaseJson(responseBody))
    {
        // Network error: fail silently
        SDL_SetAtomicInt(&s_done, 1);
        return 0;
    }

    char latestTag[128] = {0};
    if (!extractTagName(responseBody, latestTag, sizeof(latestTag)))
    {
        fprintf(stderr, "[UpdateChecker] Failed to extract tag_name from response. Response Body (truncated):\n%.256s\n", responseBody.c_str());
        fflush(stderr);
        SDL_SetAtomicInt(&s_done, 1);
        return 0;
    }
    
    fprintf(stderr, "[UpdateChecker] Extracted remote latestTag='%s'\n", latestTag);
    fflush(stderr);

    // Compare publication date of the remote release against the local build's
    // commit timestamp. Date comparison is tag-format-agnostic: it works for
    // "GeneralsX-Beta-3", "v1.0.0", or any future scheme without modification.
    // In force mode: any non-empty response is treated as "update available".
    const bool forceCheck = SDL_getenv("GENERALS_FORCE_UPDATE_CHECK") != nullptr;

    bool hasUpdate = false;
    if (forceCheck)
    {
        hasUpdate = latestTag[0] != '\0';
        fprintf(stderr, "[UpdateChecker] forceCheck is true. hasUpdate=%d\n", (int)hasUpdate);
        fflush(stderr);
    }
    else
    {
        // First check if the tag matches exactly. If it does, we are definitely on the latest version.
        if (GitTag[0] != '\0' && strcmp(latestTag, GitTag) == 0)
        {
            hasUpdate = false;
            fprintf(stderr, "[UpdateChecker] Local tag matches remote tag precisely ('%s'). No update.\n", GitTag);
            fflush(stderr);
        }
        else
        {
            const bool hasCommitTimestamp = GitCommitTimeStamp > 0;
            char publishedAt[64] = {0};
            if (hasCommitTimestamp && extractPublishedAt(responseBody, publishedAt, sizeof(publishedAt)))
            {
                time_t remoteTime = parseISO8601(publishedAt);
                fprintf(stderr, "[UpdateChecker] Extracted published_at='%s' (parsed %lld). Local commit time=%lld\n", publishedAt, (long long)remoteTime, (long long)GitCommitTimeStamp);
                fflush(stderr);
                
                // Signal update only when the remote release was published strictly
                // AFTER the commit this binary was built from.
                if (remoteTime != (time_t)-1 && remoteTime > GitCommitTimeStamp)
                {
                    hasUpdate = true;
                    fprintf(stderr, "[UpdateChecker] Remote release is newer based on timestamp!\n");
                    fflush(stderr);
                }
                else
                {
                    fprintf(stderr, "[UpdateChecker] Remote release is NOT newer based on timestamp.\n");
                    fflush(stderr);
                }
            }
            else if (GitTag[0] != '\0')
            {
                // No usable published_at comparison; fall back to tag string comparison.
                hasUpdate = (strcmp(latestTag, GitTag) != 0);
                fprintf(stderr, "[UpdateChecker] No valid published_at found or no local timestamp. Falling back to tag string diff. hasUpdate=%d\n", (int)hasUpdate);
                fflush(stderr);
            }
            else
            {
                fprintf(stderr, "[UpdateChecker] No local tag and no usable timestamp. Cannot determine update reliably. Assuming hasUpdate=false\n");
                fflush(stderr);
            }
        }
    }

    if (hasUpdate)
    {
        strncpy(s_latestTag, latestTag, sizeof(s_latestTag) - 1);
        s_latestTag[sizeof(s_latestTag) - 1] = '\0';
        SDL_SetAtomicInt(&s_hasUpdate, 1);
    }

    SDL_SetAtomicInt(&s_done, 1);
    return 0;
}

// ---------------------------------------------------------------------------
// UpdateChecker::start
// ---------------------------------------------------------------------------
void UpdateChecker::start()
{
    // Only run once per session
#if defined(_WIN32)
    if (s_started)
        return;
#else
    if (s_thread != nullptr)
        return;
#endif

    // GeneralsX @bugfix GitHubCopilot 07/05/2026 Accept clean release builds that
    // provide either an exact tag OR a valid commit timestamp (tag may be empty in
    // some packaged CI contexts even when the binary is a real release artifact).
    // Set env var GENERALS_FORCE_UPDATE_CHECK=1 to bypass release guards (for testing).
    const bool forceCheck = SDL_getenv("GENERALS_FORCE_UPDATE_CHECK") != nullptr;
    
    fprintf(stderr, "[UpdateChecker] start() called. GitTag='%s', GitCommitTimeStamp=%lld, GitUncommittedChanges=%d, forceCheck=%d\n", GitTag, (long long)GitCommitTimeStamp, (int)GitUncommittedChanges, (int)forceCheck);
    fflush(stderr);
    
    if (!forceCheck)
    {
        const bool hasTag = (GitTag[0] != '\0');
        const bool hasCommitTimestamp = (GitCommitTimeStamp > 0);
        if (!hasTag && !hasCommitTimestamp)
        {
            fprintf(stderr, "[UpdateChecker] start() aborted. hasTag=%d, hasCommitTimestamp=%d\n", (int)hasTag, (int)hasCommitTimestamp);
            fflush(stderr);
            return;
        }
    }

    // Respect the user opt-out setting
    if (TheGlobalData && !TheGlobalData->m_checkForUpdates)
    {
        fprintf(stderr, "[UpdateChecker] start() aborted. User opted out of updates in settings.\n");
        fflush(stderr);
        return;
    }

    SDL_SetAtomicInt(&s_done, 0);
    SDL_SetAtomicInt(&s_hasUpdate, 0);
    s_latestTag[0] = '\0';

    fprintf(stderr, "[UpdateChecker] Launching background thread to check %s\n", UpdateChecker::getReleasesUrl());
    fflush(stderr);

#if defined(_WIN32)
    // Detached std::thread; we communicate via the atomics (s_done / s_hasUpdate)
    // instead of joining.
    s_started = true;
    try
    {
        std::thread(threadFunc, nullptr).detach();
    }
    catch (...)
    {
        // Thread creation failed; fail silently
        SDL_SetAtomicInt(&s_done, 1);
    }
#else
    // Must be called once on the main thread before any curl handle is created.
    // curl_global_cleanup() is intentionally omitted: the game process handles
    // cleanup on exit and there is no safe single-owner shutdown hook here.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    s_thread = SDL_CreateThread(threadFunc, "UpdateChecker", nullptr);
    if (!s_thread)
    {
        // Thread creation failed; fail silently
        SDL_SetAtomicInt(&s_done, 1);
        return;
    }
    // Detach the thread so SDL frees its resources automatically when it exits.
    // We communicate via SDL_AtomicInt (s_done / s_hasUpdate) instead of joining.
    SDL_DetachThread(s_thread);
#endif
}

// ---------------------------------------------------------------------------
// UpdateChecker::poll
// ---------------------------------------------------------------------------
bool UpdateChecker::poll(const char** outLatestTag)
{
    if (SDL_GetAtomicInt(&s_done) == 0)
        return false; // still running

    if (SDL_GetAtomicInt(&s_hasUpdate) == 0)
        return false; // done but no update

    if (outLatestTag)
        *outLatestTag = s_latestTag;

    return true;
}

#endif // SAGE_UPDATE_CHECK
