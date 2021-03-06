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

#include "winscwparser.h"

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/task.h>

using namespace Qt4ProjectManager;
using namespace ProjectExplorer;
using namespace ProjectExplorer::Constants;

WinscwParser::WinscwParser()
{
    setObjectName(QLatin1String("WinscwParser"));
    // linker problems:
    m_linkerProblem.setPattern(QLatin1String("^(\\S*)\\(\\S+\\):\\s(.+)$"));
    m_linkerProblem.setMinimal(true);

    // WINSCW issue:
    m_compilerProblem.setPattern(QLatin1String("^([^\\(\\)]+[^\\d]):(\\d+):\\s(.+)$"));
    m_compilerProblem.setMinimal(true);
}

void WinscwParser::stdOutput(const QString &line)
{
    QString lne = line.trimmed();

    if (m_compilerProblem.indexIn(lne) > -1) {
        Task task(Task::Error,
                  m_compilerProblem.cap(3) /* description */,
                  Utils::FileName::fromUserInput(m_compilerProblem.cap(1)) /* filename */,
                  m_compilerProblem.cap(2).toInt() /* linenumber */,
                  Core::Id(TASK_CATEGORY_COMPILE));
        if (task.description.startsWith(QLatin1String("warning: "))) {
            task.type = Task::Warning;
            task.description = task.description.mid(9);
        }
        emit addTask(task);
        return;
    }
    IOutputParser::stdOutput(line);
}

void WinscwParser::stdError(const QString &line)
{
    QString lne = line.trimmed();

    if (m_linkerProblem.indexIn(lne) > -1) {
        emit addTask(Task(Task::Error,
                          m_linkerProblem.cap(2) /* description */,
                          Utils::FileName::fromUserInput(m_linkerProblem.cap(1)) /* filename */,
                          -1 /* linenumber */,
                          Core::Id(TASK_CATEGORY_COMPILE)));
        return;
    }
    IOutputParser::stdError(line);
}
