qt_add_qml_module(App
    URI App
    VERSION 1.0
    STATIC
    QML_FILES
        Main.qml
        PageView.qml
        ThumbnailRail.qml
        OutlinePanel.qml
        SearchPanel.qml
        SelectionToolbar.qml
        SignaturePad.qml
        BenchmarkRunner.qml
        TextEditOverlay.qml
        EmptyState.qml
)

target_link_libraries(App PRIVATE Qt6::Quick)

