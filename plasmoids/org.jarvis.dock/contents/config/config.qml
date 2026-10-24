import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18nd("jarvis-desktop", "General")
        icon: "configure"
        source: "configGeneral.qml"
    }
}
