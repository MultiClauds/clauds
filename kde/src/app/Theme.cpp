// SPDX-License-Identifier: MIT
#include "Theme.h"

#include <QCryptographicHash>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>

#include <algorithm>
#include <cmath>

#ifdef CLAUDS_HAVE_KF6
#include <KColorScheme>
#endif

namespace clauds {

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    reload();
    qApp->installEventFilter(this);
}

bool Theme::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        reload();
    return QObject::eventFilter(watched, event);
}

void Theme::reload()
{
    const QPalette pal = QGuiApplication::palette();
    m_highlight = pal.color(QPalette::Highlight);
#ifdef CLAUDS_HAVE_KF6
    // KColorScheme reads kdeglobals. In the Flatpak that file is a single-file mount that goes
    // stale when Plasma rewrites it, so the colors would not follow a change of the scheme. The
    // QPalette does follow it, so there the palette is used (the Breeze defaults for the states).
    static const bool sandboxed = QFile::exists(QStringLiteral("/.flatpak-info"));
    if (sandboxed) {
        m_positive = QColor(0x27, 0xae, 0x60);
        m_neutral = QColor(0xf6, 0x74, 0x00);
        m_negative = QColor(0xda, 0x44, 0x53);
        m_card = pal.color(QPalette::Base);
    } else {
        const KColorScheme view(QPalette::Active, KColorScheme::View);
        m_positive = view.foreground(KColorScheme::PositiveText).color();
        m_neutral = view.foreground(KColorScheme::NeutralText).color();
        m_negative = view.foreground(KColorScheme::NegativeText).color();
        m_card = view.background(KColorScheme::NormalBackground).color();
    }
#else
    // Without KDE Frameworks there is no positive/neutral/negative role in QPalette.
    // These are the Breeze defaults; with KF6 the user's scheme is used instead.
    m_positive = QColor(0x27, 0xae, 0x60);
    m_neutral = QColor(0xf6, 0x74, 0x00);
    m_negative = QColor(0xda, 0x44, 0x53);
    m_card = pal.color(QPalette::Base);
#endif
    QColor border = pal.color(QPalette::WindowText);
    border.setAlphaF(0.12);
    m_border = border;
    Q_EMIT changed();
}

QColor Theme::levelColor(int level) const
{
    return level <= 0 ? m_positive : (level == 1 ? m_neutral : m_negative);
}

QColor Theme::avatarColor(const QString &id) const
{
    // Same hue rotation for the same id; saturation and lightness follow the scheme's highlight,
    // so the avatars fit light and dark schemes.
    const auto hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1);
    const int step = static_cast<unsigned char>(hash.at(0)) % 7;
    float h, s, l, a;
    m_highlight.getHslF(&h, &s, &l, &a);
    if (h < 0)
        h = 0.58f;
    const float hue = std::fmod(h + step / 7.0f, 1.0f);
    return QColor::fromHslF(hue, std::clamp(s, 0.45f, 0.75f), std::clamp(l, 0.38f, 0.52f));
}

} // namespace clauds
