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

#include "callgrindcostitem.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include "callgrindparsedata.h"
#include "callgrindfunctioncall.h"

namespace Valgrind {
namespace Callgrind {

//BEGIN CostItem::Private

class CostItem::Private {
public:
    Private(ParseData *data);
    ~Private();

    QVector<quint64> m_positions;
    QVector<quint64> m_events;
    const FunctionCall *m_call;

    const ParseData *m_data;
    qint64 m_differingFileId;
};

CostItem::Private::Private(ParseData *data)
    : m_positions(data->positions().size(), 0)
    , m_events(data->events().size(), 0)
    , m_call(0)
    , m_data(data)
    , m_differingFileId(-1)
{
}

CostItem::Private::~Private()
{
    delete m_call;
}


//BEGIN CostItem
CostItem::CostItem(ParseData *data)
: d(new Private(data))
{
}

CostItem::~CostItem()
{
    delete d;
}

quint64 CostItem::position(int posIdx) const
{
    return d->m_positions.at(posIdx);
}

void CostItem::setPosition(int posIdx, quint64 position)
{
    d->m_positions[posIdx] = position;
}

QVector< quint64 > CostItem::positions() const
{
    return d->m_positions;
}

quint64 CostItem::cost(int event) const
{
    return d->m_events.at(event);
}

void CostItem::setCost(int event, quint64 cost)
{
    d->m_events[event] = cost;
}

QVector< quint64 > CostItem::costs() const
{
    return d->m_events;
}

const FunctionCall *CostItem::call() const
{
    return d->m_call;
}

void CostItem::setCall(const FunctionCall *call)
{
    d->m_call = call;
}

QString CostItem::differingFile() const
{
    if (d->m_differingFileId != -1)
        return d->m_data->stringForFileCompression(d->m_differingFileId);
    else
        return QString();
}

qint64 CostItem::differingFileId() const
{
    return d->m_differingFileId;
}

void CostItem::setDifferingFile(qint64 fileId)
{
    d->m_differingFileId = fileId;
}

} // namespace Callgrind
} // namespace Valgrind
