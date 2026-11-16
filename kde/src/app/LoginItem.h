// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace clauds::LoginItem {

/// Start at Login: an XDG autostart entry in ~/.config/autostart. Plasma reads it at login and
/// lists it in System Settings > Autostart.
QString path();
bool isOn();
bool set(bool on, QString *error = nullptr);

} // namespace clauds::LoginItem
