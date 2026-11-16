// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QObject>

namespace clauds {

/// Colors that come from the user's KDE color scheme (for example Quantum Light / Quantum Dark),
/// so that the panel never hard-codes a color. Updates when the scheme changes.
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor positive READ positive NOTIFY changed)   ///< usage below 60%
    Q_PROPERTY(QColor neutral READ neutral NOTIFY changed)     ///< 60% to 84%
    Q_PROPERTY(QColor negative READ negative NOTIFY changed)   ///< 85% and more
    Q_PROPERTY(QColor active READ active NOTIFY changed)       ///< "open now" dot
    Q_PROPERTY(QColor cardBackground READ cardBackground NOTIFY changed)
    Q_PROPERTY(QColor cardBorder READ cardBorder NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    QColor positive() const { return m_positive; }
    QColor neutral() const { return m_neutral; }
    QColor negative() const { return m_negative; }
    QColor active() const { return m_positive; }
    QColor cardBackground() const { return m_card; }
    QColor cardBorder() const { return m_border; }

    Q_INVOKABLE QColor levelColor(int level) const;
    /// A stable avatar color for an account id, derived from the scheme's highlight color.
    Q_INVOKABLE QColor avatarColor(const QString &id) const;

Q_SIGNALS:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reload();
    QColor m_positive, m_neutral, m_negative, m_card, m_border, m_highlight;
};

} // namespace clauds
