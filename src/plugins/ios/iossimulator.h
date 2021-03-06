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
#ifndef IOSSIMULATOR_H
#define IOSSIMULATOR_H

#include <projectexplorer/devicesupport/idevice.h>
#include <utils/fileutils.h>

#include <QSharedPointer>

namespace ProjectExplorer {
class Kit;
}
namespace Ios {
namespace Internal {
class IosConfigurations;
class IosSimulatorFactory;

class IosSimulator : public ProjectExplorer::IDevice
{
public:
    typedef QSharedPointer<const IosSimulator> ConstPtr;
    typedef QSharedPointer<IosSimulator> Ptr;
    ProjectExplorer::IDevice::DeviceInfo deviceInformation() const;

    QString displayType() const;
    ProjectExplorer::IDeviceWidget *createWidget();
    QList<Core::Id> actionIds() const;
    QString displayNameForActionId(Core::Id actionId) const;
    void executeAction(Core::Id actionId, QWidget *parent = 0);
    ProjectExplorer::DeviceProcessSignalOperation::Ptr signalOperation() const;
    Utils::FileName simulatorPath() const;
    void fromMap(const QVariantMap &map);
    QVariantMap toMap() const;

    ProjectExplorer::IDevice::Ptr clone() const;

protected:
    friend class IosSimulatorFactory;
    friend class IosConfigurations;
    IosSimulator();
    IosSimulator(Core::Id id, Utils::FileName simulatorPath);
    IosSimulator(const IosSimulator &other);
private:
    Utils::FileName m_simulatorPath;
};

namespace IosKitInformation {
IosSimulator::ConstPtr simulator(ProjectExplorer::Kit *kit);
} // namespace IosKitInformation
} // namespace Internal
} // namespace Ios

#endif // IOSSIMULATOR_H
