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

#include "cmakeappwizard.h"
#include "cmakekitinformation.h"
#include "cmakeprojectmanager.h"
#include "cmakeproject.h"
#include "cmaketoolmanager.h"

#include <utils/qtcassert.h>
#include <utils/pathchooser.h>
#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtsupportconstants.h>
#include <projectexplorer/kitmanager.h>
#include <extensionsystem/pluginmanager.h>

#include <QDir>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QDebug>
#include <QLabel>
#include <QFormLayout>

namespace CMakeProjectManager {

using namespace Internal;

CMakeAppWizard::CMakeAppWizard()
{
}

QWizard *CMakeAppWizard::createWizardDialog (QWidget *parent, const Core::WizardDialogParameters &wizardDialogParameters) const
{
    QTC_ASSERT(!parameters().isNull(), return 0);
    CMakeAppWizardDialog *projectDialog = new CMakeAppWizardDialog(parent, wizardDialogParameters);

    int firstPage = 1;
    if(!CMakeToolManager::defaultCMakeTool()->isValid()) {
        projectDialog->addChooseCMakePage(firstPage);
        firstPage++;
    }
    projectDialog->addTargetSetupPage(firstPage);

    initProjectWizardDialog(projectDialog,
                            wizardDialogParameters.defaultPath(),
                            wizardDialogParameters.extensionPages());

    projectDialog->setIntroDescription(tr("This wizard generates a Application project using CMake."));
    return projectDialog;
}

bool CMakeAppWizard::postGenerateFiles(const QWizard *w, const Core::GeneratedFiles &l, QString *errorMessage)
{
    const CMakeAppWizardDialog *dialog = qobject_cast<const CMakeAppWizardDialog *>(w);

    // Generate user settings
    foreach (const Core::GeneratedFile &file, l)
        if (file.attributes() & Core::GeneratedFile::OpenProjectAttribute) {
            dialog->writeUserFile(file.path());
            break;
        }

    // Post-Generate: Open the projects/editors
    return ProjectExplorer::CustomProjectWizard::postGenerateOpen(l ,errorMessage);
}

void CMakeAppWizard::registerSelf()
{
    ProjectExplorer::CustomWizard::registerFactory<CMakeAppWizard>(QLatin1String("cmakeapp-project"));
}

CMakeAppWizardDialog::CMakeAppWizardDialog(QWidget *parent, const Core::WizardDialogParameters &parameters)
    : ProjectExplorer::BaseProjectWizardDialog(parent,parameters)
    , m_targetSetupPage(0)
    , m_cmakePage(0)
{
    init();
}

CMakeAppWizardDialog::~CMakeAppWizardDialog()
{
    if (m_targetSetupPage && !m_targetSetupPage->parent())
        delete m_targetSetupPage;

    if (m_cmakePage && !m_cmakePage->parent())
        delete m_cmakePage;
}

bool CMakeAppWizardDialog::writeUserFile(const QString &cmakeFileName) const
{
    if (!m_targetSetupPage)
        return false;

    CMakeManager *manager = ExtensionSystem::PluginManager::getObject<CMakeManager>();
    Q_ASSERT(manager);

    CMakeProject *pro = new CMakeProject(manager, cmakeFileName);
    bool success = m_targetSetupPage->setupProject(pro);
    if (success)
        pro->saveSettings();
    delete pro;
    return success;
}


void CMakeAppWizardDialog::init()
{
    connect(this, SIGNAL(projectParametersChanged(QString,QString)),
            this, SLOT(generateProfileName(QString,QString)));
}

int CMakeAppWizardDialog::addTargetSetupPage(int id)
{
    m_targetSetupPage = new ProjectExplorer::TargetSetupPage;
    const QString platform = selectedPlatform();

    //prefer Qt Desktop or Platform Kit
    Core::FeatureSet features = Core::FeatureSet(QtSupport::Constants::FEATURE_DESKTOP);
    if (platform.isEmpty())
        m_targetSetupPage->setPreferredKitMatcher(new QtSupport::QtVersionKitMatcher(features));
    else
        m_targetSetupPage->setPreferredKitMatcher(new QtSupport::QtPlatformKitMatcher(platform));

    //make sure only CMake compatible Kits are shown
    m_targetSetupPage->setRequiredKitMatcher(new CMakeKitMatcher());

    resize(900, 450);
    if (id >= 0)
        setPage(id, m_targetSetupPage);
    else
        id = addPage(m_targetSetupPage);

    wizardProgress()->item(id)->setTitle(tr("Kits"));
    return id;
}

int CMakeAppWizardDialog::addChooseCMakePage(int id)
{
    m_cmakePage = new ChooseCMakePage;

    if (id >= 0)
        setPage(id, m_cmakePage);
    else
        id = addPage(m_cmakePage);

    wizardProgress()->item(id)->setTitle(tr("CMake"));
    return id;
}

QList<Core::Id> CMakeAppWizardDialog::selectedKits() const
{
    if(m_targetSetupPage)
        return m_targetSetupPage->selectedKits();

    return QList<Core::Id>();
}

void CMakeAppWizardDialog::generateProfileName(const QString &name, const QString &path)
{
    if (!m_targetSetupPage)
        return;

    const QString proFile =
            QDir::cleanPath(path + QLatin1Char('/') + name + QLatin1Char('/') + QLatin1String("CMakeLists.txt"));

    m_targetSetupPage->setProjectPath(proFile);
}

ChooseCMakePage::ChooseCMakePage(QWidget *parent)
    : QWizardPage(parent)
{
    QFormLayout *fl = new QFormLayout;
    fl->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    setLayout(fl);

    m_cmakeLabel = new QLabel;
    m_cmakeLabel->setWordWrap(true);
    fl->addRow(m_cmakeLabel);
    // Show a field for the user to enter
    m_cmakeExecutable = new Utils::PathChooser(this);
    m_cmakeExecutable->setExpectedKind(Utils::PathChooser::ExistingCommand);
    fl->addRow(tr("CMake Executable:"), m_cmakeExecutable);

    connect(m_cmakeExecutable, SIGNAL(editingFinished()),
            this, SLOT(cmakeExecutableChanged()));
    connect(m_cmakeExecutable, SIGNAL(browsingFinished()),
            this, SLOT(cmakeExecutableChanged()));

    setTitle(tr("Choose CMake Executable"));
    updateErrorText();
}

void ChooseCMakePage::updateErrorText()
{
    CMakeTool* cmake = CMakeToolManager::defaultCMakeTool();
    QString cmakeExecutable = CMakeToolManager::defaultCMakeTool()->cmakeExecutable();
    if (cmake->isValid()) {
        m_cmakeLabel->setText(tr("Used CMake: %0.").arg(cmakeExecutable));
    } else {
        QString text = tr("Specify the path to the CMake executable. No CMake executable was found in the path.");
        if (!cmakeExecutable.isEmpty()) {
            text += QLatin1Char(' ');
            QFileInfo fi(cmakeExecutable);
            if (!fi.exists())
                text += tr("The CMake executable (%1) does not exist.").arg(cmakeExecutable);
            else if (!fi.isExecutable())
                text += tr("The path %1 is not an executable.").arg(cmakeExecutable);
            else
                text += tr("The path %1 is not a valid CMake executable.").arg(cmakeExecutable);
        }
        m_cmakeLabel->setText(text);
    }
}

void ChooseCMakePage::cmakeExecutableChanged()
{
    CMakeToolManager::setUserCmakePath(m_cmakeExecutable->path());
    updateErrorText();
    emit completeChanged();
}

bool ChooseCMakePage::isComplete() const
{
    return CMakeToolManager::defaultCMakeTool()->isValid();
}

ChooseCMakeWizard::ChooseCMakeWizard()
{
    addPage(new ChooseCMakePage(this));
    setWindowTitle(tr("Choose CMake Wizard"));
}

} // namespace CMakeProjectManager
