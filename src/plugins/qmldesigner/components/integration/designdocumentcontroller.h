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

#ifndef DesignDocumentController_h
#define DesignDocumentController_h

#include "rewriterview.h"

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QUndoStack;
class QWidget;
class QIODevice;
class QProcess;
class QPlainTextEdit;
class QDeclarativeError;
QT_END_NAMESPACE

namespace QmlDesigner {

class Model;
class ModelNode;
class TextModifier;
class QmlObjectNode;
class RewriterView;
class ItemLibraryView;
class NavigatorView;
class ComponentView;
class PropertyEditor;
class StatesEditorView;
class FormEditorView;
struct CrumbleBarInfo;

class DesignDocumentController: public QObject
{
    Q_OBJECT

public:
    DesignDocumentController(QObject *parent);
    ~DesignDocumentController();

    QString displayName() const;
    QString simplfiedDisplayName() const;

    QString fileName() const;
    void setFileName(const QString &fileName);

    QList<RewriterView::Error> loadMaster(QPlainTextEdit *edit);
    QList<RewriterView::Error> loadMaster(const QByteArray &qml);
    void loadCurrentModel();
    void close();

    bool isDirty() const;
    bool isUndoAvailable() const;
    bool isRedoAvailable() const;

    Model *model() const;
    Model *masterModel() const;

    RewriterView *rewriterView() const;

    bool isModelSyncBlocked() const;
    void blockModelSync(bool block);

    QString contextHelpId() const;
    QList<RewriterView::Error> qmlErrors() const;

    void setItemLibraryView(ItemLibraryView* itemLibraryView);
    void setNavigator(NavigatorView* navigatorView);
    void setPropertyEditorView(PropertyEditor *propertyEditor);
    void setStatesEditorView(StatesEditorView* statesEditorView);
    void setFormEditorView(FormEditorView *formEditorView);
    void setNodeInstanceView(NodeInstanceView *nodeInstanceView);
    void setComponentView(ComponentView *componentView);

    void setCrumbleBarInfo(const CrumbleBarInfo &crumbleBarInfo);
    static void setBlockCrumbleBar(bool);

signals:
    void displayNameChanged(const QString &newFileName);
    void dirtyStateChanged(bool newState);

    void undoAvailable(bool isAvailable);
    void redoAvailable(bool isAvailable);
    void designDocumentClosed();
    void qmlErrorsChanged(const QList<RewriterView::Error> &errors);

    void fileToOpen(const QString &path);

public slots:
    bool save(QWidget *parent = 0);
    void saveAs(QWidget *parent = 0);
    void deleteSelected();
    void copySelected();
    void cutSelected();
    void paste();
    void selectAll();
    void undo();
    void redo();
    void activeQtVersionChanged();
    void changeCurrentModelTo(const ModelNode &node);
    void changeToSubComponent(const ModelNode &node);
    void changeToExternalSubComponent(const QString &fileName);
    void goIntoComponent();

#ifdef ENABLE_TEXT_VIEW
    void showText();
    void showForm();
#endif // ENABLE_TEXT_VIEW

private slots:
    void doRealSaveAs(const QString &fileName);

private:
    void detachNodeInstanceView();
    void attachNodeInstanceView();
    void changeToMasterModel();
    QVariant createCrumbleBarInfo();

    QWidget *centralWidget() const;
    QString pathToQt() const;


    class DesignDocumentControllerPrivate *d;
};


struct CrumbleBarInfo {
    ModelNode modelNode;
    QString fileName;
};

} // namespace QmlDesigner

Q_DECLARE_METATYPE(QmlDesigner::CrumbleBarInfo)

#endif // DesignDocumentController_h
