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

#include "cmakebuildconfiguration.h"

#include "cmakebuildinfo.h"
#include "cmakeproject.h"
#include "cmakeprojectconstants.h"
#include "cmaketoolmanager.h"
#include "generatorinfo.h"

#include <coreplugin/icore.h>
#include <coreplugin/mimedatabase.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QInputDialog>

using namespace CMakeProjectManager;
using namespace Internal;

namespace {
const char USE_NINJA_KEY[] = "CMakeProjectManager.CMakeBuildConfiguration.UseNinja";
const char USER_ARGS_KEY[] = "CMakeProjectManager.CMakeBuildConfiguration.UserArguments";
} // namespace

CMakeBuildConfiguration::CMakeBuildConfiguration(ProjectExplorer::Target *parent) :
    BuildConfiguration(parent, Core::Id(Constants::CMAKE_BC_ID)), m_useNinja(false)
{
    init(parent);
}

CMakeBuildConfiguration::CMakeBuildConfiguration(ProjectExplorer::Target *parent,
                                                 CMakeBuildConfiguration *source) :
    BuildConfiguration(parent, source),
    m_msvcVersion(source->m_msvcVersion),
    m_useNinja(false)
{
    Q_ASSERT(parent);
    cloneSteps(source);

    connect(this,SIGNAL(argumentsChanged(QStringList)),this,SLOT(runCMake()));
    connect(this,SIGNAL(buildDirectoryChanged()),this,SLOT(runCMake()));
    connect(this,SIGNAL(useNinjaChanged(bool)),this,SLOT(cleanAndRunCMake()));
}

QVariantMap CMakeBuildConfiguration::toMap() const
{
    QVariantMap map(ProjectExplorer::BuildConfiguration::toMap());
    map.insert(QLatin1String(USE_NINJA_KEY), m_useNinja);

    if(m_arguments.size())
        map.insert(QLatin1String(USER_ARGS_KEY),Utils::QtcProcess::joinArgs(m_arguments));

    return map;
}

bool CMakeBuildConfiguration::fromMap(const QVariantMap &map)
{
    if (!BuildConfiguration::fromMap(map))
        return false;

    CMakeProject* project = static_cast<CMakeProject*>(this->target()->project());

    m_useNinja = map.value(QLatin1String(USE_NINJA_KEY), false).toBool();
    if(map.contains(QLatin1String(USER_ARGS_KEY)))
        m_arguments = Utils::QtcProcess::splitArgs(map.value(QLatin1String(USER_ARGS_KEY)).toString());

    /*
     * If this evaluates to true, it means there was a in source build before and
     * was cleaned out by the user. Instead of querying the user for some build
     * directory we just use the default one, the user can always change this if
     * he does not like it
     */
    if(this->buildDirectory() == Utils::FileName::fromString(project->projectDirectory()) && !project->hasInSourceBuild()) {
        this->setBuildDirectory(Utils::FileName::fromString(
                                    CMakeProject::shadowBuildDirectory(project->projectFilePath()
                                                                       ,this->target()->kit()
                                                                       ,this->displayName())));
    }

    return true;
}

void CMakeBuildConfiguration::setBuildDirectory(const Utils::FileName &directory)
{
    if (directory == buildDirectory())
        return;
    BuildConfiguration::setBuildDirectory(directory);
}

CMakeBuildConfiguration::CMakeBuildConfiguration(ProjectExplorer::Target *parent, const Core::Id &id) :
    BuildConfiguration(parent,id), m_useNinja(false)
{
    init(parent);
}

bool removeRecursively(const QString dirPath)
{
    bool success = true;
    QDirIterator di(dirPath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (di.hasNext()) {
        di.next();
        const QFileInfo& fi = di.fileInfo();
        const QString &filePath = di.filePath();
        bool ok;
        if (fi.isDir() && !fi.isSymLink()) {
            ok = removeRecursively(filePath);
        } else {
            ok = QFile::remove(filePath);
            if (!ok) {
                const QFile::Permissions permissions = QFile::permissions(filePath);
                if (!(permissions & QFile::WriteUser))
                    ok = QFile::setPermissions(filePath, permissions | QFile::WriteUser)
                        && QFile::remove(filePath);
            }
        }
        if (!ok)
            success = false;
    }

    if (success)
        success = QDir(dirPath).rmdir(dirPath);

    return success;
}

void CMakeBuildConfiguration::cleanAndRunCMake()
{
    removeRecursively(buildDirectory().toString());

    runCMake();
}

void CMakeBuildConfiguration::runCMake()
{
    ICMakeTool* cmake = CMakeToolManager::cmakeToolForKit(target()->kit());
    cmake->runCMake(target());
    connect(cmake,SIGNAL(cmakeFinished(ProjectExplorer::Target*)),this,SLOT(parseCMakeLists(ProjectExplorer::Target*)),Qt::UniqueConnection);
}

void CMakeBuildConfiguration::init(ProjectExplorer::Target *parent)
{
    CMakeProject *project = static_cast<CMakeProject *>(parent->project());


    setBuildDirectory(Utils::FileName::fromString(project->shadowBuildDirectory(project->projectFilePath(),
                                                                                parent->kit(),
                                                                                displayName())));


    connect(this,SIGNAL(argumentsChanged(QStringList)),this,SLOT(runCMake()));
    connect(this,SIGNAL(buildDirectoryChanged()),this,SLOT(runCMake()));
    connect(this,SIGNAL(useNinjaChanged(bool)),this,SLOT(cleanAndRunCMake()));
}

bool CMakeBuildConfiguration::useNinja() const
{
    return m_useNinja;
}

void CMakeBuildConfiguration::setUseNinja(bool useNninja)
{
    if (m_useNinja != useNninja) {
        m_useNinja = useNninja;
        emit useNinjaChanged(m_useNinja);
    }
}

QStringList CMakeBuildConfiguration::arguments() const
{
    return m_arguments;
}

void CMakeBuildConfiguration::setArguments(const QStringList &args)
{
    m_arguments = args;
    emit argumentsChanged(m_arguments);
}

QByteArray CMakeBuildConfiguration::generator() const
{
    return cachedGeneratorFromFile(buildDirectory().toString() + QLatin1String("/CMakeCache.txt"));
}

CMakeBuildConfiguration::~CMakeBuildConfiguration()
{ }

ProjectExplorer::NamedWidget *CMakeBuildConfiguration::createConfigWidget()
{
    return new CMakeBuildSettingsWidget(this);
}

QByteArray CMakeBuildConfiguration::cachedGeneratorFromFile(const QString &cache)
{
    QFile fi(cache);
    if (fi.exists()) {
        // Cache exists, then read it...
        if (fi.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!fi.atEnd()) {
                QByteArray line = fi.readLine();
                if (line.startsWith("CMAKE_GENERATOR:INTERNAL=")) {
                    int splitpos = line.indexOf('=');
                    if (splitpos != -1) {
                        QByteArray cachedGenerator = line.mid(splitpos + 1).trimmed();
                        if (!cachedGenerator.isEmpty())
                            return cachedGenerator;
                    }
                }
            }
        }
    }
    return QByteArray();
}


/*!
  \class CMakeBuildConfigurationFactory
*/

CMakeBuildConfigurationFactory::CMakeBuildConfigurationFactory(QObject *parent) :
    ProjectExplorer::IBuildConfigurationFactory(parent)
{
}

CMakeBuildConfigurationFactory::~CMakeBuildConfigurationFactory()
{
}

int CMakeBuildConfigurationFactory::priority(const ProjectExplorer::Target *parent) const
{
    return canHandle(parent) ? 0 : -1;
}

QList<ProjectExplorer::BuildInfo *> CMakeBuildConfigurationFactory::availableBuilds(const ProjectExplorer::Target *parent) const
{
    QList<ProjectExplorer::BuildInfo *> result;

    CMakeBuildInfo *info = createBuildInfo(parent->kit(),
                                           parent->project()->projectDirectory());


    info->buildDirectory =  Utils::FileName::fromString(CMakeProject::shadowBuildDirectory(parent->project()->projectFilePath(),
                                                                                           parent->kit(),
                                                                                           info->displayName));


    result << info;
    return result;
}

int CMakeBuildConfigurationFactory::priority(const ProjectExplorer::Kit *k, const QString &projectPath) const
{
    return (k && Core::MimeDatabase::findByFile(QFileInfo(projectPath))
            .matchesType(QLatin1String(Constants::CMAKEMIMETYPE))) ? 0 : -1;
}

QList<ProjectExplorer::BuildInfo *> CMakeBuildConfigurationFactory::availableSetups(const ProjectExplorer::Kit *k,
                                                                                    const QString &projectPath) const
{
    QList<ProjectExplorer::BuildInfo *> result;
    CMakeBuildInfo *info = createBuildInfo(k, ProjectExplorer::Project::projectDirectory(projectPath));
    //: The name of the build configuration created by default for a cmake project.
    info->displayName = tr("Default");

    info->buildDirectory
            = Utils::FileName::fromString(CMakeProject::shadowBuildDirectory(projectPath,k,
                                                                             info->displayName));

    result << info;

    return result;
}

ProjectExplorer::BuildConfiguration *CMakeBuildConfigurationFactory::create(ProjectExplorer::Target *parent,
                                                                            const ProjectExplorer::BuildInfo *info) const
{
    QTC_ASSERT(info->factory() == this, return 0);
    QTC_ASSERT(info->kitId == parent->kit()->id(), return 0);
    QTC_ASSERT(!info->displayName.isEmpty(), return 0);

    CMakeBuildInfo copy(*static_cast<const CMakeBuildInfo *>(info));
    CMakeProject *project = static_cast<CMakeProject *>(parent->project());

    if (copy.buildDirectory.isEmpty())
        copy.buildDirectory
                = Utils::FileName::fromString(project->shadowBuildDirectory(project->projectFilePath(),
                                                                            parent->kit(),
                                                                            copy.displayName));

    CMakeBuildConfiguration *bc = new CMakeBuildConfiguration(parent);
    bc->setDisplayName(copy.displayName);
    bc->setDefaultDisplayName(copy.displayName);

    ProjectExplorer::BuildStepList *buildSteps = bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
    ProjectExplorer::BuildStepList *cleanSteps = bc->stepList(ProjectExplorer::Constants::BUILDSTEPS_CLEAN);

    MakeStep *makeStep = new MakeStep(buildSteps);
    buildSteps->insertStep(0, makeStep);

    MakeStep *cleanMakeStep = new MakeStep(cleanSteps);
    cleanSteps->insertStep(0, cleanMakeStep);
    cleanMakeStep->setAdditionalArguments(QLatin1String("clean"));
    cleanMakeStep->setClean(true);

    bc->setBuildDirectory(Utils::FileName::fromString(copy.buildDirectory.toString()));
    bc->setUseNinja(copy.useNinja);

    // Default to all
    if (project->hasBuildTarget(QLatin1String("all")))
        makeStep->setBuildTarget(QLatin1String("all"), true);

    return bc;
}

bool CMakeBuildConfigurationFactory::canClone(const ProjectExplorer::Target *parent, ProjectExplorer::BuildConfiguration *source) const
{
    if (!canHandle(parent))
        return false;
    return source->id() == Constants::CMAKE_BC_ID;
}

CMakeBuildConfiguration *CMakeBuildConfigurationFactory::clone(ProjectExplorer::Target *parent, ProjectExplorer::BuildConfiguration *source)
{
    if (!canClone(parent, source))
        return 0;
    CMakeBuildConfiguration *old = static_cast<CMakeBuildConfiguration *>(source);
    return new CMakeBuildConfiguration(parent, old);
}

bool CMakeBuildConfigurationFactory::canRestore(const ProjectExplorer::Target *parent, const QVariantMap &map) const
{
    if (!canHandle(parent))
        return false;
    return ProjectExplorer::idFromMap(map) == Constants::CMAKE_BC_ID;
}

CMakeBuildConfiguration *CMakeBuildConfigurationFactory::restore(ProjectExplorer::Target *parent, const QVariantMap &map)
{
    if (!canRestore(parent, map))
        return 0;
    CMakeBuildConfiguration *bc = new CMakeBuildConfiguration(parent);
    if (bc->fromMap(map))
        return bc;
    delete bc;
    return 0;
}

bool CMakeBuildConfigurationFactory::canHandle(const ProjectExplorer::Target *t) const
{
    QTC_ASSERT(t, return false);
    if (!t->project()->supportsKit(t->kit()))
        return false;
    return qobject_cast<CMakeProject *>(t->project());
}

CMakeBuildInfo *CMakeBuildConfigurationFactory::createBuildInfo(const ProjectExplorer::Kit *k,
                                                                const QString &sourceDir) const
{
    //this is safe, because the kit will not be changed and the generator not used outside this function
    ProjectExplorer::Kit* otherK = const_cast<ProjectExplorer::Kit*>(k);

    CMakeManager *manager = ExtensionSystem::PluginManager::getObject<CMakeManager>();
    Q_ASSERT(manager);

    ICMakeTool* cmake = CMakeToolManager::cmakeToolForKit(k);
    if(!cmake)
        return 0;

    //there should always be a generator when we reach this stage
    QList<GeneratorInfo> generators = GeneratorInfo::generatorInfosFor(otherK
                                                                       ,GeneratorInfo::OfferNinja
                                                                       ,manager->preferNinja()
                                                                       ,cmake->hasCodeBlocksMsvcGenerator());

    CMakeBuildInfo *info = new CMakeBuildInfo(this);
    info->typeName = tr("Build");
    info->kitId = k->id();
    info->environment = Utils::Environment::systemEnvironment();
    k->addToEnvironment(info->environment);
    info->sourceDirectory = sourceDir;
    info->supportsShadowBuild = !CMakeProject::hasInSourceBuild(info->sourceDirectory);

    //if the first generator is ninja, that is the preferred one
    info->useNinja = (generators.size() > 0) ? generators.first().isNinja() : false;

    return info;
}

ProjectExplorer::BuildConfiguration::BuildType CMakeBuildConfiguration::buildType() const
{
    QString cmakeBuildType;
    QFile cmakeCache(buildDirectory().toString() + QLatin1String("/CMakeCache.txt"));
    if (cmakeCache.open(QIODevice::ReadOnly)) {
        while (!cmakeCache.atEnd()) {
            QByteArray line = cmakeCache.readLine();
            if (line.startsWith("CMAKE_BUILD_TYPE")) {
                if (int pos = line.indexOf('='))
                    cmakeBuildType = QString::fromLocal8Bit(line.mid(pos + 1).trimmed());
                break;
            }
        }
        cmakeCache.close();
    }

    // Cover all common CMake build types
    if (cmakeBuildType.compare(QLatin1String("Release"), Qt::CaseInsensitive) == 0
            || cmakeBuildType.compare(QLatin1String("MinSizeRel"), Qt::CaseInsensitive) == 0)
    {
        return Release;
    } else if (cmakeBuildType.compare(QLatin1String("Debug"), Qt::CaseInsensitive) == 0
               || cmakeBuildType.compare(QLatin1String("DebugFull"), Qt::CaseInsensitive) == 0
               || cmakeBuildType.compare(QLatin1String("RelWithDebInfo"), Qt::CaseInsensitive) == 0)
    {
        return Debug;
    }

    return Unknown;
}

