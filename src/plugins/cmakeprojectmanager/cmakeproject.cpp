/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
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

#include "cmakeproject.h"

#include "cmakebuildconfiguration.h"
#include "cmakeprojectconstants.h"
#include "cmakeprojectnodes.h"
#include "cmakerunconfiguration.h"
#include "cmakekitinformation.h"
#include "cmakeappwizard.h"
#include "makestep.h"
#include "cmaketoolmanager.h"
#include "argumentslineedit.h"
#include "generatorinfo.h"

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/headerpath.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildtargetinfo.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/target.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/deploymentdata.h>
#include <projectexplorer/projectmacroexpander.h>
#include <qtsupport/customexecutablerunconfiguration.h>
#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitinformation.h>
#include <qtsupport/uicodemodelsupport.h>
#include <cpptools/cppmodelmanagerinterface.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/pathchooser.h>
#include <coreplugin/icore.h>
#include <coreplugin/infobar.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/variablemanager.h>
#include <coreplugin/modemanager.h>

#include <QDebug>
#include <QDir>
#include <QFormLayout>
#include <QFileSystemWatcher>
#include <QLabel>

using namespace CMakeProjectManager;
using namespace CMakeProjectManager::Internal;
using namespace ProjectExplorer;

// QtCreator CMake Generator wishlist:
// Which make targets we need to build to get all executables
// What is the make we need to call
// What is the actual compiler executable
// DEFINES

// Open Questions
// Who sets up the environment for cl.exe ? INCLUDEPATH and so on

/*!
  \class CMakeProject
*/
CMakeProject::CMakeProject(CMakeManager *manager, const QString &fileName)
    : m_manager(manager),
      m_activeTarget(0),
      m_fileName(fileName),
      m_rootNode(new CMakeProjectNode(fileName)),
      m_watcher(new QFileSystemWatcher(this))
{
    setProjectContext(Core::Context(CMakeProjectManager::Constants::PROJECTCONTEXT));
    setProjectLanguages(Core::Context(ProjectExplorer::Constants::LANG_CXX));

    m_projectName = QFileInfo(fileName).absoluteDir().dirName();

    m_file = new CMakeFile(this, fileName);

    connect(this, SIGNAL(buildTargetsChanged()),
            this, SLOT(updateConfigurations()));

    connect(m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(fileChanged(QString)));
}

CMakeProject::~CMakeProject()
{
    m_codeModelFuture.cancel();
    delete m_rootNode;
}

void CMakeProject::fileChanged(const QString &fileName)
{
    Q_UNUSED(fileName)

    parseCMakeLists();
}

void CMakeProject::changeActiveBuildConfiguration(ProjectExplorer::BuildConfiguration *bc)
{
    if (!bc)
        return;

    //@TODO handle no cmake case
    ICMakeTool* cmake = CMakeToolManager::cmakeToolForKit(bc->target()->kit());
    cmake->runCMake(bc->target());
    connect(cmake,SIGNAL(cmakeFinished(ProjectExplorer::Target*)),this,SLOT(parseCMakeLists(ProjectExplorer::Target*)),Qt::UniqueConnection);
}

void CMakeProject::activeTargetWasChanged(Target *target)
{
    if (m_activeTarget) {
        disconnect(m_activeTarget, SIGNAL(activeBuildConfigurationChanged(ProjectExplorer::BuildConfiguration*)),
                   this, SLOT(changeActiveBuildConfiguration(ProjectExplorer::BuildConfiguration*)));
    }

    m_activeTarget = target;

    if (!m_activeTarget)
        return;

    connect(m_activeTarget, SIGNAL(activeBuildConfigurationChanged(ProjectExplorer::BuildConfiguration*)),
            this, SLOT(changeActiveBuildConfiguration(ProjectExplorer::BuildConfiguration*)));

    changeActiveBuildConfiguration(m_activeTarget->activeBuildConfiguration());
}

void CMakeProject::changeBuildDirectory(CMakeBuildConfiguration *bc, const QString &newBuildDirectory)
{
    bc->setBuildDirectory(Utils::FileName::fromString(newBuildDirectory));
    parseCMakeLists();
}

QString CMakeProject::shadowBuildDirectory(const QString &projectFilePath, const Kit *k, const QString &bcName)
{
    if (projectFilePath.isEmpty())
        return QString();
    QFileInfo info(projectFilePath);

    const QString projectName = QFileInfo(info.absolutePath()).fileName();
    ProjectExplorer::ProjectMacroExpander expander(projectFilePath, projectName, k, bcName);
    QDir projectDir = QDir(projectDirectory(projectFilePath));

    if(hasInSourceBuild(projectDir.absolutePath()))
        return projectDir.absolutePath();

    QString buildPath = Utils::expandMacros(Core::DocumentManager::buildDirectory(), &expander);
    return QDir::cleanPath(projectDir.absoluteFilePath(buildPath));
}

bool CMakeProject::parseCMakeLists(Target *t)
{
    qDebug()<<"Running parseCMakeLists";

    if (!activeTarget() ||
            !activeTarget()->activeBuildConfiguration()) {
        return false;
    }

    //parse only if active target
    if(t && t != activeTarget())
        return false;

    CMakeBuildConfiguration *activeBC = static_cast<CMakeBuildConfiguration *>(activeTarget()->activeBuildConfiguration());
    foreach (Core::IDocument *document, Core::EditorManager::documentModel()->openedDocuments())
        if (isProjectFile(document->filePath()))
            document->infoBar()->removeInfo("CMakeEditor.RunCMake");

    // Find cbp file
    QString cbpFile = CMakeManager::findCbpFile(activeBC->buildDirectory().toString());

    if (cbpFile.isEmpty()) {
        emit buildTargetsChanged();
        return false;
    }

    // setFolderName
    m_rootNode->setDisplayName(QFileInfo(cbpFile).completeBaseName());
    CMakeCbpParser cbpparser;
    // Parsing
    //qDebug()<<"Parsing file "<<cbpFile;
    if (!cbpparser.parseCbpFile(cbpFile)) {
        // TODO report error
        emit buildTargetsChanged();
        return false;
    }

    foreach (const QString &file, m_watcher->files())
        if (file != cbpFile)
            m_watcher->removePath(file);

    // how can we ensure that it is completely written?
    m_watcher->addPath(cbpFile);

    m_projectName = cbpparser.projectName();
    m_rootNode->setDisplayName(cbpparser.projectName());

    //qDebug()<<"Building Tree";
    QList<ProjectExplorer::FileNode *> fileList = cbpparser.fileList();
    QSet<QString> projectFiles;
    if (cbpparser.hasCMakeFiles()) {
        fileList.append(cbpparser.cmakeFileList());
        foreach (const ProjectExplorer::FileNode *node, cbpparser.cmakeFileList())
            projectFiles.insert(node->path());
    } else {
        // Manually add the CMakeLists.txt file
        QString cmakeListTxt = projectDirectory() + QLatin1String("/CMakeLists.txt");
        bool generated = false;
        fileList.append(new ProjectExplorer::FileNode(cmakeListTxt, ProjectExplorer::ProjectFileType, generated));
        projectFiles.insert(cmakeListTxt);
    }

    m_watchedFiles = projectFiles;

    m_files.clear();
    foreach (ProjectExplorer::FileNode *fn, fileList)
        m_files.append(fn->path());
    m_files.sort();

    buildTree(m_rootNode, fileList);

    //qDebug()<<"Adding Targets";
    m_buildTargets = cbpparser.buildTargets();
    //        qDebug()<<"Printing targets";
    //        foreach (CMakeBuildTarget ct, m_buildTargets) {
    //            qDebug()<<ct.title<<" with executable:"<<ct.executable;
    //            qDebug()<<"WD:"<<ct.workingDirectory;
    //            qDebug()<<ct.makeCommand<<ct.makeCleanCommand;
    //            qDebug()<<"";
    //        }

    updateApplicationAndDeploymentTargets();

    createUiCodeModelSupport();

    Kit *k = activeTarget()->kit();
    ToolChain *tc = ProjectExplorer::ToolChainKitInformation::toolChain(k);
    if (!tc) {
        emit buildTargetsChanged();
        emit fileListChanged();
        return true;
    }

    QStringList cxxflags;
    bool found = false;
    foreach (const CMakeBuildTarget &buildTarget, m_buildTargets) {
        QString makeCommand = QDir::fromNativeSeparators(buildTarget.makeCommand);
        int startIndex = makeCommand.indexOf(QLatin1Char('\"'));
        int endIndex = makeCommand.indexOf(QLatin1Char('\"'), startIndex + 1);
        if (startIndex == -1 || endIndex == -1)
            continue;
        startIndex += 1;
        QString makefile = makeCommand.mid(startIndex, endIndex - startIndex);
        int slashIndex = makefile.lastIndexOf(QLatin1Char('/'));
        makefile.truncate(slashIndex);
        makefile.append(QLatin1String("/CMakeFiles/") + buildTarget.title + QLatin1String(".dir/flags.make"));
        QFile file(makefile);
        if (file.exists()) {
            file.open(QIODevice::ReadOnly | QIODevice::Text);
            QTextStream stream(&file);
            while (!stream.atEnd()) {
                QString line = stream.readLine().trimmed();
                if (line.startsWith(QLatin1String("CXX_FLAGS ="))) {
                    // Skip past =
                    cxxflags = line.mid(11).trimmed().split(QLatin1Char(' '), QString::SkipEmptyParts);
                    found = true;
                    break;
                }
            }
        }
        if (found)
            break;
    }
    // Attempt to find build.ninja file and obtain FLAGS (CXX_FLAGS) from there if no suitable flags.make were
    // found
    if (!found && !cbpparser.buildTargets().isEmpty()) {
        // Get "all" target's working directory
        QString buildNinjaFile = QDir::fromNativeSeparators(cbpparser.buildTargets().at(0).workingDirectory);
        buildNinjaFile += QLatin1String("/build.ninja");
        QFile buildNinja(buildNinjaFile);
        if (buildNinja.exists()) {
            buildNinja.open(QIODevice::ReadOnly | QIODevice::Text);
            QTextStream stream(&buildNinja);
            bool cxxFound = false;
            while (!stream.atEnd()) {
                QString line = stream.readLine().trimmed();
                // Look for a build rule which invokes CXX_COMPILER
                if (line.startsWith(QLatin1String("build"))) {
                    cxxFound = line.indexOf(QLatin1String("CXX_COMPILER")) != -1;
                } else if (cxxFound && line.startsWith(QLatin1String("FLAGS ="))) {
                    // Skip past =
                    cxxflags = line.mid(7).trimmed().split(QLatin1Char(' '), QString::SkipEmptyParts);
                    break;
                }
            }
        }
    }

    CppTools::CppModelManagerInterface *modelmanager =
            CppTools::CppModelManagerInterface::instance();
    if (modelmanager) {
        CppTools::CppModelManagerInterface::ProjectInfo pinfo = modelmanager->projectInfo(this);
        pinfo.clearProjectParts();

        CppTools::ProjectPart::Ptr part(new CppTools::ProjectPart);
        part->project = this;
        part->displayName = displayName();
        part->projectFile = projectFilePath();

        part->evaluateToolchain(tc,
                                cxxflags,
                                cxxflags,
                                SysRootKitInformation::sysRoot(k));

        // This explicitly adds -I. to the include paths
        part->includePaths += projectDirectory();
        part->includePaths += cbpparser.includeFiles();
        part->defines += cbpparser.defines();

        CppTools::ProjectFileAdder adder(part->files);
        foreach (const QString &file, m_files)
            adder.maybeAdd(file);

        pinfo.appendProjectPart(part);
        m_codeModelFuture.cancel();
        m_codeModelFuture = modelmanager->updateProjectInfo(pinfo);

        setProjectLanguage(ProjectExplorer::Constants::LANG_CXX, !part->files.isEmpty());
    }

    emit buildTargetsChanged();
    emit fileListChanged();

    return true;
}

KitMatcher *CMakeProject::createRequiredKitMatcher() const
{
    return new CMakeKitMatcher();
}

bool CMakeProject::supportsKit(Kit *k, QString *errorMessage) const
{
    CMakeKitMatcher matcher;
    if(matcher.matches(k))
        return true;
    if(errorMessage)
        *errorMessage = QLatin1String("Could not find compatible CMake Generators");
    return false;
}

bool CMakeProject::supportsNoTargetPanel() const
{
    return true;
}

bool CMakeProject::needsConfiguration() const
{
    return targets().isEmpty();
}

bool CMakeProject::isProjectFile(const QString &fileName)
{
    return m_watchedFiles.contains(fileName);
}

bool CMakeProject::hasInSourceBuild() const
{
    return hasInSourceBuild(projectDirectory());
}

bool CMakeProject::hasInSourceBuild(const QString &sourcePath)
{
    QFileInfo fi(sourcePath + QLatin1String("/CMakeCache.txt"));
    if (fi.exists())
        return true;
    return false;
}

QList<CMakeBuildTarget> CMakeProject::buildTargets() const
{
    return m_buildTargets;
}

QStringList CMakeProject::buildTargetTitles(bool runnable) const
{
    QStringList results;
    foreach (const CMakeBuildTarget &ct, m_buildTargets) {
        if (runnable && (ct.executable.isEmpty() || ct.library))
            continue;
        results << ct.title;
    }
    return results;
}

bool CMakeProject::hasBuildTarget(const QString &title) const
{
    foreach (const CMakeBuildTarget &ct, m_buildTargets) {
        if (ct.title == title)
            return true;
    }
    return false;
}

void CMakeProject::gatherFileNodes(ProjectExplorer::FolderNode *parent, QList<ProjectExplorer::FileNode *> &list)
{
    foreach (ProjectExplorer::FolderNode *folder, parent->subFolderNodes())
        gatherFileNodes(folder, list);
    foreach (ProjectExplorer::FileNode *file, parent->fileNodes())
        list.append(file);
}

bool sortNodesByPath(Node *a, Node *b)
{
    return a->path() < b->path();
}

void CMakeProject::buildTree(CMakeProjectNode *rootNode, QList<ProjectExplorer::FileNode *> newList)
{
    // Gather old list
    QList<ProjectExplorer::FileNode *> oldList;
    gatherFileNodes(rootNode, oldList);
    qSort(oldList.begin(), oldList.end(), sortNodesByPath);
    qSort(newList.begin(), newList.end(), sortNodesByPath);

    // generate added and deleted list
    QList<ProjectExplorer::FileNode *>::const_iterator oldIt  = oldList.constBegin();
    QList<ProjectExplorer::FileNode *>::const_iterator oldEnd = oldList.constEnd();
    QList<ProjectExplorer::FileNode *>::const_iterator newIt  = newList.constBegin();
    QList<ProjectExplorer::FileNode *>::const_iterator newEnd = newList.constEnd();

    QList<ProjectExplorer::FileNode *> added;
    QList<ProjectExplorer::FileNode *> deleted;

    while (oldIt != oldEnd && newIt != newEnd) {
        if ( (*oldIt)->path() == (*newIt)->path()) {
            delete *newIt;
            ++oldIt;
            ++newIt;
        } else if ((*oldIt)->path() < (*newIt)->path()) {
            deleted.append(*oldIt);
            ++oldIt;
        } else {
            added.append(*newIt);
            ++newIt;
        }
    }

    while (oldIt != oldEnd) {
        deleted.append(*oldIt);
        ++oldIt;
    }

    while (newIt != newEnd) {
        added.append(*newIt);
        ++newIt;
    }

    // add added nodes
    foreach (ProjectExplorer::FileNode *fn, added) {
        //        qDebug()<<"added"<<fn->path();
        // Get relative path to rootNode
        QString parentDir = QFileInfo(fn->path()).absolutePath();
        ProjectExplorer::FolderNode *folder = findOrCreateFolder(rootNode, parentDir);
        rootNode->addFileNodes(QList<ProjectExplorer::FileNode *>()<< fn, folder);
    }

    // remove old file nodes and check whether folder nodes can be removed
    foreach (ProjectExplorer::FileNode *fn, deleted) {
        ProjectExplorer::FolderNode *parent = fn->parentFolderNode();
        //        qDebug()<<"removed"<<fn->path();
        rootNode->removeFileNodes(QList<ProjectExplorer::FileNode *>() << fn, parent);
        // Check for empty parent
        while (parent->subFolderNodes().isEmpty() && parent->fileNodes().isEmpty()) {
            ProjectExplorer::FolderNode *grandparent = parent->parentFolderNode();
            rootNode->removeFolderNodes(QList<ProjectExplorer::FolderNode *>() << parent, grandparent);
            parent = grandparent;
            if (parent == rootNode)
                break;
        }
    }
}

ProjectExplorer::FolderNode *CMakeProject::findOrCreateFolder(CMakeProjectNode *rootNode, QString directory)
{
    QString relativePath = QDir(QFileInfo(rootNode->path()).path()).relativeFilePath(directory);
    QStringList parts = relativePath.split(QLatin1Char('/'), QString::SkipEmptyParts);
    ProjectExplorer::FolderNode *parent = rootNode;
    QString path = QFileInfo(rootNode->path()).path();
    foreach (const QString &part, parts) {
        path += QLatin1Char('/');
        path += part;
        // Find folder in subFolders
        bool found = false;
        foreach (ProjectExplorer::FolderNode *folder, parent->subFolderNodes()) {
            if (folder->path() == path) {
                // yeah found something :)
                parent = folder;
                found = true;
                break;
            }
        }
        if (!found) {
            // No FolderNode yet, so create it
            ProjectExplorer::FolderNode *tmp = new ProjectExplorer::FolderNode(path);
            tmp->setDisplayName(part);
            rootNode->addFolderNodes(QList<ProjectExplorer::FolderNode *>() << tmp, parent);
            parent = tmp;
        }
    }
    return parent;
}

QString CMakeProject::displayName() const
{
    return m_projectName;
}

Core::Id CMakeProject::id() const
{
    return Core::Id(Constants::CMAKEPROJECT_ID);
}

Core::IDocument *CMakeProject::document() const
{
    return m_file;
}

CMakeManager *CMakeProject::projectManager() const
{
    return m_manager;
}

ProjectExplorer::ProjectNode *CMakeProject::rootProjectNode() const
{
    return m_rootNode;
}


QStringList CMakeProject::files(FilesMode fileMode) const
{
    Q_UNUSED(fileMode)
    return m_files;
}

bool CMakeProject::fromMap(const QVariantMap &map)
{
    if (!Project::fromMap(map))
        return false;

    //make sure there is always a cmake available
    if(!CMakeToolManager::defaultCMakeTool()->isValid()) {
        ChooseCMakeWizard wiz;
        if(wiz.exec() != QDialog::Accepted)
            return false;

        if(!CMakeToolManager::defaultCMakeTool()->isValid())
            return false;
    }

    //if there is no user file, just leave a empty project
    //the configure project pane will pup up for the rescue
    bool hasUserFile = activeTarget();
    if ( hasUserFile ) {
        // We have a user file, but we could still be missing the cbp file
        // or simply run createXml with the saved settings
        CMakeBuildConfiguration *activeBC = qobject_cast<CMakeBuildConfiguration *>(activeTarget()->activeBuildConfiguration());
        if (!activeBC)
            return true; //give the user a chance to setup a cmake target

        //@TODO handle no cmake case
        ICMakeTool* cmake = CMakeToolManager::cmakeToolForKit(activeBC->target()->kit());
        cmake->runCMake(activeBC->target());
        connect(cmake,SIGNAL(cmakeFinished(ProjectExplorer::Target*)),this,SLOT(parseCMakeLists(ProjectExplorer::Target*)),Qt::UniqueConnection);
    }

    m_activeTarget = activeTarget();
    if (m_activeTarget)
        connect(m_activeTarget, SIGNAL(activeBuildConfigurationChanged(ProjectExplorer::BuildConfiguration*)),
                this, SLOT(changeActiveBuildConfiguration(ProjectExplorer::BuildConfiguration*)));

    connect(this, SIGNAL(activeTargetChanged(ProjectExplorer::Target*)),
            this, SLOT(activeTargetWasChanged(ProjectExplorer::Target*)));

    return true;
}

bool CMakeProject::setupTarget(Target *t)
{
    t->updateDefaultBuildConfigurations();
    t->updateDefaultDeployConfigurations();

    return true;
}

CMakeBuildTarget CMakeProject::buildTargetForTitle(const QString &title)
{
    foreach (const CMakeBuildTarget &ct, m_buildTargets)
        if (ct.title == title)
            return ct;
    return CMakeBuildTarget();
}

QString CMakeProject::uiHeaderFile(const QString &uiFile)
{
    QFileInfo fi(uiFile);
    Utils::FileName project = Utils::FileName::fromString(projectDirectory());
    Utils::FileName baseDirectory = Utils::FileName::fromString(fi.absolutePath());

    while (baseDirectory.isChildOf(project)) {
        Utils::FileName cmakeListsTxt = baseDirectory;
        cmakeListsTxt.appendPath(QLatin1String("CMakeLists.txt"));
        if (cmakeListsTxt.toFileInfo().exists())
            break;
        QDir dir(baseDirectory.toString());
        dir.cdUp();
        baseDirectory = Utils::FileName::fromString(dir.absolutePath());
    }

    QDir srcDirRoot = QDir(project.toString());
    QString relativePath = srcDirRoot.relativeFilePath(baseDirectory.toString());
    QDir buildDir = QDir(activeTarget()->activeBuildConfiguration()->buildDirectory().toString());
    QString uiHeaderFilePath = buildDir.absoluteFilePath(relativePath);
    uiHeaderFilePath += QLatin1String("/ui_");
    uiHeaderFilePath += fi.completeBaseName();
    uiHeaderFilePath += QLatin1String(".h");

    return QDir::cleanPath(uiHeaderFilePath);
}

void CMakeProject::updateConfigurations()
{
    foreach (Target *t, targets())
        updateConfigurations(t);
}

void CMakeProject::updateConfigurations(Target *t)
{
    //t->updateDefaultDeployConfigurations();
    t->updateDefaultRunConfigurations();

    if (t->runConfigurations().isEmpty()) {
        // Oh no, no run configuration,
        // create a custom executable run configuration
        t->addRunConfiguration(new QtSupport::CustomExecutableRunConfiguration(t));
    }

#if 0
    // *Update* runconfigurations:
    QMultiMap<QString, CMakeRunConfiguration*> existingRunConfigurations;
    QList<ProjectExplorer::RunConfiguration *> toRemove;
    foreach (ProjectExplorer::RunConfiguration *rc, t->runConfigurations()) {
        if (CMakeRunConfiguration* cmakeRC = qobject_cast<CMakeRunConfiguration *>(rc))
            existingRunConfigurations.insert(cmakeRC->title(), cmakeRC);
        QtSupport::CustomExecutableRunConfiguration *ceRC =
                qobject_cast<QtSupport::CustomExecutableRunConfiguration *>(rc);
        if (ceRC && !ceRC->isConfigured())
            toRemove << rc;
    }

    foreach (const CMakeBuildTarget &ct, buildTargets()) {
        if (ct.library)
            continue;
        if (ct.executable.isEmpty())
            continue;
        QList<CMakeRunConfiguration *> list = existingRunConfigurations.values(ct.title);
        if (!list.isEmpty()) {
            // Already exists, so override the settings...
            foreach (CMakeRunConfiguration *rc, list) {
                rc->setExecutable(ct.executable);
                rc->setBaseWorkingDirectory(ct.workingDirectory);
                rc->setEnabled(true);
            }
            existingRunConfigurations.remove(ct.title);
        } else {
            // Does not exist yet
            Core::Id id = CMakeRunConfigurationFactory::idFromBuildTarget(ct.title);
            CMakeRunConfiguration *rc = new CMakeRunConfiguration(t, id, ct.executable,
                                                                  ct.workingDirectory, ct.title);
            t->addRunConfiguration(rc);
        }
    }
    QMultiMap<QString, CMakeRunConfiguration *>::const_iterator it =
            existingRunConfigurations.constBegin();
    for ( ; it != existingRunConfigurations.constEnd(); ++it) {
        CMakeRunConfiguration *rc = it.value();
        // The executables for those runconfigurations aren't build by the current buildconfiguration
        // We just set a disable flag and show that in the display name
        rc->setEnabled(false);
        // removeRunConfiguration(rc);
    }

    foreach (ProjectExplorer::RunConfiguration *rc, toRemove)
        t->removeRunConfiguration(rc);

    if (t->runConfigurations().isEmpty()) {
        // Oh no, no run configuration,
        // create a custom executable run configuration
        t->addRunConfiguration(new QtSupport::CustomExecutableRunConfiguration(t));
    }
#endif
}

void CMakeProject::updateApplicationAndDeploymentTargets()
{
    Target *t = activeTarget();

    QFile deploymentFile;
    QTextStream deploymentStream;
    QString deploymentPrefix;
    QDir sourceDir;

    sourceDir.setPath(t->project()->projectDirectory());
    deploymentFile.setFileName(sourceDir.filePath(QLatin1String("QtCreatorDeployment.txt")));
    if (deploymentFile.open(QFile::ReadOnly | QFile::Text)) {
        deploymentStream.setDevice(&deploymentFile);
        deploymentPrefix = deploymentStream.readLine();
        if (!deploymentPrefix.endsWith(QLatin1Char('/')))
            deploymentPrefix.append(QLatin1Char('/'));
    }

    BuildTargetInfoList appTargetList;
    DeploymentData deploymentData;
    QDir buildDir(t->activeBuildConfiguration()->buildDirectory().toString());
    foreach (const CMakeBuildTarget &ct, m_buildTargets) {
        if (ct.executable.isEmpty())
            continue;

        deploymentData.addFile(ct.executable, deploymentPrefix + buildDir.relativeFilePath(QFileInfo(ct.executable).dir().path()), DeployableFile::TypeExecutable);
        if (!ct.library) {
            // TODO: Put a path to corresponding .cbp file into projectFilePath?
            appTargetList.list << BuildTargetInfo(ct.executable, ct.executable);
        }
    }

    QString absoluteSourcePath = sourceDir.absolutePath();
    if (!absoluteSourcePath.endsWith(QLatin1Char('/')))
        absoluteSourcePath.append(QLatin1Char('/'));
    while (!deploymentStream.atEnd()) {
        QStringList file = deploymentStream.readLine().split(QLatin1Char(':'));
        deploymentData.addFile(absoluteSourcePath + file.at(0), deploymentPrefix + file.at(1));
    }

    t->setApplicationTargets(appTargetList);
    t->setDeploymentData(deploymentData);
}

void CMakeProject::createUiCodeModelSupport()
{
    QHash<QString, QString> uiFileHash;

    // Find all ui files
    foreach (const QString &uiFile, m_files) {
        if (uiFile.endsWith(QLatin1String(".ui")))
            uiFileHash.insert(uiFile, uiHeaderFile(uiFile));
    }

    QtSupport::UiCodeModelManager::update(this, uiFileHash);
}

// CMakeFile

CMakeFile::CMakeFile(CMakeProject *parent, QString fileName)
    : Core::IDocument(parent), m_project(parent)
{
    setFilePath(fileName);
}

bool CMakeFile::save(QString *errorString, const QString &fileName, bool autoSave)
{
    // Once we have an texteditor open for this file, we probably do
    // need to implement this, don't we.
    Q_UNUSED(errorString)
    Q_UNUSED(fileName)
    Q_UNUSED(autoSave)
    return false;
}

QString CMakeFile::defaultPath() const
{
    return QString();
}

QString CMakeFile::suggestedFileName() const
{
    return QString();
}

QString CMakeFile::mimeType() const
{
    return QLatin1String(Constants::CMAKEMIMETYPE);
}


bool CMakeFile::isModified() const
{
    return false;
}

bool CMakeFile::isSaveAsAllowed() const
{
    return false;
}

Core::IDocument::ReloadBehavior CMakeFile::reloadBehavior(ChangeTrigger state, ChangeType type) const
{
    Q_UNUSED(state)
    Q_UNUSED(type)
    return BehaviorSilent;
}

bool CMakeFile::reload(QString *errorString, ReloadFlag flag, ChangeType type)
{
    Q_UNUSED(errorString)
    Q_UNUSED(flag)
    Q_UNUSED(type)
    return true;
}

CMakeBuildSettingsWidget::CMakeBuildSettingsWidget(CMakeBuildConfiguration *bc) : m_buildConfiguration(bc)
{
    QFormLayout *fl = new QFormLayout(this);
    fl->setContentsMargins(20, -1, 0, -1);
    fl->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    setLayout(fl);

    bool inSource = CMakeProject::hasInSourceBuild(bc->target()->project()->projectDirectory());
    if(inSource) {
        QLabel* inSourceLabel = new QLabel(this);
        inSourceLabel->setWordWrap(true);
        inSourceLabel->setText(tr("Qt Creator has detected an <b>in-source-build in %1</b> "
                                  "which prevents shadow builds. Qt Creator will not allow you to change the build directory. "
                                  "If you want a shadow build, clean your source directory and re-open the project.")
                               .arg(bc->target()->project()->projectDirectory()));
        fl->addRow(inSourceLabel);
    }

    if(!inSource) {
        m_pathChooser = new Utils::PathChooser(this);
        m_pathChooser->setPath(m_buildConfiguration->rawBuildDirectory().toString());
        fl->addRow(tr("Build directory:"), m_pathChooser);

        connect(m_pathChooser->lineEdit(),SIGNAL(editingFinished()),this,SLOT(onBuilddirChanged()));
    }

    m_userArguments = new ArgumentsLineEdit(this);
    fl->addRow(tr("CMake arguments:"),m_userArguments);
    m_userArguments->setText(Utils::QtcProcess::joinArgs(m_buildConfiguration->arguments()));
    //m_userArguments->setHistoryCompleter(QLatin1String("CMakeArgumentsLineEdit"));
    connect(m_userArguments,SIGNAL(editingFinished()),this,SLOT(onArgumentsChanged()));


    m_generatorBox = new QComboBox(this);
    ICMakeTool* cmake = CMakeToolManager::cmakeToolForKit(bc->target()->kit());
    QList<GeneratorInfo> infos = GeneratorInfo::generatorInfosFor(bc->target()->kit(),
                                                                  cmake->hasCodeBlocksNinjaGenerator() ? GeneratorInfo::OfferNinja : GeneratorInfo::NoNinja,
                                                                  CMakeManager::preferNinja(),
                                                                  cmake->hasCodeBlocksMsvcGenerator());

    int idx = -1;
    QByteArray cachedGenerator = bc->generator();
    for(int i = 0; i < infos.size(); i++) {
        const GeneratorInfo &info = infos.at(i);
        m_generatorBox->addItem(info.displayName(), qVariantFromValue(info));
        if(info.generator() == cachedGenerator) idx = i;
    }

    if(idx >= 0)
        m_generatorBox->setCurrentIndex(idx);
    connect(m_generatorBox,SIGNAL(currentIndexChanged(int)),this,SLOT(onGeneratorSelected()));
    fl->addRow(tr("Generator:"),m_generatorBox);

    setDisplayName(tr("CMake"));
}

void CMakeBuildSettingsWidget::openChangeBuildDirectoryDialog()
{
#if 0
    CMakeProject *project = static_cast<CMakeProject *>(m_buildConfiguration->target()->project());
    CMakeBuildInfo info(m_buildConfiguration);
    CMakeOpenProjectWizard copw(project->projectManager(), CMakeOpenProjectWizard::ChangeDirectory,
                                &info);
    if (copw.exec() == QDialog::Accepted) {
        project->changeBuildDirectory(m_buildConfiguration, copw.buildDirectory());
        m_buildConfiguration->setUseNinja(copw.useNinja());
        m_pathLineEdit->setText(m_buildConfiguration->rawBuildDirectory().toString());
    }
#endif
}

void CMakeBuildSettingsWidget::onArgumentsChanged()
{
    if(!m_userArguments->isValid())
        return;

    QStringList args = Utils::QtcProcess::splitArgs(m_userArguments->text());

    if(m_buildConfiguration->arguments() != args)
        m_buildConfiguration->setArguments(args);
}

void CMakeBuildSettingsWidget::onBuilddirChanged()
{
    qDebug()<<"Changing builddir to: "<<m_pathChooser->fileName().toString();
    m_buildConfiguration->setBuildDirectory(m_pathChooser->fileName());
}

void CMakeBuildSettingsWidget::onGeneratorSelected()
{
    int curr = m_generatorBox->currentIndex();
    GeneratorInfo info = m_generatorBox->itemData(curr).value<GeneratorInfo>();

    m_buildConfiguration->setUseNinja(info.isNinja());
}

/////
// CMakeCbpParser
////

bool CMakeCbpParser::parseCbpFile(const QString &fileName)
{
    QFile fi(fileName);
    if (fi.exists() && fi.open(QFile::ReadOnly)) {
        setDevice(&fi);

        while (!atEnd()) {
            readNext();
            if (name() == QLatin1String("CodeBlocks_project_file"))
                parseCodeBlocks_project_file();
            else if (isStartElement())
                parseUnknownElement();
        }
        fi.close();
        m_includeFiles.sort();
        m_includeFiles.removeDuplicates();
        return true;
    }
    return false;
}

void CMakeCbpParser::parseCodeBlocks_project_file()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (name() == QLatin1String("Project"))
            parseProject();
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseProject()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (name() == QLatin1String("Option"))
            parseOption();
        else if (name() == QLatin1String("Unit"))
            parseUnit();
        else if (name() == QLatin1String("Build"))
            parseBuild();
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseBuild()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (name() == QLatin1String("Target"))
            parseBuildTarget();
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseBuildTarget()
{
    m_buildTarget.clear();

    if (attributes().hasAttribute(QLatin1String("title")))
        m_buildTarget.title = attributes().value(QLatin1String("title")).toString();
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            if (!m_buildTarget.title.endsWith(QLatin1String("/fast")))
                m_buildTargets.append(m_buildTarget);
            return;
        } else if (name() == QLatin1String("Compiler")) {
            parseCompiler();
        } else if (name() == QLatin1String("Option")) {
            parseBuildTargetOption();
        } else if (name() == QLatin1String("MakeCommands")) {
            parseMakeCommands();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseBuildTargetOption()
{
    if (attributes().hasAttribute(QLatin1String("output"))) {
        m_buildTarget.executable = attributes().value(QLatin1String("output")).toString();
    } else if (attributes().hasAttribute(QLatin1String("type"))) {
        const QStringRef value = attributes().value(QLatin1String("type"));
        if (value == QLatin1String("2") || value == QLatin1String("3"))
            m_buildTarget.library = true;
    } else if (attributes().hasAttribute(QLatin1String("working_dir"))) {
        m_buildTarget.workingDirectory = attributes().value(QLatin1String("working_dir")).toString();
    }
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (isStartElement())
            parseUnknownElement();
    }
}

QString CMakeCbpParser::projectName() const
{
    return m_projectName;
}

void CMakeCbpParser::parseOption()
{
    if (attributes().hasAttribute(QLatin1String("title")))
        m_projectName = attributes().value(QLatin1String("title")).toString();

    if (attributes().hasAttribute(QLatin1String("compiler")))
        m_compiler = attributes().value(QLatin1String("compiler")).toString();

    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseMakeCommands()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (name() == QLatin1String("Build"))
            parseBuildTargetBuild();
        else if (name() == QLatin1String("Clean"))
            parseBuildTargetClean();
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseBuildTargetBuild()
{
    if (attributes().hasAttribute(QLatin1String("command")))
        m_buildTarget.makeCommand = attributes().value(QLatin1String("command")).toString();
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseBuildTargetClean()
{
    if (attributes().hasAttribute(QLatin1String("command")))
        m_buildTarget.makeCleanCommand = attributes().value(QLatin1String("command")).toString();
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseCompiler()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (name() == QLatin1String("Add"))
            parseAdd();
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseAdd()
{
    // CMake only supports <Add option=\> and <Add directory=\>
    const QXmlStreamAttributes addAttributes = attributes();

    const QString includeDirectory = addAttributes.value(QLatin1String("directory")).toString();
    // allow adding multiple times because order happens
    if (!includeDirectory.isEmpty())
        m_includeFiles.append(includeDirectory);

    QString compilerOption = addAttributes.value(QLatin1String("option")).toString();
    // defining multiple times a macro to the same value makes no sense
    if (!compilerOption.isEmpty() && !m_compilerOptions.contains(compilerOption)) {
        m_compilerOptions.append(compilerOption);
        int macroNameIndex = compilerOption.indexOf(QLatin1String("-D")) + 2;
        if (macroNameIndex != 1) {
            int assignIndex = compilerOption.indexOf(QLatin1Char('='), macroNameIndex);
            if (assignIndex != -1)
                compilerOption[assignIndex] = ' ';
            m_defines.append("#define ");
            m_defines.append(compilerOption.mid(macroNameIndex).toUtf8());
            m_defines.append('\n');
        }
    }

    while (!atEnd()) {
        readNext();
        if (isEndElement())
            return;
        else if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseUnit()
{
    //qDebug()<<stream.attributes().value("filename");
    QString fileName = attributes().value(QLatin1String("filename")).toString();
    m_parsingCmakeUnit = false;
    while (!atEnd()) {
        readNext();
        if (isEndElement()) {
            if (!fileName.endsWith(QLatin1String(".rule")) && !m_processedUnits.contains(fileName)) {
                // Now check whether we found a virtual element beneath
                if (m_parsingCmakeUnit) {
                    m_cmakeFileList.append( new ProjectExplorer::FileNode(fileName, ProjectExplorer::ProjectFileType, false));
                } else {
                    bool generated = false;
                    QString onlyFileName = QFileInfo(fileName).fileName();
                    if (   (onlyFileName.startsWith(QLatin1String("moc_")) && onlyFileName.endsWith(QLatin1String(".cxx")))
                           || (onlyFileName.startsWith(QLatin1String("ui_")) && onlyFileName.endsWith(QLatin1String(".h")))
                           || (onlyFileName.startsWith(QLatin1String("qrc_")) && onlyFileName.endsWith(QLatin1String(".cxx"))))
                        generated = true;

                    if (fileName.endsWith(QLatin1String(".qrc")))
                        m_fileList.append( new ProjectExplorer::FileNode(fileName, ProjectExplorer::ResourceType, generated));
                    else
                        m_fileList.append( new ProjectExplorer::FileNode(fileName, ProjectExplorer::SourceType, generated));
                }
                m_processedUnits.insert(fileName);
            }
            return;
        } else if (name() == QLatin1String("Option")) {
            parseUnitOption();
        } else if (isStartElement()) {
            parseUnknownElement();
        }
    }
}

void CMakeCbpParser::parseUnitOption()
{
    if (attributes().hasAttribute(QLatin1String("virtualFolder")))
        m_parsingCmakeUnit = true;

    while (!atEnd()) {
        readNext();

        if (isEndElement())
            break;

        if (isStartElement())
            parseUnknownElement();
    }
}

void CMakeCbpParser::parseUnknownElement()
{
    Q_ASSERT(isStartElement());

    while (!atEnd()) {
        readNext();

        if (isEndElement())
            break;

        if (isStartElement())
            parseUnknownElement();
    }
}

QList<ProjectExplorer::FileNode *> CMakeCbpParser::fileList()
{
    return m_fileList;
}

QList<ProjectExplorer::FileNode *> CMakeCbpParser::cmakeFileList()
{
    return m_cmakeFileList;
}

bool CMakeCbpParser::hasCMakeFiles()
{
    return !m_cmakeFileList.isEmpty();
}

QStringList CMakeCbpParser::includeFiles()
{
    return m_includeFiles;
}

QByteArray CMakeCbpParser::defines() const
{
    return m_defines;
}

QList<CMakeBuildTarget> CMakeCbpParser::buildTargets()
{
    return m_buildTargets;
}

QString CMakeCbpParser::compilerName() const
{
    return m_compiler;
}

void CMakeBuildTarget::clear()
{
    executable.clear();
    makeCommand.clear();
    makeCleanCommand.clear();
    workingDirectory.clear();
    title.clear();
    library = false;
}
