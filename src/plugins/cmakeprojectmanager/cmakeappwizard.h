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

#ifndef CMAKEPROJECTMANAGER_CMAKEAPPWIZARD_H
#define CMAKEPROJECTMANAGER_CMAKEAPPWIZARD_H

#include "cmakeprojectmanager_global.h"
#include <projectexplorer/baseprojectwizarddialog.h>
#include <projectexplorer/customwizard/customwizard.h>
#include <projectexplorer/targetsetuppage.h>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

namespace Utils {
class PathChooser;
}

namespace CMakeProjectManager {

class ChooseCMakePage;

class CMAKEPROJECTMANAGER_EXPORT CMakeAppWizard : public ProjectExplorer::CustomProjectWizard
{
    Q_OBJECT

public:
    CMakeAppWizard();
    static void registerSelf();

private:
    QWizard *createWizardDialog(QWidget *parent,
                                const Core::WizardDialogParameters &wizardDialogParameters) const;
    bool postGenerateFiles(const QWizard *, const Core::GeneratedFiles &l, QString *errorMessage);

private:
    enum { targetPageId = 1 };
};

class CMAKEPROJECTMANAGER_EXPORT CMakeAppWizardDialog : public ProjectExplorer::BaseProjectWizardDialog
{
    Q_OBJECT
public:
    explicit CMakeAppWizardDialog(QWidget *parent, const Core::WizardDialogParameters &parameters);
    virtual ~CMakeAppWizardDialog();

    int addTargetSetupPage(int id = -1);
    int addChooseCMakePage(int id = -1);

    QList<Core::Id> selectedKits() const;
    bool writeUserFile(const QString &cmakeFileName) const;
private slots:
    void generateProfileName(const QString &name, const QString &path);
private:
    ProjectExplorer::TargetSetupPage *m_targetSetupPage;
    ChooseCMakePage* m_cmakePage;
    void init();
};

class CMAKEPROJECTMANAGER_EXPORT ChooseCMakePage : public QWizardPage
{
    Q_OBJECT
public:
    ChooseCMakePage(QWidget *parent = 0);

    virtual bool isComplete() const;
public slots:
    void cmakeExecutableChanged();
private:
    void updateErrorText();
    QLabel *m_cmakeLabel;
    Utils::PathChooser *m_cmakeExecutable;
};

class ChooseCMakeWizard : public Utils::Wizard
{
    Q_OBJECT
public:
    ChooseCMakeWizard();
};

} // namespace CMakeProjectManager

#endif // CMAKEPROJECTMANAGER_CMAKEAPPWIZARD_H
