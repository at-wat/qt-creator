include(../../qtcreatorplugin.pri)

HEADERS = cmakebuildinfo.h \
    cmakeproject.h \
    cmakeprojectplugin.h \
    cmakeprojectmanager.h \
    cmakeprojectmanager_global.h \
    cmakeprojectconstants.h \
    cmakeprojectnodes.h \
    makestep.h \
    cmakerunconfiguration.h \
    cmakebuildconfiguration.h \
    cmakeeditorfactory.h \
    cmakeeditor.h \
    cmakehighlighter.h \
    cmakehighlighterfactory.h \
    cmakelocatorfilter.h \
    cmakefilecompletionassist.h \
    cmakeparser.h \
    generatorinfo.h \
    cmakeappwizard.h \
    cmakekitinformation.h \
    cmaketool.h \
    argumentslineedit.h \
    cmaketoolmanager.h \
    cmakesettingspage.h

SOURCES = cmakeproject.cpp \
    cmakeprojectplugin.cpp \
    cmakeprojectmanager.cpp \
    cmakeprojectnodes.cpp \
    makestep.cpp \
    cmakerunconfiguration.cpp \
    cmakebuildconfiguration.cpp \
    cmakeeditorfactory.cpp \
    cmakeeditor.cpp \
    cmakehighlighter.cpp \
    cmakehighlighterfactory.cpp \
    cmakelocatorfilter.cpp \
    cmakefilecompletionassist.cpp \
    cmakeparser.cpp \
    generatorinfo.cpp \
    cmakeappwizard.cpp \
    cmakekitinformation.cpp \
    cmaketool.cpp \
    argumentslineedit.cpp \
    cmaketoolmanager.cpp \
    cmakesettingspage.cpp


RESOURCES += cmakeproject.qrc
DEFINES += CMAKEPROJECTMANAGER_LIBRARY
