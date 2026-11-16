// SPDX-License-Identifier: MIT
// The Clauds panel. Colors come from the system palette and the KDE color scheme (theme.*),
// so it follows Breeze, Quantum Light / Quantum Dark, or any other scheme.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: 380
    height: content.implicitHeight + 24
    color: pal.window
    radius: 10
    border.color: theme.cardBorder

    SystemPalette { id: pal; colorGroup: SystemPalette.Active }

    function isPrimary(a) { return store.sideBySide ? a.isRunning : a.isActive }
    readonly property var primary: store.accounts.filter(a => isPrimary(a))
    readonly property var others: store.accounts.filter(a => !isPrimary(a))

    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 8

        // Auto-switch note
        Label {
            visible: store.autoSwitchNote !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: store.autoSwitchNote
            font.bold: true
        }

        // No accounts yet
        ColumnLayout {
            visible: store.accounts.length === 0
            Layout.fillWidth: true
            spacing: 6
            Label { text: "No accounts yet"; font.bold: true; font.pointSize: Qt.application.font.pointSize * 1.15 }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: "Save the login that the Claude app has now. Then add your other accounts."
            }
            Button { text: "Save Current Claude Login as Account…"; onClicked: actions.adoptCurrent() }
        }

        Label {
            visible: root.primary.length > 0
            text: store.sideBySide ? "OPEN ACCOUNTS" : "CURRENT ACCOUNT"
            font.pointSize: Qt.application.font.pointSize * 0.8
            font.bold: true
            font.letterSpacing: 0.6
            opacity: 0.65
        }
        CardGroup { visible: root.primary.length > 0; model: root.primary }

        Label {
            visible: root.others.length > 0
            text: "SAVED ACCOUNTS"
            font.pointSize: Qt.application.font.pointSize * 0.8
            font.bold: true
            font.letterSpacing: 0.6
            opacity: 0.65
        }
        CardGroup { visible: root.others.length > 0; model: root.others }

        ColumnLayout {
            visible: store.accounts.length > 0
            Layout.fillWidth: true
            spacing: 2
            enabled: !store.isSwitching
            Switch {
                Layout.fillWidth: true
                text: "Side by Side (no restart)"
                checked: store.sideBySide
                onToggled: { const want = checked; checked = Qt.binding(() => store.sideBySide); actions.setSideBySide(want) }
            }
            Switch {
                Layout.fillWidth: true
                text: store.sideBySide ? "Share Code Sessions (risky in Side by Side)" : "Share Code Sessions"
                checked: store.sessionsAreShared
                onToggled: { const want = checked; checked = Qt.binding(() => store.sessionsAreShared); actions.setShareSessions(want) }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: theme.cardBorder }

        RowLayout {
            Layout.fillWidth: true
            ToolButton {
                visible: store.accounts.length > 0
                text: "Add Account"
                icon.name: "list-add"
                display: AbstractButton.TextBesideIcon
                onClicked: actions.addAccount()
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { visible: store.isSwitching; running: visible; implicitWidth: 22; implicitHeight: 22 }
            ToolButton {
                id: appMenuButton
                icon.name: "application-menu"
                text: "More"
                display: AbstractButton.IconOnly
                ToolTip.visible: hovered
                ToolTip.text: "More"
                onClicked: appMenu.popup(appMenuButton, 0, appMenuButton.height)
            }
        }
    }

    Menu {
        id: appMenu
        MenuItem {
            visible: store.sessionsAreShared
            height: visible ? implicitHeight : 0
            text: "Share Code Sessions Now (Restarts Claude)"
            enabled: !store.isSwitching
            onTriggered: actions.syncSessions()
        }
        MenuItem {
            text: "Start at Login"
            checkable: true
            checked: actions.startsAtLogin
            onTriggered: actions.setStartsAtLogin(!actions.startsAtLogin)
        }
        Menu {
            title: "Auto-Switch Near a Limit"
            enabled: !store.sideBySide && store.accounts.length >= 2
            Repeater {
                model: [{ key: "off", title: "Off" }, { key: "ask", title: "Ask Me First" }, { key: "auto", title: "Switch Automatically" }]
                MenuItem {
                    text: modelData.title
                    checkable: true
                    checked: store.autoSwitch === modelData.key
                    onTriggered: actions.setAutoSwitch(modelData.key)
                }
            }
        }
        MenuSeparator {}
        MenuItem { text: "Show Saved Accounts in File Manager"; onTriggered: actions.showSavedAccounts() }
        MenuItem { text: "Open Claude"; onTriggered: actions.openClaude() }
        MenuSeparator {}
        MenuItem { text: "About Clauds"; onTriggered: actions.about() }
        MenuItem { text: "Quit Clauds"; onTriggered: actions.quit() }
    }

    component CardGroup: Rectangle {
        property alias model: rows.model
        Layout.fillWidth: true
        implicitHeight: column.implicitHeight
        radius: 8
        color: theme.cardBackground
        border.color: theme.cardBorder
        Column {
            id: column
            width: parent.width
            Repeater {
                id: rows
                delegate: Column {
                    width: column.width
                    Rectangle {
                        visible: index > 0
                        x: 56; width: parent.width - 56; height: 1
                        color: theme.cardBorder
                    }
                    AccountCard { width: parent.width; account: modelData }
                }
            }
        }
    }
}
