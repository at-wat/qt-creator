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

#include "cmaketool.h"
#include "cmakeprojectmanager.h"
#include "cmakeprojectconstants.h"
#include "cmakebuildconfiguration.h"
#include "generatorinfo.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <extensionsystem/pluginmanager.h>
#include <projectexplorer/target.h>
#include <projectexplorer/kit.h>
#include <utils/qtcprocess.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/futureprogress.h>
#include <coreplugin/progressmanager/progressmanager.h>

using namespace CMakeProjectManager;
using namespace Internal;

/*!
 * \class ICMakeTool
 * Generic interface for starting cmake to create the buildfiles
 * Can be linked to a specific Kit
 */
ICMakeTool::ICMakeTool(QObject *parent)
    : QObject(parent)
{

}

ICMakeTool::~ICMakeTool()
{

}

void ICMakeTool::addToEnvironment(Utils::Environment &env) const
{

}

CMakeTool::CMakeTool(QObject *parent)
    : ICMakeTool(parent)
    , m_state(CMakeTool::RunningBasic)
    , m_process(0)
    , m_hasCodeBlocksMsvcGenerator(false)
    , m_hasCodeBlocksNinjaGenerator(false)
    , m_futureInterface(0)
{
    setId(Constants::CMAKE_TOOL_ID);
}

CMakeTool::~CMakeTool()
{
    cancel();
}

void CMakeTool::cancel()
{
    if (m_process) {
        if(m_futureInterface){
            m_futureInterface->reportCanceled();
            m_futureInterface->reportFinished();
        }

        m_process->disconnect(this);
        m_process->terminate();
        if(!m_process->waitForFinished(100)) {
            m_process->kill();
            m_process->waitForFinished();
        }

        m_process->deleteLater();
        m_process = 0;

        if (m_state != CMakeTool::RunningDone)
            m_state = CMakeTool::Invalid;
    }

    m_pendingRuns.clear();
}

void CMakeTool::setCMakeExecutable(const QString &executable)
{
    cancel();
    createProcessIfNotExists();

    m_executable = executable;
    QFileInfo fi(m_executable);
    if (fi.exists() && fi.isExecutable()) {
        // Run it to find out more
        m_state = CMakeTool::RunningBasic;
        if (!startProcess(QStringList(QLatin1String("--help"))))
            m_state = CMakeTool::Invalid;
    } else {
        m_state = CMakeTool::Invalid;
    }
}

void CMakeTool::finished(int exitCode)
{
    if (exitCode && m_state != CMakeTool::RunningProject) {
        flushOutput();
        m_state = CMakeTool::Invalid;
        return;
    }
    if (m_state == CMakeTool::RunningBasic) {
        QByteArray response = m_process->readAll();

        m_hasCodeBlocksMsvcGenerator = response.contains("CodeBlocks - NMake Makefiles");
        m_hasCodeBlocksNinjaGenerator = response.contains("CodeBlocks - Ninja");

        if (response.isEmpty()) {
            m_state = CMakeTool::Invalid;
        } else {
            m_state = CMakeTool::RunningFunctionList;
            if (!startProcess(QStringList(QLatin1String("--help-command-list"))))
                finished(0); // should never happen, just continue
        }
    } else if (m_state == CMakeTool::RunningFunctionList) {
        parseFunctionOutput(m_process->readAll());
        m_state = CMakeTool::RunningFunctionDetails;
        if (!startProcess(QStringList(QLatin1String("--help-commands"))))
            finished(0); // should never happen, just continue
    } else if (m_state == CMakeTool::RunningFunctionDetails) {
        parseFunctionDetailsOutput(m_process->readAll());
        m_state = CMakeTool::RunningPropertyList;
        if (!startProcess(QStringList(QLatin1String("--help-property-list"))))
            finished(0); // should never happen, just continue
    } else if (m_state == CMakeTool::RunningPropertyList) {
        parseVariableOutput(m_process->readAll());
        m_state = CMakeTool::RunningVariableList;
        if (!startProcess(QStringList(QLatin1String("--help-variable-list"))))
            finished(0); // should never happen, just continue
    } else if (m_state == CMakeTool::RunningVariableList) {
        parseVariableOutput(m_process->readAll());
        parseDone();
        m_state = CMakeTool::RunningDone;
    }else if (m_state == CMakeTool::RunningProject) {
        //cmake run is finished no matter if we were successful or not
        m_currentRun.clear();
        m_state = CMakeTool::RunningDone;

        if(m_futureInterface) {
            if(exitCode) m_futureInterface->reportCanceled();
            m_futureInterface->reportFinished();
        }

        flushOutput();
        emit cmakeFinished(m_currentRun.data());
    }

    if(m_state == CMakeTool::RunningDone )
        startNextRun();
}

void CMakeTool::onProcessReadyRead()
{
    //we only care about cmake project building output
    if(m_state != CMakeTool::RunningProject)
        return;

    QString stderr = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
    QString stdout = QString::fromLocal8Bit(m_process->readAllStandardOutput()).trimmed();
    if (!stderr.isEmpty()) {
        Core::MessageManager::write(QString(QLatin1String("%0")).arg(stderr),Core::MessageManager::ModeSwitch);
    }
    if (!stdout.isEmpty()) {
        Core::MessageManager::write(QString(QLatin1String("%0")).arg(stdout),Core::MessageManager::NoModeSwitch);
    }
}

void CMakeTool::flushOutput()
{
    QString errorMsg = QString::fromLocal8Bit(m_process->readAllStandardError());
    if (errorMsg.trimmed().length()>0) Core::MessageManager::write(errorMsg,Core::MessageManager::ModeSwitch);
    QString msg = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    if (msg.trimmed().length()>0) Core::MessageManager::write(msg,Core::MessageManager::NoModeSwitch);
}

void CMakeTool::createProcessIfNotExists()
{
    if(!m_process) {
        m_process = new Utils::QtcProcess();
        connect(m_process, SIGNAL(finished(int)),
                this, SLOT(finished(int)));
        connect(m_process,SIGNAL(readyRead()),
                this,SLOT(onProcessReadyRead()));
    }
}

bool CMakeTool::isValid() const
{
    if (m_state == CMakeTool::Invalid)
        return false;
    if (m_state == CMakeTool::RunningBasic)
        m_process->waitForFinished();
    return (m_state != CMakeTool::Invalid);
}

bool CMakeTool::startProcess(const QStringList &args, Utils::Environment env)
{
    QString argsStr = Utils::QtcProcess::joinArgs(args);

    //add custom cmake environment vars
    addToEnvironment(env);

    qDebug()<<"Starting process: "
           <<m_executable
          <<argsStr;

    m_process->setEnvironment(env);
    m_process->setCommand(m_executable, argsStr);
    m_process->start();
    return m_process->waitForStarted(2000);
}

QString CMakeTool::cmakeExecutable() const
{
    return m_executable;
}

bool CMakeTool::hasCodeBlocksMsvcGenerator() const
{
    if (!isValid())
        return false;
    return m_hasCodeBlocksMsvcGenerator;
}

bool CMakeTool::hasCodeBlocksNinjaGenerator() const
{
    if (!isValid())
        return false;
    return m_hasCodeBlocksNinjaGenerator;
}

TextEditor::Keywords CMakeTool::keywords()
{
    while (m_state != RunningDone && m_state != CMakeTool::Invalid) {
        m_process->waitForFinished();
    }

    if (m_state == CMakeTool::Invalid)
        return TextEditor::Keywords(QStringList(), QStringList(), QMap<QString, QStringList>());

    return TextEditor::Keywords(m_variables, m_functions, m_functionArgs);
}

void CMakeTool::runCMake( ProjectExplorer::Target* target )
{
    if(!isValid())
        return;

    QPointer<ProjectExplorer::Target> ptrTarget(target);

    if(!m_pendingRuns.contains(ptrTarget))
        m_pendingRuns.append(ptrTarget);

    if(m_state != CMakeTool::RunningDone)
        return;

    return startNextRun();
}

QString CMakeTool::displayName()
{
    return QString::fromLatin1("Default");
}

void CMakeTool::startNextRun()
{
    QPointer<ProjectExplorer::Target> ptrMyTarget;
    while(!m_pendingRuns.isEmpty() && ptrMyTarget.isNull())
        ptrMyTarget = m_pendingRuns.dequeue();

    if(!ptrMyTarget)
        return;

    // We create a cbp file, only if we didn't find a cbp file in the base directory
    // Yet that can still override cbp files in subdirectories
    // And we are creating tons of files in the source directories
    // All of that is not really nice.
    // The mid term plan is to move away from the CodeBlocks Generator and use our own
    // QtCreator generator, which actually can be very similar to the CodeBlock Generator
    CMakeBuildConfiguration* config = qobject_cast<CMakeBuildConfiguration*>(ptrMyTarget->activeBuildConfiguration());
    if(!config)
        return;

    Utils::Environment env = config->environment();
    QDir buildDirectory(config->buildDirectory().toString());

    QString buildDirectoryPath = buildDirectory.absolutePath();
    buildDirectory.mkpath(buildDirectoryPath);

    //user chose the kit on project creation
    ptrMyTarget->kit()->addToEnvironment(env);

    createProcessIfNotExists();
    m_state = CMakeTool::RunningProject;

    GeneratorInfo gInfo(ptrMyTarget->kit(),config->useNinja());

    QStringList args;
    args << ptrMyTarget->project()->projectDirectory()
         << config->arguments()
         << QLatin1String(gInfo.generatorArgument());

    m_process->setWorkingDirectory(buildDirectoryPath);

    if(m_futureInterface)
        delete m_futureInterface;

    m_futureInterface = new QFutureInterface<void>();
    m_futureInterface->setProgressRange(0,1);

    Core::FutureProgress* futureProgress = Core::ProgressManager::addTask(m_futureInterface->future()
                                                                          ,tr("Parsing ProjectFile")
                                                                          ,Core::Id("CMakeProjectManager.CMakeTaskID"));
    connect(futureProgress,SIGNAL(canceled()),this,SLOT(cancel()));

    m_futureInterface->reportStarted();
    startProcess(args,env);
}

static void extractKeywords(const QByteArray &input, QStringList *destination)
{
    if (!destination)
        return;

    QString keyword;
    int ignoreZone = 0;
    for (int i = 0; i < input.count(); ++i) {
        const QChar chr = QLatin1Char(input.at(i));
        if (chr == QLatin1Char('{'))
            ++ignoreZone;
        if (chr == QLatin1Char('}'))
            --ignoreZone;
        if (ignoreZone == 0) {
            if ((chr.isLetterOrNumber() && chr.isUpper())
                    || chr == QLatin1Char('_')) {
                keyword += chr;
            } else {
                if (!keyword.isEmpty()) {
                    if (keyword.size() > 1)
                        *destination << keyword;
                    keyword.clear();
                }
            }
        }
    }
    if (keyword.size() > 1)
        *destination << keyword;
}

void CMakeTool::parseFunctionOutput(const QByteArray &output)
{
    QList<QByteArray> cmakeFunctionsList = output.split('\n');
    m_functions.clear();
    if (!cmakeFunctionsList.isEmpty()) {
        cmakeFunctionsList.removeFirst(); //remove version string
        foreach (const QByteArray &function, cmakeFunctionsList)
            m_functions << QString::fromLocal8Bit(function.trimmed());
    }
}

QString CMakeTool::formatFunctionDetails(const QString &command, const QString &args)
{
    return QString::fromLatin1("<table><tr><td><b>%1</b></td><td>%2</td></tr>")
            .arg(Qt::escape(command)).arg(Qt::escape(args));
}

void CMakeTool::parseFunctionDetailsOutput(const QByteArray &output)
{
    QStringList cmakeFunctionsList = m_functions;
    QList<QByteArray> cmakeCommandsHelp = output.split('\n');
    for (int i = 0; i < cmakeCommandsHelp.count(); ++i) {
        QByteArray lineTrimmed = cmakeCommandsHelp.at(i).trimmed();
        if (cmakeFunctionsList.isEmpty())
            break;
        if (cmakeFunctionsList.first().toLatin1() == lineTrimmed) {
            QStringList commandSyntaxes;
            QString currentCommandSyntax;
            QString currentCommand = cmakeFunctionsList.takeFirst();
            ++i;
            for (; i < cmakeCommandsHelp.count(); ++i) {
                lineTrimmed = cmakeCommandsHelp.at(i).trimmed();

                if (!cmakeFunctionsList.isEmpty() && cmakeFunctionsList.first().toLatin1() == lineTrimmed) {
                    //start of next function in output
                    if (!currentCommandSyntax.isEmpty())
                        commandSyntaxes << currentCommandSyntax.append(QLatin1String("</table>"));
                    --i;
                    break;
                }
                if (lineTrimmed.startsWith(currentCommand.toLatin1() + "(")) {
                    if (!currentCommandSyntax.isEmpty())
                        commandSyntaxes << currentCommandSyntax.append(QLatin1String("</table>"));

                    QByteArray argLine = lineTrimmed.mid(currentCommand.length());
                    extractKeywords(argLine, &m_variables);
                    currentCommandSyntax = formatFunctionDetails(currentCommand, QString::fromUtf8(argLine));
                } else {
                    if (!currentCommandSyntax.isEmpty()) {
                        if (lineTrimmed.isEmpty()) {
                            commandSyntaxes << currentCommandSyntax.append(QLatin1String("</table>"));
                            currentCommandSyntax.clear();
                        } else {
                            extractKeywords(lineTrimmed, &m_variables);
                            currentCommandSyntax += QString::fromLatin1("<tr><td>&nbsp;</td><td>%1</td></tr>")
                                    .arg(Qt::escape(QString::fromLocal8Bit(lineTrimmed)));
                        }
                    }
                }
            }
            m_functionArgs[currentCommand] = commandSyntaxes;
        }
    }
    m_functions = m_functionArgs.keys();
}

void CMakeTool::parseVariableOutput(const QByteArray &output)
{
    QList<QByteArray> variableList = output.split('\n');
    if (!variableList.isEmpty()) {
        variableList.removeFirst(); //remove version string
        foreach (const QByteArray &variable, variableList) {
            if (variable.contains("_<CONFIG>")) {
                m_variables << QString::fromLocal8Bit(variable).replace(QLatin1String("_<CONFIG>"), QLatin1String("_DEBUG"));
                m_variables << QString::fromLocal8Bit(variable).replace(QLatin1String("_<CONFIG>"), QLatin1String("_RELEASE"));
                m_variables << QString::fromLocal8Bit(variable).replace(QLatin1String("_<CONFIG>"), QLatin1String("_MINSIZEREL"));
                m_variables << QString::fromLocal8Bit(variable).replace(QLatin1String("_<CONFIG>"), QLatin1String("_RELWITHDEBINFO"));
            } else if (variable.contains("_<LANG>")) {
                m_variables << QString::fromLocal8Bit(variable).replace(QLatin1String("_<LANG>"), QLatin1String("_C"));
                m_variables << QString::fromLocal8Bit(variable).replace(QLatin1String("_<LANG>"), QLatin1String("_CXX"));
            } else if (!variable.contains("_<") && !variable.contains('[')) {
                m_variables << QString::fromLocal8Bit(variable);
            }
        }
    }
}

void CMakeTool::parseDone()
{
    m_variables.sort();
    m_variables.removeDuplicates();
}
