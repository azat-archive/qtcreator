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

#include "parameteraction.h"

/*!
    \class Utils::ParameterAction

    \brief Intended for actions that act on a 'current',
     string-type parameter (typically a file name), for example 'Save file %1'.

    The action has 2 states:
    \list
    \o <no current parameter> displaying "Do XX" (empty text)
    \o <parameter present> displaying "Do XX with %1".
    \endlist

    Provides a slot to set the parameter, changing display
    and enabled state accordingly.
    The text passed in should already be translated; parameterText
    should contain a %1 where the parameter is to be inserted.
*/

namespace Utils {

ParameterAction::ParameterAction(const QString &emptyText,
                                     const QString &parameterText,
                                     EnablingMode mode,
                                     QObject* parent) :
    QAction(emptyText, parent),
    m_emptyText(emptyText),
    m_parameterText(parameterText),
    m_enablingMode(mode)
{
}

QString ParameterAction::emptyText() const
{
    return m_emptyText;
}

void ParameterAction::setEmptyText(const QString &t)
{
    m_emptyText = t;
}

QString ParameterAction::parameterText() const
{
    return m_parameterText;
}

void ParameterAction::setParameterText(const QString &t)
{
    m_parameterText = t;
}

ParameterAction::EnablingMode ParameterAction::enablingMode() const
{
    return m_enablingMode;
}

void ParameterAction::setEnablingMode(EnablingMode m)
{
    m_enablingMode = m;
}

void ParameterAction::setParameter(const QString &p)
{
    const bool enabled = !p.isEmpty();
    if (enabled) {
        setText(m_parameterText.arg(p));
    } else {
        setText(m_emptyText);
    }
    if (m_enablingMode == EnabledWithParameter)
        setEnabled(enabled);
}

} // namespace Utils
