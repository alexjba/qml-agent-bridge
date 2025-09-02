cmake_minimum_required(VERSION 3.18)
project(example-minimal-cpp LANGUAGES CXX)

find_package(Qt6 COMPONENTS Core Gui Qml Quick QUIET)
if(NOT Qt6_FOUND)
  find_package(Qt5 COMPONENTS Core Gui Qml Quick REQUIRED)
  set(QT_LIBS Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick)
else()
  set(QT_LIBS Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
endif()

add_executable(example-minimal main.cpp qml.qrc)

target_link_libraries(example-minimal PRIVATE  qml_agent_bridge)
