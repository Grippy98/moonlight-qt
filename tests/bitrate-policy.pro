QT += core network
QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = bitrate-policy-test
SOURCES += bitrate-policy.cpp
HEADERS += ../app/backend/bitratepolicy.h
