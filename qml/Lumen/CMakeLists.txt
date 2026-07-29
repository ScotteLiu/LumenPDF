qt_add_qml_module(Lumen
    URI Lumen
    VERSION 1.0
    STATIC
    QML_FILES
        Tokens.qml
        Motion.qml
        Squircle.qml
        LumenButton.qml
        LumenIconButton.qml
        LumenToolbar.qml
        LumenSidebar.qml
        LumenScrollBar.qml
        LumenSearchField.qml
        LumenSeparator.qml
)

# Tokens and Motion are global design constants, so they are singletons.
set_source_files_properties(Tokens.qml Motion.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

target_link_libraries(Lumen PRIVATE Qt6::Quick)
