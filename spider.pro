

TEMPLATE = app
TARGET = spider
INCLUDEPATH += .

CONFIG += console
CONFIG -= app_bundle


DEFINES += QT_DEPRECATED_WARNINGS

macx {
    INCLUDEPATH += /usr/local/include
    DEPENDPATH += /usr/local/lib
    LIBS += -L"/usr/local/lib" -lxml2 -lcurl
}

# Input
HEADERS += \
           spider.h
SOURCES += main.cpp \
           spider.cpp \
           bloom.cpp
