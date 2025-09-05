import QtQuick
import QtQuick.Controls
ApplicationWindow {
    id: win
    width: 400; height: 300; visible: true
    title: "Minimal Example"
    Rectangle { anchors.fill: parent; color: "#20232a" }
    Column {
        anchors.centerIn: parent
        spacing: 12
        // Custom emitter with signals bound from button click
        Item {
            id: customEmitter
            objectName: "customEmitter"
            signal ping()
            signal message(string text)
        }
        Button {
            id: helloButton
            text: "Hello"
            objectName: "helloButton"
            onClicked: {
                customEmitter.ping()
                customEmitter.message("from button")
            }
        }
        TextField { id: nameField; width: 200; placeholderText: "Type text"; objectName: "nameField" }
        CheckBox { id: toggle; text: "Enabled"; checked: true; objectName: "toggleBox" }
        // Simple ListModel for testing
        ListModel {
            id: fruitsModel
            objectName: "fruitsModel"
            ListElement { name: "Apple"; color: "red" }
            ListElement { name: "Banana"; color: "yellow" }
            ListElement { name: "Grape"; color: "purple" }
        }
    }
}
