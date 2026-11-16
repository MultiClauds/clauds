# SPDX-License-Identifier: MIT
find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Widgets Quick Network WebEngineCore WebEngineWidgets)

add_executable(clauds
    main.cpp
    Actions.cpp Actions.h
    ChatWindows.cpp ChatWindows.h
    LiveUsage.cpp LiveUsage.h
    LoginItem.cpp LoginItem.h
    Profiles.cpp Profiles.h
    Theme.cpp Theme.h
    Tray.cpp Tray.h
    resources.qrc
)
target_link_libraries(clauds PRIVATE clauds-core
    Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Network Qt6::WebEngineCore Qt6::WebEngineWidgets)
target_compile_definitions(clauds PRIVATE CLAUDS_VERSION="${PROJECT_VERSION}")

if(CLAUDS_WITH_KF6)
    find_package(ECM 6.0 NO_MODULE)
    if(ECM_FOUND)
        set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    endif()
    find_package(KF6 6.0 COMPONENTS StatusNotifierItem Notifications CoreAddons ColorScheme)
endif()
if(CLAUDS_WITH_KF6 AND KF6StatusNotifierItem_FOUND AND KF6Notifications_FOUND AND KF6CoreAddons_FOUND AND KF6ColorScheme_FOUND)
    message(STATUS "Clauds: building with KDE Frameworks 6")
    target_compile_definitions(clauds PRIVATE CLAUDS_HAVE_KF6)
    target_link_libraries(clauds PRIVATE KF6::StatusNotifierItem KF6::Notifications KF6::CoreAddons KF6::ColorScheme)
    install(FILES ../../packaging/clauds.notifyrc DESTINATION share/knotifications6)
else()
    message(STATUS "Clauds: KDE Frameworks 6 not found, building the plain Qt fallback (QSystemTrayIcon)")
endif()

include(GNUInstallDirs)
install(TARGETS clauds RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../../packaging/io.github.atterdag.clauds.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../../packaging/io.github.atterdag.clauds.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES icons/clauds.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES icons/clauds-tray-symbolic.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/status)
