#!/usr/bin/env bash
# GeneralsX @build 22/07/2026
# Windows parity release: keep the fork's Windows (MinGW) build in lockstep with the
# upstream GeneralsX releases.
#
# It pulls the latest release from the ORIGINAL GeneralsX (fbraz3/GeneralsX), rebuilds
# the Windows binary from those exact sources plus the Windows build fixes, and
# publishes a matching release on this fork. Idempotent: if the fork already has a
# Windows asset for that release tag, it exits without building.
#
# Usage:
#   ./scripts/release/windows-parity-release.sh [TAG]
#     TAG   Release tag to build (default: latest upstream release).
#
# Requirements: git, docker, gh (authenticated with write access to FORK_REPO), zip.
#
# Environment overrides:
#   UPSTREAM_REPO   Original GeneralsX repo      (default: fbraz3/GeneralsX)
#   FORK_REPO       Fork to publish releases to  (default: aalbayaty/GeneralsX)
#   FIXES_REF       Branch on FORK_REPO carrying the Windows build fixes to merge onto
#                   the upstream release. Its merge-base with each upstream tag is the
#                   release it was built from, so only the Windows-fix delta is applied.
#                   Set empty once the fixes are merged upstream. (default: main)
#   PRESET          CMake preset to build        (default: mingw-w64-i686)
#   ASSET_NAME      Release asset filename       (default: GeneralsXZH-windows-x86.zip)

set -eo pipefail

UPSTREAM_REPO="${UPSTREAM_REPO:-fbraz3/GeneralsX}"
FORK_REPO="${FORK_REPO:-aalbayaty/GeneralsX}"
FIXES_REF="${FIXES_REF-main}"
PRESET="${PRESET:-mingw-w64-i686}"
ASSET_NAME="${ASSET_NAME:-GeneralsXZH-windows-x86.zip}"

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

echo "🔄 Fetching tags from the original GeneralsX (${UPSTREAM_REPO})..."
git fetch --quiet --tags "https://github.com/${UPSTREAM_REPO}.git"

# ---- Resolve the target release tag -----------------------------------------
TAG="${1:-}"
if [[ -z "$TAG" ]]; then
	echo "🔎 Resolving latest upstream release..."
	TAG="$(gh release view --repo "$UPSTREAM_REPO" --json tagName -q .tagName)"
fi
echo "🎯 Target release: ${TAG}"

if ! git rev-parse -q --verify "refs/tags/${TAG}^{commit}" >/dev/null; then
	echo "❌ Tag '${TAG}' not found in ${UPSTREAM_REPO} after fetch. Aborting." >&2
	exit 1
fi

# ---- Idempotency: skip if the fork already published this Windows build ------
if gh release view "$TAG" --repo "$FORK_REPO" --json assets \
		-q '.assets[].name' 2>/dev/null | grep -qx "$ASSET_NAME"; then
	echo "✅ ${FORK_REPO} release ${TAG} already carries ${ASSET_NAME}; nothing to do."
	exit 0
fi
echo "🆕 ${FORK_REPO} has no Windows build for ${TAG} yet — building."

# ---- Fetch the Windows build fixes ------------------------------------------
FIXES_SHA=""
if [[ -n "$FIXES_REF" ]]; then
	echo "🔧 Fetching Windows build fixes (${FORK_REPO}:${FIXES_REF})..."
	git fetch --quiet "https://github.com/${FORK_REPO}.git" "$FIXES_REF"
	FIXES_SHA="$(git rev-parse FETCH_HEAD)"
fi

# ---- Build in an isolated worktree: upstream tag (+ fixes) -------------------
WORKTREE="$(mktemp -d "${TMPDIR:-/tmp}/genx-release-XXXXXX")"
BUILD_BRANCH="release-build/${TAG}"
git branch -D "$BUILD_BRANCH" >/dev/null 2>&1 || true

cleanup() {
	git worktree remove --force "$WORKTREE" >/dev/null 2>&1 || true
	git branch -D "$BUILD_BRANCH" >/dev/null 2>&1 || true
	rm -rf "$WORKTREE" 2>/dev/null || true
}
trap cleanup EXIT

echo "🌱 Creating build worktree at ${TAG}..."
git worktree add --quiet -b "$BUILD_BRANCH" "$WORKTREE" "refs/tags/${TAG}"

pushd "$WORKTREE" >/dev/null

if [[ -n "$FIXES_SHA" ]]; then
	echo "🔧 Merging Windows build fixes (${FIXES_SHA:0:9}) onto ${TAG}..."
	if ! git -c user.name="GeneralsX Release Bot" -c user.email="release@localhost" \
			merge --no-edit "$FIXES_SHA"; then
		echo "❌ Merging the Windows fixes onto ${TAG} conflicted." >&2
		echo "   The upstream sources have diverged from the fixes branch; rebase" >&2
		echo "   '${FIXES_REF}' onto ${TAG} manually, then re-run." >&2
		exit 2
	fi
fi

echo "🐳 Building Windows binary (preset ${PRESET})..."
./scripts/build/linux/docker-build-mingw-zh.sh "$PRESET"

EXE="build/${PRESET}/GeneralsMD/generalszh.exe"
if [[ ! -f "$EXE" ]]; then
	echo "❌ Build did not produce ${EXE}." >&2
	exit 3
fi

echo "📦 Packaging ${ASSET_NAME}..."
rm -f "$ASSET_NAME" generalszh.exe
cp "$EXE" generalszh.exe
zip -q "$ASSET_NAME" generalszh.exe
SHA="$(sha256sum "$ASSET_NAME" | cut -d' ' -f1)"
ASSET_PATH="${WORKTREE}/${ASSET_NAME}"

popd >/dev/null

# ---- Publish / update the matching fork release -----------------------------
read -r -d '' NOTES <<EOF || true
Windows x86 build of **GeneralsX Zero Hour ${TAG}**, in parity with the upstream [${UPSTREAM_REPO} ${TAG}](https://github.com/${UPSTREAM_REPO}/releases/tag/${TAG}) release.

Cross-compiled with MinGW-w64 (SSE2 deterministic math) from the upstream ${TAG} sources${FIXES_REF:+ plus the Windows build fixes (\`${FIXES_REF}\`)}.

## Install
1. Install **Command & Conquer Generals: Zero Hour 1.04** (retail assets required).
2. Drop \`generalszh.exe\` into the Zero Hour install directory (keep the retail \`mss32.dll\`/\`binkw32.dll\`).
3. Play over LAN, or Tailscale/ZeroTier for internet play.

Compatible with **GeneralsX ${TAG}** Mac/Linux clients. Not compatible with retail 1.04 or TheSuperHackers Windows clients.

SHA-256 (\`${ASSET_NAME}\`): \`${SHA}\`
EOF

if gh release view "$TAG" --repo "$FORK_REPO" >/dev/null 2>&1; then
	echo "⬆️  Updating existing ${FORK_REPO} release ${TAG}..."
	gh release upload "$TAG" "$ASSET_PATH" --repo "$FORK_REPO" --clobber
	gh release edit "$TAG" --repo "$FORK_REPO" --notes "$NOTES"
else
	echo "🚀 Creating ${FORK_REPO} release ${TAG}..."
	gh release create "$TAG" "$ASSET_PATH" --repo "$FORK_REPO" \
		--title "GeneralsX ${TAG} — Windows (MinGW)" --notes "$NOTES"
fi

echo "✅ Done. ${FORK_REPO} release ${TAG} now carries ${ASSET_NAME} (sha256 ${SHA})."
