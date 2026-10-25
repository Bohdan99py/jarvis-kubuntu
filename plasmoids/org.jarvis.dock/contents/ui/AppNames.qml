import QtQuick
import org.kde.plasma.private.kicker as Kicker

// The applications' own names for desktop file ids (0.20: the settings listed the
// pinned applications as "org.kde.konsole"). Plasma's favourites model, the one
// the launcher uses; loaded by a Loader, so the settings still open without it.
Item {
    id: appNames
    property var ids: []              // desktop file ids: org.kde.konsole.desktop
    property var names: ({})          // id → name
    visible: false
    Kicker.SimpleFavoritesModel { id: favorites; favorites: appNames.ids }
    Repeater {
        model: favorites
        Item {
            required property var model
            Component.onCompleted: {
                const n = Object.assign({}, appNames.names)
                n[model.favoriteId] = model.display
                appNames.names = n
            }
        }
    }
}
