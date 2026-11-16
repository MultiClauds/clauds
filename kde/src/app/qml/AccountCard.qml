// SPDX-License-Identifier: MIT
// One account: avatar, name, email, and one meter per plan limit. A click selects the account.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: card
    property var account
    implicitHeight: body.implicitHeight + 20

    SystemPalette { id: pal; colorGroup: SystemPalette.Active }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 6
        color: pal.highlight
        opacity: hover.hovered && !store.isSwitching ? 0.12 : 0
    }
    HoverHandler { id: hover }
    TapHandler {
        enabled: !store.isSwitching
        onTapped: actions.select(card.account.id)
    }

    RowLayout {
        id: body
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                spacing: 10
                Rectangle {
                    implicitWidth: 36; implicitHeight: 36
                    radius: 8
                    color: theme.avatarColor(card.account.id)
                    Label {
                        anchors.centerIn: parent
                        text: (card.account.name.trim().charAt(0) || "?").toUpperCase()
                        color: "white"
                        font.bold: true
                        font.pointSize: Qt.application.font.pointSize * 1.35
                    }
                    Rectangle {
                        visible: store.sideBySide && card.account.isRunning
                        width: 10; height: 10; radius: 5
                        anchors { right: parent.right; bottom: parent.bottom; margins: -3 }
                        color: theme.active
                        border.color: theme.cardBackground; border.width: 2
                    }
                }
                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label { text: card.account.name; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        visible: card.account.email !== "" && card.account.email !== card.account.name
                        text: card.account.email
                        opacity: 0.65
                        font.pointSize: Qt.application.font.pointSize * 0.9
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            // Meters
            RowLayout {
                visible: card.account.limits.length > 0
                spacing: 12
                Layout.fillWidth: true
                Repeater {
                    model: card.account.limits
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 3
                        RowLayout {
                            Label { text: modelData.label; font.pointSize: Qt.application.font.pointSize * 0.85; opacity: 0.75 }
                            Item { Layout.fillWidth: true }
                            Label { text: modelData.percent + "%"; font.pointSize: Qt.application.font.pointSize * 0.85; font.bold: true; color: theme.levelColor(modelData.level) }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 4
                            radius: 2
                            color: theme.cardBorder
                            Rectangle {
                                width: parent.width * Math.min(100, modelData.percent) / 100
                                height: parent.height
                                radius: 2
                                color: theme.levelColor(modelData.level)
                            }
                        }
                        Label { text: modelData.resetText; font.pointSize: Qt.application.font.pointSize * 0.78; opacity: 0.6; elide: Text.ElideRight; Layout.fillWidth: true }
                        HoverHandler { id: meterHover }
                        ToolTip.visible: meterHover.hovered
                        ToolTip.text: modelData.tip
                        ToolTip.delay: 400
                    }
                }
            }

            Label {
                visible: card.account.limits.length === 0 || card.account.needsSignIn || card.account.liveFailed
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.65
                font.pointSize: Qt.application.font.pointSize * 0.85
                text: card.account.isLoading ? "Loading usage…"
                    : card.account.needsSignIn ? "Sign in for live usage (… menu)"
                    : card.account.liveFailed ? "Could not load new usage numbers" + (card.account.liveDetail ? ": " + card.account.liveDetail : "")
                                               + (card.account.age ? " · " + card.account.age : "")
                    : "No usage numbers yet"
            }
        }

        ToolButton {
            id: more
            Layout.alignment: Qt.AlignTop
            icon.name: "overflow-menu"
            text: "Account menu"
            display: AbstractButton.IconOnly
            onClicked: accountMenu.popup(more, 0, more.height)
        }
    }

    Menu {
        id: accountMenu
        MenuItem {
            visible: card.account.liveFailed
            height: visible ? implicitHeight : 0
            text: "Load Usage Again"
            onTriggered: actions.reloadUsage()
        }
        MenuItem {
            visible: card.account.needsSignIn || card.account.liveFailed
            height: visible ? implicitHeight : 0
            text: "Sign In for Live Usage…"
            onTriggered: actions.signInForUsage(card.account.id, false)
        }
        MenuItem { text: "Open Chats in Window"; onTriggered: actions.openChats(card.account.id) }
        MenuItem { text: "New Chat in Window"; onTriggered: actions.newChat(card.account.id) }
        MenuSeparator {}
        MenuItem { text: "Rename…"; onTriggered: actions.rename(card.account.id) }
        MenuItem { text: "Set Email…"; onTriggered: actions.setEmail(card.account.id) }
        MenuItem { text: "Move Up"; enabled: !card.account.isFirst; onTriggered: actions.moveUp(card.account.id) }
        MenuItem { text: "Move Down"; enabled: !card.account.isLast; onTriggered: actions.moveDown(card.account.id) }
        MenuSeparator {}
        MenuItem { text: "Remove…"; enabled: !card.account.isActive; onTriggered: actions.remove(card.account.id) }
    }
}
