import QtQuick
import Lumen
import Lumen.Backend

// A new version is available.
//
// A strip under the toolbar rather than a dialog: an update is never urgent
// enough to interrupt what someone opened the application to do, and a modal
// that appears on launch is the reason people turn update checks off.
//
// It also dismisses permanently for that version, because being told the same
// thing on every launch is how a notice becomes noise.
Item {
    id: root

    property string dismissedVersion: ""

    readonly property bool shouldShow:
        Updates.updateAvailable && Updates.latestVersion !== dismissedVersion

    signal toastRequested(string message)
    signal errorRequested(string message)

    implicitHeight: Tokens.toolbarHeight - 6
    height: shouldShow || Updates.downloading || Updates.downloadedFile.length > 0
            ? implicitHeight : 0
    visible: height > 0
    clip: true

    Behavior on height {
        NumberAnimation { duration: Motion.normal; easing.type: Easing.OutCubic }
    }

    Connections {
        target: Updates
        function onFailed(reason) { root.errorRequested(reason) }
        function onDownloadFinished(path) {
            root.toastRequested(qsTr("Downloaded and verified"));
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tokens.accentSubtle

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Tokens.separator
        }
    }

    // Download progress, drawn as a fill behind the text rather than as a
    // separate bar -- one element instead of two saying the same thing.
    Rectangle {
        height: parent.height
        width: parent.width * Updates.downloadProgress
        color: Tokens.accentSubtle
        visible: Updates.downloading
        Behavior on width { NumberAnimation { duration: Motion.fast } }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Tokens.space4
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.space3

        LumenIcon {
            anchors.verticalCenter: parent.verticalCenter
            path: Icons.download
            size: 16
            color: Tokens.accent
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            font.family: Tokens.fontFamily
            font.pixelSize: Tokens.textSmall
            color: Tokens.textPrimary
            text: {
                if (Updates.downloadedFile.length > 0)
                    return qsTr("LumenPDF %1 is downloaded and its checksum checks out.")
                             .arg(Updates.latestVersion);
                if (Updates.downloading)
                    return qsTr("Downloading %1… %2%")
                             .arg(Updates.latestVersion)
                             .arg(Math.round(Updates.downloadProgress * 100));
                return qsTr("LumenPDF %1 is available.").arg(Updates.latestVersion);
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Tokens.space3
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.space2

        LumenButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("What's new")
            variant: LumenButton.Variant.Plain
            visible: !Updates.downloading && Updates.downloadedFile.length === 0
            onClicked: Updates.openReleasePage()
        }

        LumenButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Download")
            variant: LumenButton.Variant.Primary
            visible: !Updates.downloading && Updates.downloadedFile.length === 0
            onClicked: Updates.download()
        }

        LumenButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Cancel")
            variant: LumenButton.Variant.Plain
            visible: Updates.downloading
            onClicked: Updates.cancelDownload()
        }

        // Says "quit and install" because that is what happens: an installer
        // cannot replace files this process has open.
        LumenButton {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Quit and install")
            variant: LumenButton.Variant.Primary
            visible: Updates.downloadedFile.length > 0
            onClicked: {
                if (!Updates.runInstaller())
                    root.errorRequested(qsTr("The installer could not be started."));
            }
        }

        LumenIconButton {
            anchors.verticalCenter: parent.verticalCenter
            iconPath: Icons.close
            tooltip: qsTr("Not now")
            visible: !Updates.downloading
            onClicked: root.dismissedVersion = Updates.latestVersion
        }
    }
}
