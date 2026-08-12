# GeneralsX @feature fbraz 03/05/2026
# Deterministic cross-platform math library integration (Phase 4)
# GameMath: fdlibm-based deterministic math functions for bit-exact replay validation
#
# Strategy:
# - Enable deterministic math via FetchContent (not on VC6, which uses x87 asm)
# - Define USE_DETERMINISTIC_MATH compile flag when enabled
# - Wrappers in wwmath.h conditionally dispatch to GameMath (deterministic) or CRT (fast fallback)
#
# Reference: Okladnoj et al., PR #2670, TheSuperHackers/GeneralsGameCode
# https://github.com/TheSuperHackers/GeneralsGameCode/pull/2670
#
# Note: GameMath source location is configurable via SAGE_GAMEMATH_GIT_REPO
# Default: TheSuperHackers fork with deterministic math integration
#
# Upstream reference: fdlibm (Berkeley math library) provides platform-independent
# implementations of standard math functions (sin, cos, sqrt, atan2, etc.) that produce
# identical results on all architectures when compiled with the same precision flags.

# Enable deterministic math only for non-VC6 builds (VC6 uses native x87 asm)
# Note: Currently defaults to OFF until GameMath is available as a proper library/submodule
if(NOT IS_VS6_BUILD)
    option(SAGE_USE_DETERMINISTIC_MATH "Use fdlibm-based deterministic math for cross-platform replay validation" ON)
else()
    # VC6 uses native x87 inline asm; deterministic mode not applicable
    set(SAGE_USE_DETERMINISTIC_MATH OFF)
endif()

if(SAGE_USE_DETERMINISTIC_MATH)
    message(STATUS "Configuring GameMath (fdlibm-based deterministic math)...")

    include(FetchContent)

    # FORCE is required to guarantee cross-platform bit-exact determinism.
    # Intrinsics would use platform-specific SIMD, breaking CRC parity between architectures.
    set(GM_ENABLE_INTRINSICS OFF CACHE BOOL "Disable intrinsics for cross-arch determinism" FORCE)
    set(GM_ENABLE_TESTS OFF CACHE BOOL "Disable GameMath tests" FORCE)
    set(gamemath_SHARED_LIBS OFF CACHE BOOL "Force GameMath as static lib" FORCE)

    FetchContent_Declare(
        gamemath
        GIT_REPOSITORY https://github.com/TheSuperHackers/GameMath.git
        GIT_TAG        59f7ccd494f7e7c916a784ac26ef266f9f09d78d
    )

    # Make GameMath available (FetchContent_MakeAvailable is idempotent)
    FetchContent_MakeAvailable(gamemath)

    # Ensure GameMath includes are available to ALL targets
    # to prevent one-definition-rule violations and ensure USE_DETERMINISTIC_MATH activates consistently.
    include_directories(${gamemath_SOURCE_DIR}/include)

    # GeneralsX @bugfix 12/08/2026 Actually activate the deterministic dispatch on Windows.
    # USE_DETERMINISTIC_MATH was never defined on any target, so the WWMath trig/sqrt
    # gateways silently fell back to the platform CRT everywhere. On glibc/Apple libm the
    # native float trig is correctly rounded, so mac/linux agree with each other (and with
    # fdlibm) without the define — and upstream ships those builds without it, so defining
    # it there would break CRC parity with upstream binaries. msvcrt's x87 trig is NOT
    # correctly rounded and desyncs cross-platform play (tunnel-evacuate desync, frame-level
    # CRC fork), so Windows builds must dispatch to fdlibm to match the other platforms.
    if(WIN32)
        target_compile_definitions(core_config INTERFACE USE_DETERMINISTIC_MATH)
        target_link_libraries(core_config INTERFACE gamemath)
    endif()

    message(STATUS "GameMath deterministic math enabled (fdlibm backend)")
    message(STATUS "  Math operations will be bit-exact across platforms")
    message(STATUS "  Performance: Slightly slower than CRT but guarantees replay determinism")

else()
    message(STATUS "Deterministic math disabled (SAGE_USE_DETERMINISTIC_MATH=OFF)")
    message(STATUS "  Math operations will use platform-native CRT/x87")
    message(STATUS "  Note: Replays may differ between platforms due to FMA/rounding differences")
endif()
