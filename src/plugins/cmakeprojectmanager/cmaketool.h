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

#ifndef CMAKETOOL_H
#define CMAKETOOL_H

#include "cmakeprojectmanager_global.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QQueue>
#include <QPointer>
#include <QFutureInterface>
#include <texteditor/codeassist/keywordscompletionassist.h>

#include <projectexplorer/project.h>
#include <utils/environment.h>

namespace Utils {
    class QtcProcess;
}

namespace CMakeProjectManager {

class CMAKEPROJECTMANAGER_EXPORT ICMakeTool: public QObject
{
    Q_OBJECT
public:
    ICMakeTool(QObject * parent);
    virtual ~ICMakeTool();
    virtual void cancel() = 0;
    virtual QString displayName () = 0;
    virtual bool isValid() const = 0;
    virtual bool hasCodeBlocksMsvcGenerator() const = 0;
    virtual bool hasCodeBlocksNinjaGenerator() const = 0;
    virtual TextEditor::Keywords keywords() = 0;
    virtual void runCMake (ProjectExplorer::Target* target) = 0;
    virtual void addToEnvironment(Utils::Environment &env) const;

    Core::Id id() const { return m_id; }

signals:
    void cmakeFinished (ProjectExplorer::Target* target);

protected:
    void setId(Core::Id id) { m_id = id; }

private:
    Core::Id m_id;
};

class CMAKEPROJECTMANAGER_EXPORT ICMakeToolFactory : public QObject
{
    Q_OBJECT
public:
    virtual bool canCreate (const Core::Id& id) const = 0;
    virtual ICMakeTool* create (const Core::Id& id) = 0;
};


class CMAKEPROJECTMANAGER_EXPORT CMakeTool : public ICMakeTool
{
    Q_OBJECT
public:
    explicit CMakeTool(QObject *parent = 0);
    virtual ~CMakeTool();

    enum State { Invalid, RunningBasic, RunningFunctionList, RunningFunctionDetails,
                 RunningPropertyList, RunningVariableList, RunningDone, RunningProject };

    bool isValid() const;

    void setCMakeExecutable(const QString &executable);
    QString cmakeExecutable() const;
    bool hasCodeBlocksMsvcGenerator() const;
    bool hasCodeBlocksNinjaGenerator() const;
    TextEditor::Keywords keywords();

    virtual void runCMake (ProjectExplorer::Target *target );
    virtual QString displayName ();

public slots:
    void cancel();

protected:
    void startNextRun ();

private slots:
    void finished(int exitCode);
    void onProcessReadyRead();

private:
    void flushOutput();
    void finishStep();
    void createProcessIfNotExists();
    bool startProcess(const QStringList &args, Utils::Environment env = Utils::Environment::systemEnvironment());
    void parseFunctionOutput(const QByteArray &output);
    void parseFunctionDetailsOutput(const QByteArray &output);
    void parseVariableOutput(const QByteArray &output);
    void parseDone();
    QString formatFunctionDetails(const QString &command, const QString &args);

    State m_state;
    Utils::QtcProcess *m_process;
    bool m_hasCodeBlocksMsvcGenerator;
    bool m_hasCodeBlocksNinjaGenerator;
    QString m_executable;

    QMap<QString, QStringList> m_functionArgs;
    QStringList m_variables;
    QStringList m_functions;

    QQueue< QPointer<ProjectExplorer::Target> > m_pendingRuns;
    QPointer<ProjectExplorer::Target> m_currentRun;
    QFutureInterface<void> *m_futureInterface;
};

}

#endif // CMAKETOOL_H
