QT += core gui widgets network dbus webenginecore webenginewidgets
CONFIG += c++17
TARGET = hum
DESTDIR = build
OBJECTS_DIR = build/obj
MOC_DIR = build/moc
RCC_DIR = build/rcc

SOURCES = \
  src/main.cpp \
  src/engine.cpp \
  src/spotify.cpp \
  src/suno.cpp \
  src/player.cpp \
  src/mpris.cpp \
  src/util.cpp

HEADERS = \
  src/engine.h \
  src/spotify.h \
  src/suno.h \
  src/player.h \
  src/mpris.h \
  src/util.h

RESOURCES = src/resources.qrc
