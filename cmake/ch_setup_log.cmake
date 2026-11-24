# Carnage Heart: setup log file (launcher/ch_setup_log.inc, launcher/ch_setup_tee.py).
#
# The framework's setup wizard only shows the last error line when a step
# fails, and saves nothing. Instead of forking psxrecomp, this builds a copy of
# psxrecomp/host/psxrecomp_codegen_host.c with small insertions:
#   1. cli_tail_note(): every line the psxrecomp CLI prints goes to the log
#   2. psxrecomp_codegen_host_apply() is renamed; launcher/ch_setup_log.inc
#      (included at the end) provides a new one that wraps each wizard step so
#      it logs a header, its progress messages and its result, and adds
#      "Full log: <path>" to the error the wizard shows
#   3. Windows only: the rebuild console that runs after the wizard exits
#      sends its output through launcher/ch_setup_tee.py into the same file
# The log is <game folder>/setup-log.txt (setup-log.old.txt past 4 MB).
# If psxrecomp changes and a required anchor is gone, the build warns and
# keeps the stock file; nothing breaks.
#
# Usage (after psxrecomp_add_game_runtime):  ch_add_setup_log(psx-runtime)

function(ch_add_setup_log TGT)
    set(_orig "${PSXRECOMP_ROOT}/host/psxrecomp_codegen_host.c")
    if(NOT TARGET ${TGT} OR NOT EXISTS "${_orig}")
        return()
    endif()
    get_target_property(_srcs ${TGT} SOURCES)
    list(FIND _srcs "${_orig}" _idx)
    if(_idx EQUAL -1)
        message(STATUS "Carnage Heart setup log: ${TGT} has no setup host, skipped")
        return()
    endif()

    file(READ "${_orig}" _txt)

    # Required anchors (C side).
    set(_a_apply [==[void psxrecomp_codegen_host_apply(RecompLauncherCGameInfo* gi,]==])
    set(_a_tail [==[    const char* rec = line;
    int is_error_event = 0;
]==])
    foreach(_a IN ITEMS "${_a_apply}" "${_a_tail}")
        string(FIND "${_txt}" "${_a}" _pos)
        if(_pos EQUAL -1)
            message(WARNING
                "Carnage Heart setup log: psxrecomp_codegen_host.c changed (missing "
                "'${_a}'). Building without the setup log; update cmake/ch_setup_log.cmake.")
            return()
        endif()
    endforeach()

    string(PREPEND _txt
        "/* Carnage Heart setup log (cmake/ch_setup_log.cmake). */\nstatic void ch_setup_log_cli(const char* line);\n")
    string(REPLACE "${_a_apply}"
        [==[static void ch_stock_codegen_host_apply(RecompLauncherCGameInfo* gi,]==]
        _txt "${_txt}")
    string(REPLACE "${_a_tail}" "    ch_setup_log_cli(line);\n${_a_tail}" _txt "${_txt}")
    string(APPEND _txt "\n/* Carnage Heart setup log. */\n#include \"ch_setup_log.inc\"\n")

    # Optional anchors (Windows rebuild console). Missing = console output is
    # not logged, the rest still works.
    set(_b_tc [==["echo Ensuring toolchain...\r\n"]==])
    set(_b_cli [==["\"%%PYTHON%%\" \"%%CLI%%\" ]==])
    set(_b_tc_fail [==["  echo RETCOMM_TOOLCHAIN_DIR / pass --toolchain-zip on rebuild.\r\n"]==])
    set(_b_fail [==["  echo Build failed. Fix the errors above, then rebuild manually.\r\n"]==])
    set(_console_ok TRUE)
    foreach(_b IN ITEMS "${_b_tc}" "${_b_cli}" "${_b_tc_fail}" "${_b_fail}")
        string(FIND "${_txt}" "${_b}" _pos)
        if(_pos EQUAL -1)
            set(_console_ok FALSE)
        endif()
    endforeach()
    if(_console_ok)
        string(REPLACE "${_b_tc}" [==["set \"TEE=\"\r\n"
            "if exist \"%%ROOT%%\\launcher\\ch_setup_tee.py\" set TEE=\"%%PYTHON%%\" \"%%ROOT%%\\launcher\\ch_setup_tee.py\" \"%%ROOT%%\\setup-log.txt\"\r\n"
            "echo Ensuring toolchain...\r\n"]==] _txt "${_txt}")
        string(REPLACE "${_b_cli}" [==["%%TEE%% \"%%PYTHON%%\" \"%%CLI%%\" ]==] _txt "${_txt}")
        set(_log_note [==["  echo A full log was saved to setup-log.txt in the game folder.\r\n"]==])
        string(REPLACE "${_b_tc_fail}" "${_b_tc_fail}\n            ${_log_note}" _txt "${_txt}")
        string(REPLACE "${_b_fail}" "${_b_fail}\n            ${_log_note}" _txt "${_txt}")
    else()
        message(WARNING "Carnage Heart setup log: the Windows rebuild console text "
                        "changed; its output will not be logged (wizard steps still are).")
    endif()

    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/ch_setup_log")
    set(_out "${_out_dir}/psxrecomp_codegen_host.c")
    file(WRITE "${_out_dir}/psxrecomp_codegen_host.c.new" "${_txt}")
    configure_file("${_out_dir}/psxrecomp_codegen_host.c.new" "${_out}" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_orig}")

    list(REMOVE_AT _srcs ${_idx})
    list(APPEND _srcs "${_out}")
    set_property(TARGET ${TGT} PROPERTY SOURCES ${_srcs})
    # The copy's sibling headers stay in psxrecomp/host; the .inc is in launcher/.
    target_include_directories(${TGT} PRIVATE
        "${PSXRECOMP_ROOT}/host" "${CMAKE_CURRENT_SOURCE_DIR}/launcher")

    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _ch_version LIMIT_COUNT 1)
    string(STRIP "${_ch_version}" _ch_version)
    if(DEFINED PSX_GAME_VERSION AND NOT PSX_GAME_VERSION STREQUAL "")
        set(_ch_version "${PSX_GAME_VERSION}")
    endif()
    if(_ch_version STREQUAL "")
        set(_ch_version "dev")
    endif()
    set_source_files_properties("${_out}" TARGET_DIRECTORY ${TGT}
        PROPERTIES COMPILE_DEFINITIONS "CH_VERSION=\"${_ch_version}\"")
    message(STATUS "Carnage Heart setup log: on (setup-log.txt)")
endfunction()
