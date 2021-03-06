/**************************************************************************
**
** Copyright (c) 2014 BogDan Vatra <bog_dan_ro@yahoo.com>
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

#include "androidpackagecreationwidget.h"
#include "androidpackagecreationstep.h"
#include "androidconfigurations.h"
#include "androidcreatekeystorecertificate.h"
#include "androidmanager.h"
#include "androiddeploystep.h"
#include "androidglobal.h"
#include "ui_androidpackagecreationwidget.h"

#include <projectexplorer/project.h>
#include <projectexplorer/target.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <qmakeprojectmanager/qmakebuildconfiguration.h>
#include <qmakeprojectmanager/qmakestep.h>

#include <QTimer>

#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitinformation.h>

namespace Android {
namespace Internal {

using namespace ProjectExplorer;
using namespace QmakeProjectManager;

///////////////////////////// CheckModel /////////////////////////////

CheckModel::CheckModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void CheckModel::setAvailableItems(const QStringList &items)
{
    beginResetModel();
    m_availableItems = items;
    endResetModel();
}

void CheckModel::setCheckedItems(const QStringList &items)
{
    m_checkedItems = items;
    if (rowCount())
        emit dataChanged(index(0), index(rowCount()-1));
}

const QStringList &CheckModel::checkedItems()
{
    return m_checkedItems;
}

QVariant CheckModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();
    switch (role) {
    case Qt::CheckStateRole:
        return m_checkedItems.contains(m_availableItems.at(index.row())) ? Qt::Checked : Qt::Unchecked;
    case Qt::DisplayRole:
        return m_availableItems.at(index.row());
    }
    return QVariant();
}

void CheckModel::moveUp(int index)
{
    beginMoveRows(QModelIndex(), index, index, QModelIndex(), index - 1);
    const QString &item1 = m_availableItems[index];
    const QString &item2 = m_availableItems[index - 1];
    m_availableItems.swap(index, index - 1);
    index = m_checkedItems.indexOf(item1);
    int index2 = m_checkedItems.indexOf(item2);
    if (index > -1 && index2 > -1)
        m_checkedItems.swap(index, index2);
    endMoveRows();
}

bool CheckModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::CheckStateRole || !index.isValid())
        return false;
    if (value.toInt() == Qt::Checked)
        m_checkedItems.append(m_availableItems.at(index.row()));
    else
        m_checkedItems.removeAll(m_availableItems.at(index.row()));
    emit dataChanged(index, index);
    return true;
}

int CheckModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_availableItems.count();
}

Qt::ItemFlags CheckModel::flags(const QModelIndex &/*index*/) const
{
    return Qt::ItemIsSelectable|Qt::ItemIsUserCheckable|Qt::ItemIsEnabled;
}

///////////////////////////// AndroidPackageCreationWidget /////////////////////////////

AndroidPackageCreationWidget::AndroidPackageCreationWidget(AndroidPackageCreationStep *step)
    : ProjectExplorer::BuildStepConfigWidget(),
      m_step(step),
      m_ui(new Ui::AndroidPackageCreationWidget),
      m_fileSystemWatcher(new QFileSystemWatcher(this)),
      m_currentBuildConfiguration(0)
{
    m_qtLibsModel = new CheckModel(this);
    m_prebundledLibs = new CheckModel(this);

    m_ui->setupUi(this);
    m_ui->signingDebugWarningIcon->hide();
    m_ui->signingDebugWarningLabel->hide();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QTimer::singleShot(50, this, SLOT(initGui()));
    connect(m_step, SIGNAL(updateRequiredLibrariesModels()), SLOT(updateRequiredLibrariesModels()));
}

void AndroidPackageCreationWidget::initGui()
{
    updateAndroidProjectInfo();
    ProjectExplorer::Target *target = m_step->target();

    m_fileSystemWatcher->addPath(AndroidManager::dirPath(target).toString());
    m_fileSystemWatcher->addPath(AndroidManager::manifestPath(target).toString());
    m_fileSystemWatcher->addPath(AndroidManager::srcPath(target).toString());
    connect(m_fileSystemWatcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(updateAndroidProjectInfo()));
    connect(m_fileSystemWatcher, SIGNAL(fileChanged(QString)), this,
            SLOT(updateAndroidProjectInfo()));

    connect(m_ui->targetSDKComboBox, SIGNAL(activated(QString)), SLOT(setTargetSDK(QString)));
    connect(m_qtLibsModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), SLOT(setQtLibs(QModelIndex,QModelIndex)));
    connect(m_prebundledLibs, SIGNAL(dataChanged(QModelIndex,QModelIndex)), SLOT(setPrebundledLibs(QModelIndex,QModelIndex)));
    connect(m_ui->prebundledLibsListView, SIGNAL(activated(QModelIndex)), SLOT(prebundledLibSelected(QModelIndex)));
    connect(m_ui->prebundledLibsListView, SIGNAL(clicked(QModelIndex)), SLOT(prebundledLibSelected(QModelIndex)));
    connect(m_ui->upPushButton, SIGNAL(clicked()), SLOT(prebundledLibMoveUp()));
    connect(m_ui->downPushButton, SIGNAL(clicked()), SLOT(prebundledLibMoveDown()));
    connect(m_ui->readInfoPushButton, SIGNAL(clicked()), SLOT(readElfInfo()));

    m_ui->qtLibsListView->setModel(m_qtLibsModel);
    m_ui->prebundledLibsListView->setModel(m_prebundledLibs);
    m_ui->KeystoreLocationLineEdit->setText(m_step->keystorePath().toUserOutput());

    // Make the buildconfiguration emit a evironmentChanged() signal
    // TODO find a better way
    QmakeBuildConfiguration *bc = qobject_cast<QmakeBuildConfiguration *>(m_step->target()->activeBuildConfiguration());
    if (!bc)
        return;
    bool use = bc->useSystemEnvironment();
    bc->setUseSystemEnvironment(!use);
    bc->setUseSystemEnvironment(use);

    activeBuildConfigurationChanged();
    connect(m_step->target(), SIGNAL(activeBuildConfigurationChanged(ProjectExplorer::BuildConfiguration*)),
            this, SLOT(activeBuildConfigurationChanged()));
}

void AndroidPackageCreationWidget::updateSigningWarning()
{
    QmakeBuildConfiguration *bc = qobject_cast<QmakeBuildConfiguration *>(m_step->target()->activeBuildConfiguration());
    bool debug = bc && (bc->qmakeBuildConfiguration() & QtSupport::BaseQtVersion::DebugBuild);
    if (m_step->signPackage() && debug) {
        m_ui->signingDebugWarningIcon->setVisible(true);
        m_ui->signingDebugWarningLabel->setVisible(true);
    } else {
        m_ui->signingDebugWarningIcon->setVisible(false);
        m_ui->signingDebugWarningLabel->setVisible(false);
    }
}

void AndroidPackageCreationWidget::activeBuildConfigurationChanged()
{
    if (m_currentBuildConfiguration)
        disconnect(m_currentBuildConfiguration, SIGNAL(qmakeBuildConfigurationChanged()),
                   this, SLOT(updateSigningWarning()));
    updateSigningWarning();
    QmakeBuildConfiguration *bc = qobject_cast<QmakeBuildConfiguration *>(m_step->target()->activeBuildConfiguration());
    m_currentBuildConfiguration = bc;
    if (bc)
        connect(bc, SIGNAL(qmakeBuildConfigurationChanged()), this, SLOT(updateSigningWarning()));
                m_currentBuildConfiguration = bc;
}

void AndroidPackageCreationWidget::updateAndroidProjectInfo()
{
    ProjectExplorer::Target *target = m_step->target();
    m_ui->targetSDKComboBox->clear();

    int minApiLevel = 4;
    if (QtSupport::BaseQtVersion *qt = QtSupport::QtKitInformation::qtVersion(target->kit()))
        if (qt->qtVersion() >= QtSupport::QtVersionNumber(5, 0, 0))
            minApiLevel = 9;

    QStringList targets = AndroidConfigurations::instance().sdkTargets(minApiLevel);
    m_ui->targetSDKComboBox->addItems(targets);
    m_ui->targetSDKComboBox->setCurrentIndex(targets.indexOf(AndroidManager::buildTargetSDK(target)));

    m_qtLibsModel->setAvailableItems(AndroidManager::availableQtLibs(target));
    m_qtLibsModel->setCheckedItems(AndroidManager::qtLibs(target));
    m_prebundledLibs->setAvailableItems(AndroidManager::availablePrebundledLibs(target));
    m_prebundledLibs->setCheckedItems(AndroidManager::prebundledLibs(target));
}

void AndroidPackageCreationWidget::setTargetSDK(const QString &sdk)
{
    AndroidManager::setBuildTargetSDK(m_step->target(), sdk);
    QmakeBuildConfiguration *bc = qobject_cast<QmakeBuildConfiguration *>(m_step->target()->activeBuildConfiguration());
    if (!bc)
        return;
    QMakeStep *qs = bc->qmakeStep();
    if (!qs)
        return;

    qs->setForced(true);

    BuildManager::buildList(bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_CLEAN),
                  ProjectExplorerPlugin::displayNameForStepId(ProjectExplorer::Constants::BUILDSTEPS_CLEAN));
    BuildManager::appendStep(qs, ProjectExplorerPlugin::displayNameForStepId(ProjectExplorer::Constants::BUILDSTEPS_CLEAN));
    bc->setSubNodeBuild(0);
    // Make the buildconfiguration emit a evironmentChanged() signal
    // TODO find a better way
    bool use = bc->useSystemEnvironment();
    bc->setUseSystemEnvironment(!use);
    bc->setUseSystemEnvironment(use);
}

void AndroidPackageCreationWidget::setQtLibs(QModelIndex, QModelIndex)
{
    AndroidManager::setQtLibs(m_step->target(), m_qtLibsModel->checkedItems());
    AndroidDeployStep * const deployStep = AndroidGlobal::buildStep<AndroidDeployStep>(m_step->target()->activeDeployConfiguration());
    if (deployStep->deployAction() == AndroidDeployStep::DeployLocal
            || deployStep->deployAction() == AndroidDeployStep::BundleLibraries)
        AndroidManager::updateDeploymentSettings(m_step->target());
}

void AndroidPackageCreationWidget::setPrebundledLibs(QModelIndex, QModelIndex)
{
    AndroidManager::setPrebundledLibs(m_step->target(), m_prebundledLibs->checkedItems());
    AndroidDeployStep * const deployStep = AndroidGlobal::buildStep<AndroidDeployStep>(m_step->target()->activeDeployConfiguration());
    if (deployStep->deployAction() == AndroidDeployStep::DeployLocal
            || deployStep->deployAction() == AndroidDeployStep::BundleLibraries)
        AndroidManager::updateDeploymentSettings(m_step->target());
}

void AndroidPackageCreationWidget::prebundledLibSelected(const QModelIndex &index)
{
    m_ui->upPushButton->setEnabled(false);
    m_ui->downPushButton->setEnabled(false);
    if (!index.isValid())
        return;
    if (index.row() > 0)
        m_ui->upPushButton->setEnabled(true);
    if (index.row() < m_prebundledLibs->rowCount(QModelIndex()) - 1)
        m_ui->downPushButton->setEnabled(true);
}

void AndroidPackageCreationWidget::prebundledLibMoveUp()
{
    const QModelIndex &index = m_ui->prebundledLibsListView->currentIndex();
    if (index.isValid())
        m_prebundledLibs->moveUp(index.row());
}

void AndroidPackageCreationWidget::prebundledLibMoveDown()
{
    const QModelIndex &index = m_ui->prebundledLibsListView->currentIndex();
    if (index.isValid())
        m_prebundledLibs->moveUp(index.row() + 1);
}

void AndroidPackageCreationWidget::updateRequiredLibrariesModels()
{
    m_qtLibsModel->setCheckedItems(AndroidManager::qtLibs(m_step->target()));
    m_prebundledLibs->setCheckedItems(AndroidManager::prebundledLibs(m_step->target()));
}

void AndroidPackageCreationWidget::readElfInfo()
{
    m_step->checkRequiredLibraries();
}

QString AndroidPackageCreationWidget::summaryText() const
{
    return tr("<b>Package configurations</b>");
}

QString AndroidPackageCreationWidget::displayName() const
{
    return m_step->displayName();
}

void AndroidPackageCreationWidget::setCertificates()
{
    QAbstractItemModel *certificates = m_step->keystoreCertificates();
    m_ui->signPackageCheckBox->setChecked(certificates);
    m_ui->certificatesAliasComboBox->setModel(certificates);
}

void AndroidPackageCreationWidget::on_signPackageCheckBox_toggled(bool checked)
{
    m_step->setSignPackage(checked);
    updateSigningWarning();
    if (!checked)
        return;
    if (!m_step->keystorePath().isEmpty())
        setCertificates();
}

void AndroidPackageCreationWidget::on_KeystoreCreatePushButton_clicked()
{
    AndroidCreateKeystoreCertificate d;
    if (d.exec() != QDialog::Accepted)
        return;
    m_ui->KeystoreLocationLineEdit->setText(d.keystoreFilePath().toUserOutput());
    m_step->setKeystorePath(d.keystoreFilePath());
    m_step->setKeystorePassword(d.keystorePassword());
    m_step->setCertificateAlias(d.certificateAlias());
    m_step->setCertificatePassword(d.certificatePassword());
    setCertificates();
}

void AndroidPackageCreationWidget::on_KeystoreLocationPushButton_clicked()
{
    Utils::FileName keystorePath = m_step->keystorePath();
    if (keystorePath.isEmpty())
        keystorePath = Utils::FileName::fromString(QDir::homePath());
    Utils::FileName file = Utils::FileName::fromString(QFileDialog::getOpenFileName(this, tr("Select keystore file"), keystorePath.toString(), tr("Keystore files (*.keystore *.jks)")));
    if (file.isEmpty())
        return;
    m_ui->KeystoreLocationLineEdit->setText(file.toUserOutput());
    m_step->setKeystorePath(file);
    m_ui->signPackageCheckBox->setChecked(false);
}

void AndroidPackageCreationWidget::on_certificatesAliasComboBox_activated(const QString &alias)
{
    if (alias.length())
        m_step->setCertificateAlias(alias);
}

void AndroidPackageCreationWidget::on_certificatesAliasComboBox_currentIndexChanged(const QString &alias)
{
    if (alias.length())
        m_step->setCertificateAlias(alias);
}

void AndroidPackageCreationWidget::on_openPackageLocationCheckBox_toggled(bool checked)
{
    m_step->setOpenPackageLocation(checked);
}

} // namespace Internal
} // namespace Android
