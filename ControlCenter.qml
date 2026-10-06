import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Jarvis

Dialog {
    id: center
    required property var desktop
    required property var voice
    required property var chat
    signal apiSettingsRequested()
    function showVoice() { tabs.currentIndex=1; open() }
    title: "Центр управления"
    modal: true; anchors.centerIn: Overlay.overlay
    width: Math.min(780, Overlay.overlay.width-40)
    height: Math.min(660, Overlay.overlay.height-40)
    padding: 22
    background: Rectangle { color: Theme.panel; radius: 22; border.color: Theme.border }
    header: Label { text: center.title; color: Theme.text; font.pixelSize: 24; padding: 22 }
    contentItem: ColumnLayout {
        spacing: 16
        TabBar {
            id: tabs; objectName: "controlTabs"; Layout.fillWidth: true
            Repeater {
                model: ["Навыки", "Голос", "Приложение"]
                TabButton {
                    id: tabButton
                    required property string modelData
                    text: modelData
                    contentItem: Text { text: tabButton.text; color: tabButton.checked ? Theme.accent : Theme.textDim; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: tabButton.checked ? "#203b4c" : Theme.bg; radius: 8 }
                }
            }
        }
        StackLayout {
            currentIndex: tabs.currentIndex; Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                Label { text: "Подключайте нужные темы. Примеры работают локально, расширенные ответы — через Claude."; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10
                    model: center.desktop.skills
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: skill
                        required property var modelData
                        width: ListView.view.width; height: 86; radius: 14; color: Theme.bg; border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 14
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4
                                Label { text: skill.modelData.name; color: Theme.text; font.pixelSize: 17; font.bold: true; textFormat: Text.PlainText }
                                Label { text: skill.modelData.description; color: Theme.textDim; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                            }
                            DarkButton { text: "О навыке"; onClicked: { details.skill=skill.modelData; details.open() } }
                            Switch { checked: skill.modelData.enabled; onClicked: center.desktop.setSkillEnabled(skill.modelData.id, checked); Accessible.name: skill.modelData.name }
                        }
                    }
                }
                RowLayout {
                    DarkButton { text: "Импорт JSON"; onClicked: skillFile.open() }
                    TextField { id: skillUrl; Layout.fillWidth: true; placeholderText: "https://…/skill.json"; placeholderTextColor: Theme.textDim; selectByMouse: true; color: Theme.text; background: Rectangle { color: Theme.bg; radius: 8 } }
                    DarkButton { text: "Получить"; enabled: skillUrl.text.trim().length>0; onClicked: center.desktop.fetchSkill(skillUrl.text.trim()) }
                }
                Label { text: center.desktop.skillStatus; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
            }
            ColumnLayout {
                spacing: 16
                Label { text: "Разговаривайте с Jarvis"; color: Theme.text; font.pixelSize: 24; font.bold: true }
                Label { text: "Нажмите микрофон, произнесите фразу и нажмите стоп.\nРаспознанный текст появится в строке ввода — отправьте его клавишей Enter."; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Switch { text: "Озвучивать ответы"; checked: center.voice.enabled; onClicked: center.voice.enabled=checked; palette.windowText: Theme.text }
                RowLayout {
                    DarkButton { text: "Проверить голос"; enabled: !center.voice.speaking; onClicked: { center.voice.enabled=true; center.voice.speak("Привет! Я Джарвис. Голосовой вывод включён.") } }
                    DarkButton { text: "Стоп"; enabled: center.voice.speaking || center.voice.recording; onClicked: center.voice.stop() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                Label { text: center.voice.ready ? "Локальное распознавание готово" : "Русская модель распознавания"; color: Theme.text; font.pixelSize: 18 }
                Label { text: "Первичная настройка скачивает Vosk из PyPI и модель (~45 МБ) с alphacephei.com. После настройки звук распознаётся на компьютере."; color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap }
                DarkButton { text: center.voice.ready ? "Проверить установку голоса" : "Установить голос"; primary: true; enabled: !center.voice.busy; onClicked: center.voice.setup() }
                Label { text: center.voice.status; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                Item { Layout.fillHeight: true }
            }
            ColumnLayout {
                spacing: 14
                Label { text: "Jarvis " + center.desktop.version; color: Theme.text; font.pixelSize: 26; font.bold: true }
                Label { text: "Обновления: Bohdan99py/jarvis-kubuntu"; color: Theme.textDim }
                Label { text: "Кнопка скачивает проверенный пакет и открывает Discover. Подтвердите системную установку, затем перезапустите службу и приложение."; color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap }
                DarkButton { text: center.desktop.updating ? "Проверка и загрузка…" : "Обновить Jarvis"; primary: true; enabled: !center.desktop.updating; onClicked: center.desktop.update() }
                Label { text: center.desktop.updateStatus; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                RowLayout {
                    DarkButton { text: "Перезапустить службу"; enabled: !center.chat.busy; onClicked: center.desktop.restartDaemon() }
                    DarkButton { text: "Перезапустить Jarvis"; enabled: !center.chat.busy && !center.voice.busy && !center.desktop.updating; onClicked: center.desktop.restart() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                Label { text: "Meta+J — быстрые команды\nКнопка в меню KDE и значок в системном трее"; color: Theme.textDim; lineHeight: 1.4 }
                DarkButton { text: "Настройки Claude API"; onClicked: { center.close(); center.apiSettingsRequested() } }
                Item { Layout.fillHeight: true }
            }
        }
        DarkButton { Layout.alignment: Qt.AlignRight; text: "Готово"; onClicked: center.close() }
    }
    FileDialog { id: skillFile; title: "Импорт навыка"; nameFilters: ["Навыки Jarvis (*.json)"]; onAccepted: center.desktop.importSkill(selectedFile) }
    Dialog {
        id: details
        property var skill: ({})
        anchors.centerIn: Overlay.overlay; width: Math.min(600, center.width-30); height: 380; modal: true
        title: skill.name || "Навык"; standardButtons: Dialog.Close
        ScrollView { anchors.fill: parent; TextArea { readOnly: true; wrapMode: TextEdit.Wrap; text: (details.skill.prompt || "") + "\n\nЛокальных примеров: " + (details.skill.examples || []).length } }
    }
}
