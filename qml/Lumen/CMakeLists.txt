# Tokens and Motion are global design constants, so they are singletons.
#
# This MUST come before qt_add_qml_module: the module writes its qmldir at
# configure time, and a QT_QML_SINGLETON_TYPE set afterwards is simply not
# seen. The failure is silent and confusing -- the app builds, then every
# `Tokens.foo` evaluates to undefined at runtime.
set_source_files_properties(Tokens.qml Motion.qml Icons.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Lumen
    URI Lumen
    VERSION 1.0
    STATIC
    QML_FILES
        Tokens.qml
        Motion.qml
        Icons.qml
        Squircle.qml
        LumenIcon.qml
        LumenButton.qml
        LumenIconButton.qml
        LumenToolbar.qml
        LumenSidebar.qml
        LumenScrollBar.qml
        LumenSearchField.qml
        LumenSeparator.qml
        LumenSegmented.qml
)

target_link_libraries(Lumen PRIVATE Qt6::Quick)
