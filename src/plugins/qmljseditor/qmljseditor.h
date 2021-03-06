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

#ifndef QMLJSEDITOR_H
#define QMLJSEDITOR_H

#include "qmljseditor_global.h"

#include <qmljs/qmljsdocument.h>
#include <qmljs/qmljsscanner.h>
#include <qmljs/qmljsscopechain.h>
#include <qmljs/qmljsstaticanalysismessage.h>
#include <texteditor/basetexteditor.h>
#include <texteditor/quickfix.h>

#include <QSharedPointer>
#include <QModelIndex>

QT_BEGIN_NAMESPACE
class QComboBox;
class QTimer;
QT_END_NAMESPACE

namespace Core {
class ICore;
}

namespace QmlJS {
    class ModelManagerInterface;
    class IContextPane;
    class LookupContext;
}

/*!
    The top-level namespace of the QmlJSEditor plug-in.
 */
namespace QmlJSEditor {
class QmlJSEditorEditable;
class FindReferences;

namespace Internal {
class QmlOutlineModel;
class SemanticInfoUpdater;
struct SemanticInfoUpdaterSource;
class SemanticHighlighter;
} // namespace Internal

struct QMLJSEDITOR_EXPORT Declaration
{
    QString text;
    int startLine;
    int startColumn;
    int endLine;
    int endColumn;

    Declaration()
        : startLine(0),
        startColumn(0),
        endLine(0),
        endColumn(0)
    { }
};

class QMLJSEDITOR_EXPORT Range
{
public:
    Range(): ast(0) {}

public: // attributes
    QmlJS::AST::Node *ast;
    QTextCursor begin;
    QTextCursor end;
};

class QMLJSEDITOR_EXPORT SemanticInfo
{
public:
    SemanticInfo() {}

    bool isValid() const;
    int revision() const;

    // Returns the AST path
    QList<QmlJS::AST::Node *> astPath(int cursorPosition) const;

    // Returns the AST node at the offset (the last member of the astPath)
    QmlJS::AST::Node *astNodeAt(int cursorPosition) const;

    // Returns the list of declaration-type nodes that enclose the given position.
    // It is more robust than astPath because it tracks ranges with text cursors
    // and will thus be correct even if the document was changed and not yet
    // reparsed. It does not return the full path of AST nodes.
    QList<QmlJS::AST::Node *> rangePath(int cursorPosition) const;

    // Returns the declaring member
    QmlJS::AST::Node *rangeAt(int cursorPosition) const;
    QmlJS::AST::Node *declaringMemberNoProperties(int cursorPosition) const;

    // Returns a scopeChain for the given path
    QmlJS::ScopeChain scopeChain(const QList<QmlJS::AST::Node *> &path = QList<QmlJS::AST::Node *>()) const;

public: // attributes
    QmlJS::Document::Ptr document;
    QmlJS::Snapshot snapshot;
    QmlJS::ContextPtr context;
    QList<Range> ranges;
    QHash<QString, QList<QmlJS::AST::SourceLocation> > idLocations;

    // these are in addition to the parser messages in the document
    QList<QmlJS::DiagnosticMessage> semanticMessages;
    QList<QmlJS::StaticAnalysis::Message> staticAnalysisMessages;

private:
    QSharedPointer<const QmlJS::ScopeChain> m_rootScopeChain;

    friend class Internal::SemanticInfoUpdater;
};

class QMLJSEDITOR_EXPORT QmlJSTextEditorWidget : public TextEditor::BaseTextEditorWidget
{
    Q_OBJECT

public:
    QmlJSTextEditorWidget(QWidget *parent = 0);
    ~QmlJSTextEditorWidget();

    virtual void unCommentSelection();

    SemanticInfo semanticInfo() const;
    bool isSemanticInfoOutdated() const;
    int editorRevision() const;

    Internal::QmlOutlineModel *outlineModel() const;
    QModelIndex outlineModelIndex();

    bool updateSelectedElements() const;
    void setUpdateSelectedElements(bool value);

    static QVector<QString> highlighterFormatCategories();

    TextEditor::IAssistInterface *createAssistInterface(TextEditor::AssistKind assistKind,
                                                        TextEditor::AssistReason reason) const;

public slots:
    virtual void setTabSettings(const TextEditor::TabSettings &ts);
    void reparseDocument();
    void reparseDocumentNow();
    void updateSemanticInfo();
    void updateSemanticInfoNow();
    void findUsages();
    void renameUsages();
    void showContextPane();
    virtual void setFontSettings(const TextEditor::FontSettings &);

signals:
    void outlineModelIndexChanged(const QModelIndex &index);
    void selectedElementsChanged(QList<int> offsets, const QString &wordAtCursor);
    void semanticInfoUpdated();

private slots:
    void onDocumentUpdated(QmlJS::Document::Ptr doc);
    void modificationChanged(bool);

    void jumpToOutlineElement(int index);
    void updateOutlineNow();
    void updateOutlineIndexNow();
    void updateCursorPositionNow();
    void showTextMarker();
    void updateFileName();

    void updateUses();
    void updateUsesNow();

    void acceptNewSemanticInfo(const QmlJSEditor::SemanticInfo &semanticInfo);
    void onCursorPositionChanged();
    void onRefactorMarkerClicked(const TextEditor::RefactorMarker &marker);

    void performQuickFix(int index);

protected:
    void contextMenuEvent(QContextMenuEvent *e);
    bool event(QEvent *e);
    void wheelEvent(QWheelEvent *event);
    void resizeEvent(QResizeEvent *event);
    void scrollContentsBy(int dx, int dy);
    TextEditor::BaseTextEditor *createEditor();
    void createToolBar(QmlJSEditorEditable *editable);
    TextEditor::BaseTextEditorWidget::Link findLinkAt(const QTextCursor &cursor, bool resolveTarget = true);
    QString foldReplacementText(const QTextBlock &block) const;

private:
    bool isClosingBrace(const QList<QmlJS::Token> &tokens) const;

    void setSelectedElements();
    QString wordUnderCursor() const;

    QModelIndex indexForPosition(unsigned cursorPosition, const QModelIndex &rootIndex = QModelIndex()) const;
    bool hideContextPane();

    const Core::Context m_context;

    QTimer *m_updateDocumentTimer;
    QTimer *m_updateUsesTimer;
    QTimer *m_updateSemanticInfoTimer;
    QTimer *m_updateOutlineTimer;
    QTimer *m_updateOutlineIndexTimer;
    QTimer *m_cursorPositionTimer;
    QComboBox *m_outlineCombo;
    Internal::QmlOutlineModel *m_outlineModel;
    QModelIndex m_outlineModelIndex;
    QmlJS::ModelManagerInterface *m_modelManager;
    QTextCharFormat m_occurrencesFormat;
    QTextCharFormat m_occurrencesUnusedFormat;
    QTextCharFormat m_occurrenceRenameFormat;

    Internal::SemanticInfoUpdater *m_semanticInfoUpdater;
    SemanticInfo m_semanticInfo;
    int m_futureSemanticInfoRevision;

    QList<TextEditor::QuickFixOperation::Ptr> m_quickFixes;

    QmlJS::IContextPane *m_contextPane;
    int m_oldCursorPosition;
    bool m_updateSelectedElements;

    FindReferences *m_findReferences;
    Internal::SemanticHighlighter *m_semanticHighlighter;

    friend class Internal::SemanticHighlighter;
};

} // namespace QmlJSEditor

#endif // QMLJSEDITOR_H
