# Carnage Heart launcher menu (launcher/ch_menu.inc) and updater
# (launcher/ch_update.cpp).
#
# recomp-ui draws the launcher in launcher_imgui.cpp and has no place for a
# title's own menu. Instead of forking the submodule, this builds a copy of
# that file with four small insertions:
#   0. #include "ch_update.h" at the top (outside any namespace)
#   1. #include "ch_menu.inc" just before draw_ui()
#   2. at the top of draw_ui()'s header: let ch_menu draw the views it owns
#   3. after draw_footer(): close that if, so the stock modals (setup wizard,
#      BIOS prompts, netplay dialogs) still draw on top of the menu
# If recomp-ui changes and an anchor is gone, the build says so and keeps the
# stock launcher; nothing breaks.
#
# Usage (after psxrecomp_add_game_runtime):  ch_add_launcher_menu(psx-runtime)

function(ch_add_launcher_menu TGT)
    set(_rui_imgui "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src/common/backends/imgui")
    set(_orig "${_rui_imgui}/launcher_imgui.cpp")
    if(NOT TARGET ${TGT} OR NOT EXISTS "${_orig}")
        message(STATUS "Carnage Heart menu: no recomp-ui launcher, stock launcher kept")
        return()
    endif()
    get_target_property(_srcs ${TGT} SOURCES)
    list(FIND _srcs "${_orig}" _idx)
    if(_idx EQUAL -1)
        message(STATUS "Carnage Heart menu: ${TGT} does not build launcher_imgui.cpp, stock launcher kept")
        return()
    endif()

    file(READ "${_orig}" _txt)
    set(_a1 "void draw_ui(LauncherModel* m, const LauncherTheme& th, int logical_w, int logical_h) {\n")
    set(_a2 "    // ---- Marquee header:")
    set(_a3 "    draw_footer(m, th, footer_h);\n")
    foreach(_a IN ITEMS "${_a1}" "${_a2}" "${_a3}")
        string(FIND "${_txt}" "${_a}" _pos)
        if(_pos EQUAL -1)
            message(WARNING
                "Carnage Heart menu: recomp-ui's launcher_imgui.cpp changed (missing "
                "'${_a}'). Building the stock launcher; update cmake/ch_launcher_menu.cmake.")
            return()
        endif()
    endforeach()

    # The menu lands inside the file's anonymous namespace; a C++ header
    # included there would declare its functions in it, so ch_update.h and
    # ch_online.h go at the very top.
    string(PREPEND _txt "#include \"ch_update.h\"   /* Carnage Heart launcher menu */\n")
    string(PREPEND _txt "#include \"ch_online.h\"   /* Carnage Heart online campaign */\n")
    string(PREPEND _txt "#include \"ch_apk_share.h\"   /* Carnage Heart: app to tablet */\n#include <ctime>\n#include <vector>\n")
    string(REPLACE "${_a1}"
        "/* Carnage Heart launcher menu (cmake/ch_launcher_menu.cmake). */\n#include \"ch_menu.inc\"\n\n${_a1}"
        _txt "${_txt}")
    string(REPLACE "${_a2}"
        "    const bool ch_title_menu = ch_menu::owns_view(m);\n    if (ch_title_menu) ch_menu::draw(m, th);\n    if (!ch_title_menu) {   /* stock header, page body and footer */\n${_a2}"
        _txt "${_txt}")
    string(REPLACE "${_a3}"
        "${_a3}    }   /* !ch_title_menu */\n"
        _txt "${_txt}")

    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/ch_launcher")
    set(_out "${_out_dir}/launcher_imgui.cpp")
    # Write only on change, so a reconfigure does not rebuild the launcher.
    file(WRITE "${_out_dir}/launcher_imgui.cpp.new" "${_txt}")
    configure_file("${_out_dir}/launcher_imgui.cpp.new" "${_out}" COPYONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_orig}")

    list(REMOVE_AT _srcs ${_idx})
    list(APPEND _srcs "${_out}")
    set_property(TARGET ${TGT} PROPERTY SOURCES ${_srcs})
    # The copy's sibling headers stay in recomp-ui; the menu is in launcher/.
    target_include_directories(${TGT} PRIVATE
        "${_rui_imgui}" "${CMAKE_CURRENT_SOURCE_DIR}/launcher")

    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _ch_version LIMIT_COUNT 1)
    string(STRIP "${_ch_version}" _ch_version)
    if(DEFINED PSX_GAME_VERSION AND NOT PSX_GAME_VERSION STREQUAL "")
        set(_ch_version "${PSX_GAME_VERSION}")
    endif()
    if(_ch_version STREQUAL "")
        set(_ch_version "dev")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    target_sources(${TGT} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/launcher/ch_update.cpp")
    # Only this file carries the version, so a version bump rebuilds one TU.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/launcher/ch_update.cpp"
        PROPERTIES COMPILE_DEFINITIONS "CH_VERSION=\"${_ch_version}\"")
    # Online campaign games (launcher/ch_online.cpp): on when the game has
    # turn files (CH_REMOTE_BATTLE). CH_TURN_RELAY_URL builds in the relay
    # address (the project's own relay by default, set up 2026-11-24);
    # online-turns/relay.txt beside the game or CH_TURN_RELAY overrides it.
    set(CH_TURN_RELAY_URL "https://ch-turns.surreal64.workers.dev" CACHE STRING "Turn relay address built into the launcher (relay/README.md)")
    # SEND APP TO TABLET (launcher/ch_apk_share.cpp): a small Wi-Fi web
    # server for CarnageHeart.apk and a QR code (vendored Nayuki qrcodegen,
    # MIT, launcher/third_party/qrcodegen).
    target_sources(${TGT} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/launcher/ch_apk_share.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/launcher/third_party/qrcodegen/qrcodegen.c")
    if(WIN32)
        target_link_libraries(${TGT} PRIVATE ws2_32 shell32)
    endif()
    set(_ch_online "${CMAKE_CURRENT_SOURCE_DIR}/launcher/ch_online.cpp")
    target_sources(${TGT} PRIVATE "${_ch_online}")
    set(_ch_online_defs "CH_TURN_RELAY_DEFAULT=\"${CH_TURN_RELAY_URL}\"")
    if(CH_REMOTE_BATTLE)
        list(APPEND _ch_online_defs "CH_ONLINE_CAMPAIGN=1")
    endif()
    set_source_files_properties("${_ch_online}" PROPERTIES COMPILE_DEFINITIONS "${_ch_online_defs}")
    message(STATUS "Carnage Heart menu: on (version ${_ch_version})")
endfunction()
