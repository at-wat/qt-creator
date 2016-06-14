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

#include "cmakekitinformation.h"
#include "cmaketool.h"
#include "cmaketoolmanager.h"
#include "cmakeprojectconstants.h"
#include "generatorinfo.h"

#include <utils/elidinglabel.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace CMakeProjectManager {

CMakeKitInformation::CMakeKitInformation()
{
    setId(CMakeKitInformation::id());
}

QVariant CMakeKitInformation::defaultValue(Kit *kit) const
{
    Q_UNUSED(kit);
    return QString::fromLatin1(Constants::CMAKE_TOOL_ID);
}

QList<Task> CMakeKitInformation::validate(const Kit *) const
{
    return QList<Task>();
}

KitInformation::ItemList CMakeKitInformation::toUserOutput(const Kit *kit) const
{
    return KitInformation::ItemList()
            << qMakePair(tr("CMake Tool"), cmakeTool(kit)->displayName());
}

KitConfigWidget *CMakeKitInformation::createConfigWidget(Kit *kit) const
{
    return new CMakeKitInformationWidget(kit, this);
}

Core::Id CMakeKitInformation::id()
{
    return "CMakeProjectManager.KitInformation";
}

bool CMakeKitInformation::hasSpecialCMakeTool(const Kit *kit)
{
    QString id(kit->value(CMakeKitInformation::id()).toString());
    if(id.isNull())
        return false;

    if(id == QString::fromLatin1(Constants::CMAKE_TOOL_ID))
        return false;

    return true;
}

ICMakeTool* CMakeKitInformation::cmakeTool(const Kit *kit)
{
    Core::Id id = cmakeToolId(kit);

    if(!id.isValid() || id == Constants::CMAKE_TOOL_ID)
        return CMakeToolManager::defaultCMakeTool();

    ICMakeTool* tool = CMakeToolManager::cmakeTool(id);
    //the cmake tool id is not known, fall back to default tool
    //should we also remove the ID from the KIT?
    if(!tool) {
        return CMakeToolManager::defaultCMakeTool();
    }
    return tool;
}

Core::Id CMakeKitInformation::cmakeToolId(const Kit *kit)
{
    Core::Id test = Core::Id::fromSetting(kit->value(CMakeKitInformation::id()));
    qDebug()<<"Id for kit: "<<kit->displayName()<<" "<<test.toString()<<" "<<test.uniqueIdentifier();
    return test;
}

void CMakeKitInformation::setCMakeTool(Kit *kit, const Core::Id &toolId)
{
    kit->setValue(CMakeKitInformation::id(), toolId.toSetting());
}

///////////////
// CMakeKitInformationWidget
///////////////


CMakeKitInformationWidget::CMakeKitInformationWidget(Kit *kit, const KitInformation *ki)
    : KitConfigWidget(kit, ki),
      m_label(new ElidingLabel)
{
    refresh();
}

CMakeKitInformationWidget::~CMakeKitInformationWidget()
{
    delete m_label;
}

QString CMakeKitInformationWidget::displayName() const
{
    return tr("CMake Tool");
}

QString CMakeKitInformationWidget::toolTip() const
{
    return tr("The CMake Tool used for this Kit.");
}

void CMakeKitInformationWidget::makeReadOnly()
{
}

void CMakeKitInformationWidget::refresh()
{
    m_label->setText(CMakeKitInformation::cmakeTool(m_kit)->displayName());
}

bool CMakeKitInformationWidget::visibleInKit()
{
    Core::Id toolId = CMakeKitInformation::cmakeToolId(m_kit);
    if(!toolId.isValid() || toolId == Constants::CMAKE_TOOL_ID)
        return false;
    return true;
}

QWidget *CMakeKitInformationWidget::mainWidget() const
{
    return m_label;
}

QWidget *CMakeKitInformationWidget::buttonWidget() const
{
    return 0;
}

bool CMakeKitMatcher::matches(const ProjectExplorer::Kit *k) const
{
    //this is safe, since the returned list is not used for more than checking
    //if there are any results
    ProjectExplorer::Kit* otherKit = const_cast<ProjectExplorer::Kit*>(k);

    ICMakeTool* cmake = CMakeToolManager::cmakeToolForKit(k);
    if(!cmake)
        return false;

    QList<GeneratorInfo> infos = GeneratorInfo::generatorInfosFor(otherKit
                                                                  ,GeneratorInfo::OfferNinja
                                                                  ,false //not relevant at this place
                                                                  ,cmake->hasCodeBlocksMsvcGenerator());

    return (infos.size() > 0);
}

} // namespace CMakeProjectManager
