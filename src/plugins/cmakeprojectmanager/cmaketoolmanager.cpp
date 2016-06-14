/****************************************************************************
**
** Copyright (C) 2014 Canonical Ltd.
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "cmaketoolmanager.h"
#include "cmakeprojectconstants.h"
#include "cmakekitinformation.h"

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <projectexplorer/kit.h>
#include <utils/environment.h>

namespace CMakeProjectManager {

CMakeToolManager* CMakeToolManager::m_instance = 0;
CMakeToolManager::CMakeToolManager()
{
    m_instance = this;
    m_cmakeToolForSystem.setCMakeExecutable(findCmakeExecutable());

    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(QLatin1String("CMakeSettings"));
    m_cmakeToolForUser.setCMakeExecutable(settings->value(QLatin1String("cmakeExecutable")).toString());
    settings->endGroup();

}

CMakeToolManager::~CMakeToolManager()
{
    m_instance = 0;
    m_cmakeToolForSystem.cancel();
    m_cmakeToolForUser.cancel();

    //clean up created cmake tools
    qDeleteAll(m_cmakeTools.begin(),m_cmakeTools.end());
}

void CMakeToolManager::setUserCmakePath(const QString &path)
{
    m_instance->m_cmakeToolForUser.setCMakeExecutable(path);
}

QString CMakeToolManager::userCMakePath()
{
    return m_instance->m_cmakeToolForUser.cmakeExecutable();
}

/*!
 * \brief CMakeToolManager::defaultCMakeTool
 * Returns a pointer to the internal default cmake tool instance
 */
CMakeTool *CMakeToolManager::defaultCMakeTool()
{
    if(m_instance->m_cmakeToolForUser.isValid())
        return &m_instance->m_cmakeToolForUser;
    return &m_instance->m_cmakeToolForSystem;
}

bool CMakeToolManager::registerCMakeTool(ICMakeTool *tool)
{
    if(m_instance->m_cmakeTools.contains(tool->id().uniqueIdentifier()))
        return false;
    m_instance->m_cmakeTools.insert(tool->id().uniqueIdentifier(),tool);
    return true;
}

ICMakeTool *CMakeToolManager::cmakeTool(const Core::Id &id)
{
    if(id == Constants::CMAKE_TOOL_ID)
        return defaultCMakeTool();

    if(m_instance->m_cmakeTools.contains(id.uniqueIdentifier()))
        return m_instance->m_cmakeTools[id.uniqueIdentifier()];

    qDebug()<<"Don't know "<<id.toString()<<" Creating and registering it";

    //try to find a factory that can create the cmake tool
    QList<ICMakeToolFactory*> factories = ExtensionSystem::PluginManager::getObjects<ICMakeToolFactory>();
    foreach(ICMakeToolFactory* factory, factories) {
        if(factory->canCreate(id)) {
            ICMakeTool* cmake = factory->create(id);
            m_instance->m_cmakeTools.insert(id.uniqueIdentifier(),cmake);

            return cmake;
        }
    }
    return 0;
}

ICMakeTool *CMakeToolManager::cmakeToolForKit(const ProjectExplorer::Kit *kit)
{
    return CMakeKitInformation::cmakeTool(kit);
}

QString CMakeToolManager::findCmakeExecutable()
{
    return Utils::Environment::systemEnvironment().searchInPath(QLatin1String("cmake"));
}

} // namespace CMakeProjectManager
