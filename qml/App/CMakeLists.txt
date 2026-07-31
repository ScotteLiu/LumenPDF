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
        DemoRunner.qml
        OcrPanel.qml
        EmptyState.qml
        PrintSheet.qml
        PasswordSheet.qml
        SettingsSheet.qml
        UpdateBanner.qml
        NumberField.qml
        ToggleRow.qml
)

target_link_libraries(App PRIVATE Qt6::Quick)


