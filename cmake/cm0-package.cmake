# Packaging and install layout for the CardputerZero Debian package.

include(GNUInstallDirs)

set(APP_DISPLAY_NAME "Pocket Alert" CACHE STRING "Name shown in the launcher")
set(APP_PACKAGE_NAME "pocketalert-cardputer" CACHE STRING "Debian package name")
set(APP_DEBIAN_REVISION "1" CACHE STRING "Debian package revision")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
set(APP_MAINTAINER "Pocket Alert <support@pocketalert.app>" CACHE STRING "Debian package maintainer")
set(APP_PACKAGE_DESCRIPTION "Pocket Alert client: read, send and manage push alerts" CACHE STRING "Debian package summary")

set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file("${CMAKE_CURRENT_LIST_DIR}/templates/app.desktop.in" "${APP_GENERATED_DIR}/pocketalert.desktop" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/templates/notifyd.service.in"
               "${APP_GENERATED_DIR}/pocketalert-notifyd.service" @ONLY)
foreach(script postinst prerm)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/templates/${script}.in" "${APP_GENERATED_DIR}/${script}" @ONLY
                   FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()

install(TARGETS ${PROJECT_NAME} pocketalert-notifyd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/audio" PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts" PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/i18n" FILES_MATCHING PATTERN "*.json")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/pocketalert.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images")
install(FILES "${APP_GENERATED_DIR}/pocketalert.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications")
# Literal path: GNUInstallDirs would put LIBDIR under the multiarch triplet.
install(FILES "${APP_GENERATED_DIR}/pocketalert-notifyd.service" DESTINATION "lib/systemd/user")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE.txt" "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Geist-OFL.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansCJK-OFL.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "${APP_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "Pocket Alert")
set(CPACK_PACKAGE_CONTACT "${APP_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${APP_PACKAGE_NAME}_${PROJECT_VERSION}-${APP_DEBIAN_REVISION}_${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_NAME "${APP_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${APP_DEBIAN_REVISION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://pocketalert.app")
# Library dependencies are computed by dpkg-shlibdeps when packaging on Debian;
# runtime tools that are exec'd rather than linked are listed by hand.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON CACHE BOOL "Run dpkg-shlibdeps")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "ca-certificates, alsa-utils")
# pw-play reaches the speaker through the session's PipeWire, like the launcher does.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-bin")
# The bundled CJK subset only covers the UI; the full font also draws CJK in messages.
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "fonts-noto-cjk")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${APP_GENERATED_DIR}/postinst;${APP_GENERATED_DIR}/prerm")

include(CPack)
