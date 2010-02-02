/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "parser/qmljsast_p.h"
#include "qmljsbind.h"
#include "qmljslink.h"
#include "qmljscheck.h"
#include "qmljsmetatypesystem.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QDebug>

using namespace QmlJS;
using namespace QmlJS::AST;
using namespace QmlJS::Interpreter;

namespace {

class ASTObjectValue: public ObjectValue
{
    UiQualifiedId *_typeName;
    UiObjectInitializer *_initializer;

public:
    ASTObjectValue(UiQualifiedId *typeName, UiObjectInitializer *initializer, Interpreter::Engine *engine)
        : ObjectValue(engine), _typeName(typeName), _initializer(initializer)
    {
    }

    virtual void processMembers(MemberProcessor *processor) const
    {
        if (_initializer) {
            for (UiObjectMemberList *it = _initializer->members; it; it = it->next) {
                UiObjectMember *member = it->member;
                if (UiPublicMember *def = cast<UiPublicMember *>(member)) {
                    if (def->name && def->memberType) {
                        const QString propName = def->name->asString();
                        const QString propType = def->memberType->asString();

                        processor->processProperty(propName, engine()->defaultValueForBuiltinType(propType));
                    }
                }
            }
        }
        ObjectValue::processMembers(processor);
    }
};

class ASTFunctionValue: public FunctionValue
{
    FunctionDeclaration *_ast;
    QList<NameId *> _argumentNames;

public:
    ASTFunctionValue(FunctionDeclaration *ast, Interpreter::Engine *engine)
        : FunctionValue(engine), _ast(ast)
    {
        setPrototype(engine->functionPrototype());

        for (FormalParameterList *it = ast->formals; it; it = it->next)
            _argumentNames.append(it->name);
    }

    FunctionDeclaration *ast() const { return _ast; }

    virtual const Value *returnValue() const
    {
        return engine()->undefinedValue();
    }

    virtual int argumentCount() const
    {
        return _argumentNames.size();
    }

    virtual const Value *argument(int) const
    {
        return engine()->undefinedValue();
    }

    virtual QString argumentName(int index) const
    {
        if (index < _argumentNames.size()) {
            if (NameId *nameId = _argumentNames.at(index))
                return nameId->asString();
        }

        return FunctionValue::argumentName(index);
    }

    virtual bool isVariadic() const
    {
        return true;
    }
};

class ProcessSourceElements: protected AST::Visitor
{
    Interpreter::Engine *_interp;

public:
    ProcessSourceElements(Interpreter::Engine *interp)
        : _interp(interp),
          typeOfExpression(interp)
    {
    }

    void operator()(AST::Node *node)
    {
        if (node)
            node->accept(this);
    }

protected:
    using AST::Visitor::visit;

    virtual bool visit(FunctionDeclaration *ast)
    {
        if (ast->name)
            _interp->globalObject()->setProperty(ast->name->asString(), new ASTFunctionValue(ast, _interp));

        return false;
    }

    virtual bool visit(VariableDeclaration *ast)
    {
        if (ast->name) {
            const Value *value = _interp->undefinedValue();

            if (ast->expression)
                value = typeOfExpression(ast->expression, _interp->globalObject());

            _interp->globalObject()->setProperty(ast->name->asString(), value);
        }

        return false;
    }

    Check typeOfExpression;
};

} // end of anonymous namespace

Bind::Bind(Document::Ptr doc, Interpreter::Engine *interp)
    : _doc(doc),
      _interp(interp),
      _currentObjectValue(0),
      _typeEnvironment(0),
      _idEnvironment(0),
      _functionEnvironment(0),
      _rootObjectValue(0)
{
    if (_doc && _doc->qmlProgram() != 0) {
        UiProgram *program = _doc->qmlProgram();

        _currentObjectValue = 0;
        _typeEnvironment = _interp->newObject(/*prototype =*/ 0);
        _idEnvironment = _interp->newObject(/*prototype =*/ 0);
        _functionEnvironment = _interp->newObject(/*prototype =*/ 0);
        _rootObjectValue = 0;

        _qmlObjectDefinitions.clear();
        _qmlObjectBindings.clear();

        accept(program);
    }
}

Bind::~Bind()
{
}

QStringList Bind::includedScripts() const
{
    return _includedScripts;
}

QStringList Bind::localImports() const
{
    return _localImports;
}

ObjectValue *Bind::switchObjectValue(ObjectValue *newObjectValue)
{
    ObjectValue *oldObjectValue = _currentObjectValue;
    _currentObjectValue = newObjectValue;
    return oldObjectValue;
}

ObjectValue *Bind::scopeChainAt(Document::Ptr currentDocument, const Snapshot &snapshot,
                                Interpreter::Engine *interp, AST::UiObjectMember *currentObject)
{
    Bind *currentBind = 0;
    QList<Bind *> binds;

    QSet<QString> processed;
    QStringList todo;

    QMultiHash<QString, Document::Ptr> documentByPath;
    foreach (Document::Ptr doc, snapshot)
        documentByPath.insert(doc->path(), doc);

    todo.append(currentDocument->path());

    // Bind the reachable documents.
    while (! todo.isEmpty()) {
        const QString path = todo.takeFirst();

        if (processed.contains(path))
            continue;

        processed.insert(path);

        QStringList localImports;
        foreach (Document::Ptr doc, documentByPath.values(path)) {
            Bind *newBind = new Bind(doc, interp);
            binds += newBind;

            localImports += newBind->localImports();

            if (doc == currentDocument)
                currentBind = newBind;
        }

        localImports.removeDuplicates();
        todo += localImports;
    }

    LinkImports linkImports;
    Link link;

    // link the import directives
    linkImports(binds);

    // link the scripts
    QStringList includedScriptFiles;
    foreach (Bind *bind, binds)
        includedScriptFiles += bind->includedScripts();

    includedScriptFiles.removeDuplicates();

    ProcessSourceElements processSourceElements(interp);

    foreach (const QString &scriptFile, includedScriptFiles) {
        if (Document::Ptr scriptDoc = snapshot.document(scriptFile)) {
            if (AST::Program *program = scriptDoc->jsProgram()) {
                processSourceElements(program);
            }
        }
    }

    ObjectValue *scope = interp->globalObject();
    if (currentBind)
        scope = link(binds, currentBind, currentObject);

    qDeleteAll(binds);

    return scope;
}

QString Bind::toString(UiQualifiedId *qualifiedId, QChar delimiter)
{
    QString result;

    for (UiQualifiedId *iter = qualifiedId; iter; iter = iter->next) {
        if (iter != qualifiedId)
            result += delimiter;

        if (iter->name)
            result += iter->name->asString();
    }

    return result;
}

ExpressionNode *Bind::expression(UiScriptBinding *ast) const
{
    if (ExpressionStatement *statement = cast<ExpressionStatement *>(ast->statement))
        return statement->expression;

    return 0;
}

void Bind::processScript(AST::UiQualifiedId *qualifiedId, AST::UiObjectInitializer *initializer)
{
    Q_UNUSED(qualifiedId);

    if (! initializer)
        return;

    for (UiObjectMemberList *it = initializer->members; it; it = it->next) {
        if (UiScriptBinding *binding = cast<UiScriptBinding *>(it->member)) {
            const QString bindingName = toString(binding->qualifiedId);
            if (bindingName == QLatin1String("source")) {
                if (StringLiteral *literal = cast<StringLiteral *>(expression(binding))) {
                    QFileInfo fileInfo(QDir(_doc->path()), literal->value->asString());
                    const QString scriptPath = fileInfo.absoluteFilePath();
                    _includedScripts.append(scriptPath);
                }
            }
        } else if (UiSourceElement *binding = cast<UiSourceElement *>(it->member)) {
            if (FunctionDeclaration *decl = cast<FunctionDeclaration *>(binding->sourceElement)) {
                accept(decl); // process the function declaration
            } else {
                // ### unexpected source element
            }
        } else {
            // ### unexpected binding.
        }
    }
}

ObjectValue *Bind::bindObject(UiQualifiedId *qualifiedTypeNameId, UiObjectInitializer *initializer)
{
    ObjectValue *parentObjectValue = 0;
    const QString typeName = toString(qualifiedTypeNameId);

    if (typeName == QLatin1String("Script")) {
        // Script blocks all contribute to the same scope
        parentObjectValue = switchObjectValue(_functionEnvironment);
        processScript(qualifiedTypeNameId, initializer);
        return switchObjectValue(parentObjectValue);
    }

    // normal component instance
    ASTObjectValue *objectValue = new ASTObjectValue(qualifiedTypeNameId, initializer, _interp);
    parentObjectValue = switchObjectValue(objectValue);

    if (parentObjectValue)
        objectValue->setProperty(QLatin1String("parent"), parentObjectValue);
    else
        _rootObjectValue = objectValue;

    accept(initializer);

    return switchObjectValue(parentObjectValue);
}

void Bind::accept(Node *node)
{
    Node::accept(node, this);
}

/*
  import Qt 4.6
  import Qt 4.6 as Xxx
  (import com.nokia.qt is the same as the ones above)

  import "content"
  import "content" as Xxx
  import "content" 4.6
  import "content" 4.6 as Xxx

  import "http://www.ovi.com/" as Ovi
 */
// ### TODO: Move to LinkImports
bool Bind::visit(UiImport *ast)
{
    if (! (ast->importUri || ast->fileName))
        return false; // nothing to do.

    ObjectValue *namespaceObject = 0;

    if (ast->asToken.isValid()) { // with namespace we insert an object in the type env. to hold the imported types
        if (!ast->importId)
            return false; // this should never happen, but better be safe than sorry

        namespaceObject = _interp->newObject(/*prototype */ 0);

        _typeEnvironment->setProperty(ast->importId->asString(), namespaceObject);

    } else { // without namespace we insert all types directly into the type env.
        namespaceObject = _typeEnvironment;
    }

    // look at files first

    // else try the metaobject system
    if (ast->importUri) {
        const QString package = toString(ast->importUri, '/');
        int majorVersion = -1; // ### TODO: Check these magic version numbers
        int minorVersion = -1; // ### TODO: Check these magic version numbers

        if (ast->versionToken.isValid()) {
            const QString versionString = _doc->source().mid(ast->versionToken.offset, ast->versionToken.length);
            const int dotIdx = versionString.indexOf(QLatin1Char('.'));
            if (dotIdx == -1) {
                // only major (which is probably invalid, but let's handle it anyway)
                majorVersion = versionString.toInt();
                minorVersion = 0; // ### TODO: Check with magic version numbers above
            } else {
                majorVersion = versionString.left(dotIdx).toInt();
                minorVersion = versionString.mid(dotIdx + 1).toInt();
            }
        }
#ifndef NO_DECLARATIVE_BACKEND
        foreach (QmlObjectValue *object, _interp->metaTypeSystem().staticTypesForImport(package, majorVersion, minorVersion)) {
            namespaceObject->setProperty(object->qmlTypeName(), object);
        }
#endif // NO_DECLARATIVE_BACKEND
    } else if (ast->fileName) {
        QString path = _doc->path();
        path += QLatin1Char('/');
        path += ast->fileName->asString();
        path = QDir::cleanPath(path);
        _localImports.append(path);
    }

    return false;
}

bool Bind::visit(UiPublicMember *)
{
    // nothing to do.
    return false;
}

bool Bind::visit(UiObjectDefinition *ast)
{
    ObjectValue *value = bindObject(ast->qualifiedTypeNameId, ast->initializer);
    _qmlObjectDefinitions.insert(ast, value);

    return false;
}

bool Bind::visit(UiObjectBinding *ast)
{
//    const QString name = serialize(ast->qualifiedId);
    ObjectValue *value = bindObject(ast->qualifiedTypeNameId, ast->initializer);
    _qmlObjectBindings.insert(ast, value);
    // ### FIXME: we don't handle dot-properties correctly (i.e. font.size)
//    _currentObjectValue->setProperty(name, value);

    return false;
}

bool Bind::visit(UiScriptBinding *ast)
{
    if (toString(ast->qualifiedId) == QLatin1String("id")) {
        if (ExpressionStatement *e = cast<ExpressionStatement*>(ast->statement))
            if (IdentifierExpression *i = cast<IdentifierExpression*>(e->expression))
                if (i->name)
                    _idEnvironment->setProperty(i->name->asString(), _currentObjectValue);
    }

    return false;
}

bool Bind::visit(UiArrayBinding *)
{
    // ### FIXME: do we need to store the members into the property? Or, maybe the property type is an JS Array?

    return true;
}

bool Bind::visit(FunctionDeclaration *ast)
{
    if (!ast->name)
        return false;
    // the first declaration counts
    if (_currentObjectValue->property(ast->name->asString()))
        return false;

    ASTFunctionValue *function = new ASTFunctionValue(ast, _interp);
    _currentObjectValue->setProperty(ast->name->asString(), function);
    return false; // ### eventually want to visit function bodies
}
