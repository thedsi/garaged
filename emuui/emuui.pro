QT += core gui widgets
CONFIG += c++14
TARGET = emuui
TEMPLATE = app
DEFINES += EMU
HEADERS += ../emu.h ../garaged.h ../events.h
SOURCES += ../garaged.cpp ../ui.cpp ../events.cpp
