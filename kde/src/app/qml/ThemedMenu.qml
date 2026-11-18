// SPDX-License-Identifier: MIT
// A Menu whose colors follow the system palette. A menu is a window of its own and does not take
// the palette of the panel, so without this it keeps the colors it had at start-up.
import QtQuick
import QtQuick.Controls

Menu {
    SystemPalette { id: pal; colorGroup: SystemPalette.Active }

    palette.window: pal.window
    palette.windowText: pal.windowText
    palette.base: pal.base
    palette.alternateBase: pal.alternateBase
    palette.text: pal.text
    palette.button: pal.button
    palette.buttonText: pal.buttonText
    palette.highlight: pal.highlight
    palette.highlightedText: pal.highlightedText
    palette.light: pal.light
    palette.midlight: pal.midlight
    palette.mid: pal.mid
    palette.dark: pal.dark
    palette.shadow: pal.shadow
}
