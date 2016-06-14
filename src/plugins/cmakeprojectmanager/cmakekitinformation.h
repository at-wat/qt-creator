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

#ifndef CMAKEPROJECTMANAGER_CMAKEKITINFORMATION_H
#define CMAKEPROJECTMANAGER_CMAKEKITINFORMATION_H

#include "cmakeprojectmanager_global.h"
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/kitconfigwidget.h>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace CMakeProjectManager {

class ICMakeTool;

class CMAKEPROJECTMANAGER_EXPORT CMakeKitInformationWidget : public ProjectExplorer::KitConfigWidget
{
    Q_OBJECT
public:
    CMakeKitInformationWidget(ProjectExplorer::Kit *kit,
                        const ProjectExplorer::KitInformation *ki);
    ~CMakeKitInformationWidget();

    QString displayName() const;
    QString toolTip() const;
    void makeReadOnly();
    void refresh();
    bool visibleInKit();

    QWidget *mainWidget() const;
    QWidget *buttonWidget() const;

private:
    QLabel *m_label;
    QPushButton *m_button;
};

class CMAKEPROJECTMANAGER_EXPORT CMakeKitInformation : public ProjectExplorer::KitInformation
{
    Q_OBJECT
public:
    CMakeKitInformation();

    QVariant defaultValue(ProjectExplorer::Kit *) const;

    QList<ProjectExplorer::Task> validate(const ProjectExplorer::Kit *) const;

    ItemList toUserOutput(const ProjectExplorer::Kit *) const;

    ProjectExplorer::KitConfigWidget *createConfigWidget(ProjectExplorer::Kit *) const;

    static Core::Id id();
    static bool hasSpecialCMakeTool(const ProjectExplorer::Kit *kit);
    static ICMakeTool* cmakeTool(const ProjectExplorer::Kit *kit);
    static Core::Id cmakeToolId(const ProjectExplorer::Kit *kit);
    static void setCMakeTool(ProjectExplorer::Kit *kit, const Core::Id &toolId);
};

class CMAKEPROJECTMANAGER_EXPORT CMakeKitMatcher : public ProjectExplorer::KitMatcher
{
public:
    explicit CMakeKitMatcher() { }
    bool matches(const ProjectExplorer::Kit *k) const;
};

} // namespace CMakeProjectManager

#endif // CMAKEPROJECTMANAGER_CMAKEKITINFORMATION_H
