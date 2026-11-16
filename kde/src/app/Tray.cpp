// SPDX-License-Identifier: MIT
#include "Tray.h"
#include "AccountStore.h"
#include "Actions.h"
#include "Theme.h"

#include <QActionGroup>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>

#include <algorithm>

#ifdef CLAUDS_HAVE_KF6
#include <KStatusNotifierItem>
#else
#include <QSystemTrayIcon>
#endif

namespace clauds {

#ifndef CLAUDS_HAVE_KF6
namespace {
QIcon trayIcon()
{
    return QIcon::fromTheme(QStringLiteral("clauds-tray-symbolic"), QIcon(QStringLiteral(":/icons/clauds-tray-symbolic.svg")));
}
} // namespace
#endif

Tray::Tray(AccountStore *store, Actions *actions, Theme *theme, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_actions(actions)
{
    m_panel = new QQuickView;
    m_panel->setTitle(QStringLiteral("Clauds"));
    m_panel->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_panel->setResizeMode(QQuickView::SizeViewToRootObject);
    m_panel->setColor(Qt::transparent);
    m_panel->rootContext()->setContextProperty(QStringLiteral("store"), store);
    m_panel->rootContext()->setContextProperty(QStringLiteral("actions"), actions);
    m_panel->rootContext()->setContextProperty(QStringLiteral("theme"), theme);
    m_panel->setSource(QUrl(QStringLiteral("qrc:/qml/Panel.qml")));
    // The panel closes when it loses focus, like a popup.
    connect(m_panel, &QWindow::activeChanged, this, [this] {
        if (!m_panel->isActive() && m_panel->isVisible())
            hidePanel();
    });
    connect(actions, &Actions::hidePanelRequested, this, &Tray::hidePanel);

    m_menu = new QMenu;
    connect(store, &AccountStore::changed, this, &Tray::rebuildMenu);
    connect(store, &AccountStore::changed, this, &Tray::updateToolTip);
    rebuildMenu();

#ifdef CLAUDS_HAVE_KF6
    m_item = new KStatusNotifierItem(QStringLiteral("clauds"), this);
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setTitle(QStringLiteral("Clauds"));
    m_item->setIconByName(QStringLiteral("clauds-tray-symbolic"));
    m_item->setStandardActionsEnabled(false);
    m_item->setContextMenu(m_menu);
    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &pos) { togglePanel(pos); });
#else
    m_item = new QSystemTrayIcon(trayIcon(), this);
    m_item->setContextMenu(m_menu);
    connect(m_item, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            togglePanel(QCursor::pos());
    });
    m_item->show();
#endif
    updateToolTip();
}

Tray::~Tray()
{
    delete m_panel;
    delete m_menu;
}

void Tray::updateToolTip()
{
    const auto active = m_store->account(m_store->activeID());
    const QString sub = m_store->sideBySide() ? QStringLiteral("Side by Side: %1 open").arg(m_store->runningIDs().size())
                                              : (active ? QStringLiteral("Current: %1").arg(active->name) : QStringLiteral("No accounts yet"));
#ifdef CLAUDS_HAVE_KF6
    m_item->setToolTip(QStringLiteral("clauds"), QStringLiteral("Clauds"), sub);
#else
    m_item->setToolTip(QStringLiteral("Clauds\n") + sub);
#endif
}

void Tray::rebuildMenu()
{
    m_menu->clear();
    auto *group = new QActionGroup(m_menu);
    for (const auto &a : m_store->accounts()) {
        const bool current = m_store->sideBySide() ? m_store->runningIDs().contains(a.id) : a.id == m_store->activeID();
        auto *action = m_menu->addAction(a.name);
        action->setCheckable(true);
        action->setChecked(current);
        action->setEnabled(!m_store->isSwitching());
        if (!m_store->sideBySide())
            group->addAction(action);
        const QString id = a.id;
        connect(action, &QAction::triggered, m_actions, [this, id] { m_actions->select(id); });
    }
    if (!m_store->accounts().isEmpty())
        m_menu->addSeparator();
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("view-list-details")), QStringLiteral("Show Panel"), this,
                      [this] { showPanel(QCursor::pos()); });
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("internet-chat")), QStringLiteral("Open Claude"), m_actions, &Actions::openClaude);
    m_menu->addSeparator();
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit Clauds"), m_actions, &Actions::quit);
}

void Tray::togglePanel(const QPoint &near)
{
    // A click on the tray icon takes focus from the panel first, which hides it. Do not reopen it
    // in that same click.
    if (m_panel->isVisible() || QDateTime::currentMSecsSinceEpoch() - m_hiddenAt < 300) {
        hidePanel();
        return;
    }
    showPanel(near);
}

void Tray::showPanel(const QPoint &near)
{
    m_store->refreshRunning();
    m_store->refreshUsage();
    // X11: place the panel next to the tray icon, inside the screen. Wayland does not let an app
    // place its own window; KWin places it.
    if (QGuiApplication::platformName() != QLatin1String("wayland") && !near.isNull()) {
        QScreen *screen = QGuiApplication::screenAt(near);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        const QRect area = screen->availableGeometry();
        const QSize size = m_panel->size();
        int x = std::clamp(near.x() - size.width() / 2, area.left() + 8, area.right() - size.width() - 8);
        int y = near.y() > area.center().y() ? area.bottom() - size.height() - 8 : area.top() + 8;
        m_panel->setPosition(x, y);
    }
    m_panel->show();
    m_panel->raise();
    m_panel->requestActivate();
}

void Tray::hidePanel()
{
    if (!m_panel->isVisible())
        return;
    m_panel->hide();
    m_hiddenAt = QDateTime::currentMSecsSinceEpoch();
    m_store->clearAutoSwitchNote();
}

} // namespace clauds
