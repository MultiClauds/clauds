// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QPoint>

class QMenu;
class QQuickView;
class QSystemTrayIcon;
#ifdef CLAUDS_HAVE_KF6
class KStatusNotifierItem;
#endif

namespace clauds {

class AccountStore;
class Actions;
class Theme;

/// The Plasma system tray item and the panel window.
///
/// With KDE Frameworks: a KStatusNotifierItem (the native Plasma tray protocol). Without: a
/// QSystemTrayIcon, which Qt also maps to StatusNotifierItem on Plasma.
/// Left click opens the panel. Right click shows a quick menu with every account.
class Tray : public QObject
{
    Q_OBJECT
public:
    Tray(AccountStore *store, Actions *actions, Theme *theme, QObject *parent = nullptr);
    ~Tray() override;

public Q_SLOTS:
    void togglePanel(const QPoint &near = {});
    void showPanel(const QPoint &near = {});
    void hidePanel();

private:
    void rebuildMenu();
    void updateToolTip();
    void syncPanelSize();

    AccountStore *m_store;
    Actions *m_actions;
    QQuickView *m_panel = nullptr;
    QMenu *m_menu = nullptr;
    qint64 m_hiddenAt = 0;
#ifdef CLAUDS_HAVE_KF6
    KStatusNotifierItem *m_item = nullptr;
#else
    QSystemTrayIcon *m_item = nullptr;
#endif
};

} // namespace clauds
