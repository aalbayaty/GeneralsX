# GeneralsX @build 22/07/2026 Retail-compatible import libraries for MinGW.
#
# The retail binkw32.dll / mss32.dll export stdcall names WITH the leading
# underscore (RAD convention: "_BinkClose@4", "_AIL_shutdown@0"). When the exe
# is linked against the stub DLLs' auto-generated import libraries, GNU ld
# records the import names WITHOUT the underscore ("BinkClose@4"), so the
# loader cannot resolve them against the retail DLLs at runtime
# ("procedure entry point BinkClose@4 could not be located").
#
# Generate import libraries whose import names match the retail exports, using
# the stubs' own .def files as the source of truth, and link the game against
# these instead of the stub targets. --no-leading-underscore makes the archive
# symbol exactly the def entry (i686 already carries the underscore in the
# stdcall-decorated name), so compiler references and DLL export names agree.

if(NOT MINGW)
    return()
endif()

set(_implib_dir "${CMAKE_BINARY_DIR}/retail-implibs")
file(MAKE_DIRECTORY "${_implib_dir}")

function(generalsx_make_retail_implib name dllname def_in out_var)
    # Normalize the stub def into "LIBRARY + EXPORTS + one decorated name per line".
    # bink_mingw.def uses alias lines ("_BinkClose@4=BinkClose@4") - keep the
    # export-name side only. miles.def already lists plain decorated names.
    file(STRINGS "${def_in}" _lines)
    set(_def_out "${_implib_dir}/${name}_retail.def")
    set(_content "LIBRARY ${dllname}\nEXPORTS\n")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _line)
        if(_line MATCHES "^(LIBRARY|EXPORTS)" OR _line STREQUAL "" OR _line MATCHES "^;")
            continue()
        endif()
        string(REGEX REPLACE "=.*$" "" _export_name "${_line}")
        string(STRIP "${_export_name}" _export_name)
        string(APPEND _content "${_export_name}\n")
    endforeach()
    file(WRITE "${_def_out}" "${_content}")

    set(_lib_out "${_implib_dir}/lib${name}_retail.a")
    execute_process(
        COMMAND "${CMAKE_DLLTOOL}" --no-leading-underscore
                -d "${_def_out}" -D "${dllname}" -l "${_lib_out}"
        RESULT_VARIABLE _dlltool_rc
        ERROR_VARIABLE _dlltool_err
    )
    if(NOT _dlltool_rc EQUAL 0)
        message(FATAL_ERROR "dlltool failed for ${dllname}: ${_dlltool_err}")
    endif()

    add_library(${name}_retail STATIC IMPORTED GLOBAL)
    set_target_properties(${name}_retail PROPERTIES IMPORTED_LOCATION "${_lib_out}")
    set(${out_var} ${name}_retail PARENT_SCOPE)
    message(STATUS "Generated retail import library for ${dllname}: ${_lib_out}")
endfunction()

generalsx_make_retail_implib(bink  binkw32.dll "${bink_SOURCE_DIR}/bink_mingw.def" BINK_RETAIL_TARGET)
generalsx_make_retail_implib(miles mss32.dll   "${miles_SOURCE_DIR}/miles.def"     MILES_RETAIL_TARGET)

# The stubs' public headers are still the compile-time interface.
target_include_directories(bink_retail  INTERFACE "${bink_SOURCE_DIR}")
target_include_directories(miles_retail INTERFACE "${miles_SOURCE_DIR}" "${miles_SOURCE_DIR}/mss")

# MSS_auto_cleanup is a stub-repo helper compiled from source (not a retail
# mss32.dll export) - carry it alongside the retail import library.
target_link_libraries(miles_retail INTERFACE milescleanup)
