/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/
#ifndef DEFAULTDEVICETESTDIALOG_H
#define DEFAULTDEVICETESTDIALOG_H

#include "linuxdevicetester.h"
#include "remotelinux_export.h"

#include <QDialog>

namespace RemoteLinux {
namespace Internal {
class LinuxDeviceTestDialogPrivate;
} // namespace Internal

class REMOTELINUX_EXPORT LinuxDeviceTestDialog : public QDialog
{
    Q_OBJECT
public:

    // Note: The dialog takes ownership of deviceTester
    explicit LinuxDeviceTestDialog(const QSharedPointer<const LinuxDeviceConfiguration> &deviceConfiguration,
        AbstractLinuxDeviceTester * deviceTester, QWidget *parent = 0);
    ~LinuxDeviceTestDialog();

    void reject();

private slots:
    void handleProgressMessage(const QString &message);
    void handleErrorMessage(const QString &message);
    void handleTestFinished(RemoteLinux::AbstractLinuxDeviceTester::TestResult result);

private:
    void addText(const QString &text, const QString &color, bool bold);

    Internal::LinuxDeviceTestDialogPrivate * const d;
};

} // namespace RemoteLinux

#endif // DEFAULTDEVICETESTDIALOG_H
