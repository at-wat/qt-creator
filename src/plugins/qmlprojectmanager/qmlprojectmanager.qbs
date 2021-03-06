import qbs.base 1.0

import QtcPlugin

QtcPlugin {
    name: "QmlProjectManager"

    Depends { name: "Qt"; submodules: ["widgets", "network"] }
    Depends { name: "Core" }
    Depends { name: "ProjectExplorer" }
    Depends { name: "TextEditor" }
    Depends { name: "QmlJSEditor" }
    Depends { name: "QmlJS" }
    Depends { name: "QmlJSTools" }
    Depends { name: "QmlDebug" }
    Depends { name: "QtSupport" }
    Depends { name: "app_version_header" }

    Group {
        name: "General"
        files: [
            "qmlapp.cpp", "qmlapp.h",
            "qmlapplicationwizard.cpp", "qmlapplicationwizard.h",
            "qmlapplicationwizardpages.cpp", "qmlapplicationwizardpages.h",
            "qmlproject.cpp", "qmlproject.h",
            "qmlproject.qrc",
            "qmlprojectconstants.h",
            "qmlprojectenvironmentaspect.cpp", "qmlprojectenvironmentaspect.h",
            "qmlprojectfile.cpp", "qmlprojectfile.h",
            "qmlprojectmanager.cpp", "qmlprojectmanager.h",
            "qmlprojectmanager_global.h",
            "qmlprojectmanagerconstants.h",
            "qmlprojectnodes.cpp", "qmlprojectnodes.h",
            "qmlprojectplugin.cpp", "qmlprojectplugin.h",
            "qmlprojectrunconfiguration.cpp", "qmlprojectrunconfiguration.h",
            "qmlprojectrunconfigurationfactory.cpp", "qmlprojectrunconfigurationfactory.h",
            "qmlprojectrunconfigurationwidget.cpp", "qmlprojectrunconfigurationwidget.h"
        ]
    }

    Group {
        name: "File Format"
        prefix: "fileformat/"
        files: [
            "filefilteritems.cpp", "filefilteritems.h",
            "qmlprojectfileformat.cpp", "qmlprojectfileformat.h",
            "qmlprojectitem.cpp", "qmlprojectitem.h",
        ]
    }
}
