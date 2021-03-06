/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
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

#ifndef CREATESCENECOMMAND_H
#define CREATESCENECOMMAND_H

#include <qmetatype.h>
#include <QUrl>
#include <QVector>

#include "instancecontainer.h"
#include "reparentcontainer.h"
#include "idcontainer.h"
#include "propertyvaluecontainer.h"
#include "propertybindingcontainer.h"
#include "addimportcontainer.h"

namespace QmlDesigner {

class CreateSceneCommand
{
    friend QDataStream &operator>>(QDataStream &in, CreateSceneCommand &command);

public:
    CreateSceneCommand();
    CreateSceneCommand(const QVector<InstanceContainer> &instanceContainer,
                       const QVector<ReparentContainer> &reparentContainer,
                       const QVector<IdContainer> &idVector,
                       const QVector<PropertyValueContainer> &valueChangeVector,
                       const QVector<PropertyBindingContainer> &bindingChangeVector,
                       const QVector<PropertyValueContainer> &auxiliaryChangeVector,
                       const QVector<AddImportContainer> &importVector,
                       const QUrl &fileUrl);

    QVector<InstanceContainer> instances() const;
    QVector<ReparentContainer> reparentInstances() const;
    QVector<IdContainer> ids() const;
    QVector<PropertyValueContainer> valueChanges() const;
    QVector<PropertyBindingContainer> bindingChanges() const;
    QVector<PropertyValueContainer> auxiliaryChanges() const;
    QVector<AddImportContainer> imports() const;
    QUrl fileUrl() const;

private:
    QVector<InstanceContainer> m_instanceVector;
    QVector<ReparentContainer> m_reparentInstanceVector;
    QVector<IdContainer> m_idVector;
    QVector<PropertyValueContainer> m_valueChangeVector;
    QVector<PropertyBindingContainer> m_bindingChangeVector;
    QVector<PropertyValueContainer> m_auxiliaryChangeVector;
    QVector<AddImportContainer> m_importVector;
    QUrl m_fileUrl;
};

QDataStream &operator<<(QDataStream &out, const CreateSceneCommand &command);
QDataStream &operator>>(QDataStream &in, CreateSceneCommand &command);

}

Q_DECLARE_METATYPE(QmlDesigner::CreateSceneCommand)

#endif // CREATESCENECOMMAND_H
