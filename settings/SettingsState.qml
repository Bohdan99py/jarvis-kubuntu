pragma Singleton

import QtQuick

// Shared by the settings window and its rows: which row a search result
// points at (it is outlined for a few seconds) and whether the window is narrow
// (rows put their control under the text).
QtObject {
    property string highlight: ""
    property bool narrow: false
}
