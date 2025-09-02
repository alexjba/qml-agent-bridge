import QtQuick
import QtQuick.Controls
ApplicationWindow {
    id: win
    width: 400; height: 300; visible: true
    title: "Minimal Example"
    Rectangle { anchors.fill: parent; color: "#20232a" }
    Button { id: helloButton; text: "Hello"; anchors.centerIn: parent; objectName: "helloButton" }
}
