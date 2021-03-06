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

#include "debuggerplugin.h"

#include "debuggerstartparameters.h"
#include "debuggeractions.h"
#include "debuggerconstants.h"
#include "debuggerinternalconstants.h"
#include "debuggercore.h"
#include "debuggerdialogs.h"
#include "debuggerengine.h"
#include "debuggermainwindow.h"
#include "debuggerrunner.h"
#include "debuggerruncontrolfactory.h"
#include "debuggerstringutils.h"
#include "memoryagent.h"
#include "breakpoint.h"
#include "breakhandler.h"
#include "breakwindow.h"
#include "qtmessagelogwindow.h"
#include "disassemblerlines.h"
#include "logwindow.h"
#include "moduleswindow.h"
#include "moduleshandler.h"
#include "registerwindow.h"
#include "snapshotwindow.h"
#include "stackhandler.h"
#include "stackwindow.h"
#include "sourcefileswindow.h"
#include "threadswindow.h"
#include "watchhandler.h"
#include "watchwindow.h"
#include "watchutils.h"
#include "debuggertooltipmanager.h"

#include "snapshothandler.h"
#include "threadshandler.h"
#include "commonoptionspage.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/id.h>
#include <coreplugin/imode.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/findplaceholder.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/imode.h>
#include <coreplugin/icorelistener.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/modemanager.h>

#include <cppeditor/cppeditorconstants.h>
#include <cpptools/ModelManagerInterface.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/invoker.h>

#include <projectexplorer/abi.h>
#include <projectexplorer/applicationrunconfiguration.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorersettings.h>
#include <projectexplorer/project.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainmanager.h>

#include <qtsupport/qtsupportconstants.h>

#include <texteditor/basetexteditor.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>

#include <utils/qtcassert.h>
#include <utils/savedaction.h>
#include <utils/styledbar.h>
#include <utils/proxyaction.h>
#include <utils/statuslabel.h>
#include <utils/fileutils.h>

#include <QTimer>
#include <QtPlugin>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>
#include <QTreeWidget>
#include <QInputDialog>

#ifdef WITH_TESTS
#include <QTest>
#include <QSignalSpy>
#include <QTestEventLoop>

//#define WITH_BENCHMARK
#ifdef WITH_BENCHMARK
#include <valgrind/callgrind.h>
#endif

#endif // WITH_TESTS

#include <climits>

#define DEBUG_STATE 1
#ifdef DEBUG_STATE
//#   define STATE_DEBUG(s)
//    do { QString msg; QTextStream ts(&msg); ts << s;
//      showMessage(msg, LogDebug); } while (0)
#   define STATE_DEBUG(s) do { qDebug() << s; } while(0)
#else
#   define STATE_DEBUG(s)
#endif

/*!
    \namespace Debugger
    Debugger plugin namespace
*/

/*!
    \namespace Debugger::Internal
    Internal namespace of the Debugger plugin
    \internal
*/

/*!
    \class Debugger::DebuggerEngine

    \brief Base class of a debugger engine.

    Note: the Debugger process itself and any helper processes like
    gdbserver, the CODA client etc are referred to as 'Engine',
    whereas the debugged process is referred to as 'Inferior'.

    Transitions marked by '---' are done in the individual engines.
    Transitions marked by '+-+' are done in the base DebuggerEngine.
    Transitions marked by '*' are done asynchronously.

    The GdbEngine->setupEngine() function is described in more detail below.

    The engines are responsible for local roll-back to the last
    acknowledged state before calling notify*Failed. I.e. before calling
    notifyEngineSetupFailed() any process started during setupEngine()
    so far must be terminated.
    \code

                        DebuggerNotReady
                         progressmanager/progressmanager.cpp      +
                      EngineSetupRequested
                               +
                  (calls *Engine->setupEngine())
                            |      |
                            |      |
                       {notify-  {notify-
                        Engine-   Engine-
                        SetupOk}  SetupFailed}
                            +      +
                            +      `+-+-+> EngineSetupFailed
                            +                   +
                            +    [calls RunControl->startFailed]
                            +                   +
                            +             DebuggerFinished
                            v
                      EngineSetupOk
                            +
             [calls RunControl->StartSuccessful]
                            +
                  InferiorSetupRequested
                            +
             (calls *Engine->setupInferior())
                         |       |
                         |       |
                    {notify-   {notify-
                     Inferior- Inferior-
                     SetupOk}  SetupFailed}
                         +       +
                         +       ` +-+-> InferiorSetupFailed +-+-+-+-+-+->.
                         +                                                +
                  InferiorSetupOk                                         +
                         +                                                +
                  EngineRunRequested                                      +
                         +                                                +
                 (calls *Engine->runEngine())                             +
               /       |            |        \                            +
             /         |            |          \                          +
            | (core)   | (attach)   |           |                         +
            |          |            |           |                         +
      {notify-    {notifyER&- {notifyER&-  {notify-                       +
      Inferior-     Inferior-   Inferior-  EngineRun-                     +
     Unrunnable}     StopOk}     RunOk}     Failed}                       +
           +           +            +           +                         +
   InferiorUnrunnable  +     InferiorRunOk      +                         +
                       +                        +                         +
                InferiorStopOk            EngineRunFailed                 +
                                                +                         v
                                                 `-+-+-+-+-+-+-+-+-+-+-+>-+
                                                                          +
                                                                          +
                       #Interrupt@InferiorRunOk#                          +
                                  +                                       +
                          InferiorStopRequested                           +
  #SpontaneousStop                +                                       +
   @InferiorRunOk#         (calls *Engine->                               +
          +               interruptInferior())                            +
      {notify-               |          |                                 +
     Spontaneous-       {notify-    {notify-                              +
      Inferior-          Inferior-   Inferior-                            +
       StopOk}           StopOk}    StopFailed}                           +
           +              +             +                                 +
            +            +              +                                 +
            InferiorStopOk              +                                 +
                  +                     +                                 +
                  +                    +                                  +
                  +                   +                                   +
        #Stop@InferiorUnrunnable#    +                                    +
          #Creator Close Event#     +                                     +
                       +           +                                      +
                InferiorShutdownRequested                                 +
                            +                                             +
           (calls *Engine->shutdownInferior())                            +
                         |        |                                       +
                    {notify-   {notify-                                   +
                     Inferior- Inferior-                                  +
                  ShutdownOk}  ShutdownFailed}                            +
                         +        +                                       +
                         +        +                                       +
  #Inferior exited#      +        +                                       +
         |               +        +                                       +
   {notifyInferior-      +        +                                       +
      Exited}            +        +                                       +
           +             +        +                                       +
     InferiorExitOk      +        +                                       +
             +           +        +                                       +
            InferiorShutdownOk InferiorShutdownFailed                     +
                      *          *                                        +
                  EngineShutdownRequested                                 +
                            +                                             +
           (calls *Engine->shutdownEngine())  <+-+-+-+-+-+-+-+-+-+-+-+-+-+'
                         |        |
                         |        |
                    {notify-   {notify-
                     Engine-    Engine-
                  ShutdownOk}  ShutdownFailed}
                         +       +
            EngineShutdownOk  EngineShutdownFailed
                         *       *
                     DebuggerFinished

\endcode */

/* Here is a matching graph as a GraphViz graph. View it using
 * \code
grep "^sg1:" debuggerplugin.cpp | cut -c5- | dot -osg1.ps -Tps && gv sg1.ps

sg1: digraph DebuggerStates {
sg1:   DebuggerNotReady -> EngineSetupRequested
sg1:   EngineSetupRequested -> EngineSetupOk [ label="notifyEngineSetupOk", style="dashed" ];
sg1:   EngineSetupRequested -> EngineSetupFailed [ label= "notifyEngineSetupFailed", style="dashed"];
sg1:   EngineSetupFailed -> DebuggerFinished [ label= "RunControl::StartFailed" ];
sg1:   EngineSetupOk -> InferiorSetupRequested [ label= "RunControl::StartSuccessful" ];
sg1:   InferiorSetupRequested -> InferiorSetupOk [ label="notifyInferiorSetupOk", style="dashed" ];
sg1:   InferiorSetupRequested -> InferiorSetupFailed [ label="notifyInferiorFailed", style="dashed" ];
sg1:   InferiorSetupOk -> EngineRunRequested
sg1:   InferiorSetupFailed -> EngineShutdownRequested
sg1:   EngineRunRequested -> InferiorUnrunnable [ label="notifyInferiorUnrunnable", style="dashed" ];
sg1:   EngineRunRequested -> InferiorStopOk [ label="notifyEngineRunAndInferiorStopOk", style="dashed" ];
sg1:   EngineRunRequested -> InferiorRunOk [ label="notifyEngineRunAndInferiorRunOk", style="dashed" ];
sg1:   EngineRunRequested -> EngineRunFailed [ label="notifyEngineRunFailed", style="dashed" ];
sg1:   EngineRunFailed -> EngineShutdownRequested
sg1:   InferiorRunOk -> InferiorStopOk [ label="SpontaneousStop\nnotifyInferiorSpontaneousStop", style="dashed" ];
sg1:   InferiorRunOk -> InferiorStopRequested [ label="User stop\nEngine::interruptInferior", style="dashed"];
sg1:   InferiorStopRequested -> InferiorStopOk [ label="notifyInferiorStopOk", style="dashed" ];
sg1:   InferiorStopRequested -> InferiorShutdownRequested  [ label="notifyInferiorStopFailed", style="dashed" ];
sg1:   InferiorStopOk -> InferiorRunRequested [ label="User\nEngine::continueInferior" ];
sg1:   InferiorRunRequested -> InferiorRunOk [ label="notifyInferiorRunOk", style="dashed"];
sg1:   InferiorRunRequested -> InferiorRunFailed [ label="notifyInferiorRunFailed", style="dashed"];
sg1:   InferiorRunFailed -> InferiorStopOk
sg1:   InferiorStopOk -> InferiorShutdownRequested [ label="Close event" ];
sg1:   InferiorUnrunnable -> InferiorShutdownRequested [ label="Close event" ];
sg1:   InferiorShutdownRequested -> InferiorShutdownOk [ label= "Engine::shutdownInferior\nnotifyInferiorShutdownOk", style="dashed" ];
sg1:   InferiorShutdownRequested -> InferiorShutdownFailed [ label="Engine::shutdownInferior\nnotifyInferiorShutdownFailed", style="dashed" ];
sg1:   InferiorExited -> InferiorExitOk [ label="notifyInferiorExited", style="dashed"];
sg1:   InferiorExitOk -> InferiorShutdownOk
sg1:   InferiorShutdownOk -> EngineShutdownRequested
sg1:   InferiorShutdownFailed -> EngineShutdownRequested
sg1:   EngineShutdownRequested -> EngineShutdownOk [ label="Engine::shutdownEngine\nnotifyEngineShutdownOk", style="dashed" ];
sg1:   EngineShutdownRequested -> EngineShutdownFailed  [ label="Engine::shutdownEngine\nnotifyEngineShutdownFailed", style="dashed" ];
sg1:   EngineShutdownOk -> DebuggerFinished  [ style = "dotted" ];
sg1:   EngineShutdownFailed  -> DebuggerFinished [ style = "dotted" ];
sg1: }
* \endcode */
// Additional signalling:    {notifyInferiorIll}   {notifyEngineIll}


/*!
    \class Debugger::Internal::GdbEngine
    \brief Implementation of Debugger::Engine driving a gdb executable.

    GdbEngine specific startup. All happens in EngineSetupRequested state:

    Transitions marked by '---' are done in the individual adapters.

    Transitions marked by '+-+' are done in the GdbEngine.

    \code
                  GdbEngine::setupEngine()
                          +
            (calls *Adapter->startAdapter())
                          |      |
                          |      `---> handleAdapterStartFailed()
                          |                   +
                          |             {notifyEngineSetupFailed}
                          |
                 handleAdapterStarted()
                          +
                 {notifyEngineSetupOk}



                GdbEngine::setupInferior()
                          +
            (calls *Adapter->prepareInferior())
                          |      |
                          |      `---> handlePrepareInferiorFailed()
                          |                   +
                          |             {notifyInferiorSetupFailed}
                          |
                handleInferiorPrepared()
                          +
                {notifyInferiorSetupOk}

\endcode */

using namespace Core;
using namespace Debugger::Constants;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace ExtensionSystem;

namespace CC = Core::Constants;
namespace PE = ProjectExplorer::Constants;


namespace Debugger {
namespace Constants {

#ifdef Q_OS_MAC
const char STOP_KEY[]                     = "Shift+Ctrl+Y";
const char RESET_KEY[]                    = "Ctrl+Shift+F5";
const char STEP_KEY[]                     = "Ctrl+Shift+I";
const char STEPOUT_KEY[]                  = "Ctrl+Shift+T";
const char NEXT_KEY[]                     = "Ctrl+Shift+O";
const char REVERSE_KEY[]                  = "";
const char RUN_TO_LINE_KEY[]              = "Shift+F8";
const char RUN_TO_SELECTED_FUNCTION_KEY[] = "Ctrl+F6";
const char JUMP_TO_LINE_KEY[]             = "Ctrl+D,Ctrl+L";
const char TOGGLE_BREAK_KEY[]             = "F8";
const char BREAK_BY_FUNCTION_KEY[]        = "Ctrl+D,Ctrl+F";
const char BREAK_AT_MAIN_KEY[]            = "Ctrl+D,Ctrl+M";
const char ADD_TO_WATCH_KEY[]             = "Ctrl+D,Ctrl+W";
const char SNAPSHOT_KEY[]                 = "Ctrl+D,Ctrl+S";
#else
const char STOP_KEY[]                     = "Shift+F5";
const char RESET_KEY[]                    = "Ctrl+Shift+F5";
const char STEP_KEY[]                     = "F11";
const char STEPOUT_KEY[]                  = "Shift+F11";
const char NEXT_KEY[]                     = "F10";
const char REVERSE_KEY[]                  = "F12";
const char RUN_TO_LINE_KEY[]              = "Ctrl+F10";
const char RUN_TO_SELECTED_FUNCTION_KEY[] = "Ctrl+F6";
const char JUMP_TO_LINE_KEY[]             = "";
const char TOGGLE_BREAK_KEY[]             = "F9";
const char BREAK_BY_FUNCTION_KEY[]        = "";
const char BREAK_AT_MAIN_KEY[]            = "";
const char ADD_TO_WATCH_KEY[]             = "Ctrl+Alt+Q";
const char SNAPSHOT_KEY[]                 = "Ctrl+D,Ctrl+S";
#endif

} // namespace Constants


namespace Internal {

// To be passed through margin menu action's data
struct BreakpointMenuContextData : public ContextData
{
    enum Mode
    {
        Breakpoint,
        MessageTracePoint
    };

    BreakpointMenuContextData() : mode(Breakpoint) {}
    Mode mode;
};

struct TestCallBack
{
    TestCallBack() : receiver(0), slot(0) {}
    TestCallBack(QObject *ob, const char *s) : receiver(ob), slot(s) {}

    QObject *receiver;
    const char *slot;
    QVariant cookie;
};


} // namespace Internal
} // namespace Debugger

Q_DECLARE_METATYPE(Debugger::Internal::BreakpointMenuContextData)
Q_DECLARE_METATYPE(Debugger::Internal::TestCallBack)


namespace Debugger {
namespace Internal {

// FIXME: Outdated?
// The createCdbEngine function takes a list of options pages it can add to.
// This allows for having a "enabled" toggle on the page independently
// of the engine. That's good for not enabling the related ActiveX control
// unnecessarily.

void addCdbOptionPages(QList<IOptionsPage*> *opts);
void addGdbOptionPages(QList<IOptionsPage*> *opts);
void addScriptOptionPages(QList<IOptionsPage*> *opts);
void addTcfOptionPages(QList<IOptionsPage*> *opts);

#ifdef WITH_LLDB
void addLldbOptionPages(QList<IOptionsPage*> *opts);
#endif

static SessionManager *sessionManager()
{
    return ProjectExplorerPlugin::instance()->session();
}

static QToolButton *toolButton(QAction *action)
{
    QToolButton *button = new QToolButton;
    button->setDefaultAction(action);
    return button;
}

static Abi anyAbiOfBinary(const QString &fileName)
{
    QList<Abi> abis = Abi::abisOfBinary(Utils::FileName::fromString(fileName));
    if (abis.isEmpty())
        return Abi();
    return abis.at(0);
}

///////////////////////////////////////////////////////////////////////
//
// DummyEngine
//
///////////////////////////////////////////////////////////////////////

class DummyEngine : public DebuggerEngine
{
    Q_OBJECT

public:
    DummyEngine() : DebuggerEngine(DebuggerStartParameters(), AnyLanguage) {}
    ~DummyEngine() {}

    void setupEngine() {}
    void setupInferior() {}
    void runEngine() {}
    void shutdownEngine() {}
    void shutdownInferior() {}
    bool hasCapability(unsigned cap) const;
    bool acceptsBreakpoint(BreakpointModelId) const { return false; }
    bool acceptsDebuggerCommands() const { return false; }
};

bool DummyEngine::hasCapability(unsigned cap) const
{
    // This can only be a first approximation of what to expect when running.
    Project *project = ProjectExplorerPlugin::currentProject();
    if (!project)
        return 0;
    Target *target = project->activeTarget();
    QTC_ASSERT(target, return 0);
    RunConfiguration *activeRc = target->activeRunConfiguration();
    QTC_ASSERT(activeRc, return 0);

    // This is a non-started Cdb or Gdb engine:
    if (activeRc->debuggerAspect()->useCppDebugger())
        return cap & (WatchpointByAddressCapability
               | BreakConditionCapability
               | TracePointCapability
               | OperateByInstructionCapability);

    // This is a Qml or unknown engine.
    return cap & AddWatcherCapability;
}

///////////////////////////////////////////////////////////////////////
//
// DebugMode
//
///////////////////////////////////////////////////////////////////////

class DebugMode : public IMode
{
public:
    DebugMode()
    {
        setObjectName(QLatin1String("DebugMode"));
        setContext(Context(CC::C_EDITORMANAGER, C_DEBUGMODE, CC::C_NAVIGATION_PANE));
        setDisplayName(DebuggerPlugin::tr("Debug"));
        setIcon(QIcon(QLatin1String(":/fancyactionbar/images/mode_Debug.png")));
        setPriority(85);
        setId(QLatin1String(MODE_DEBUG));
        setType(QLatin1String(CC::MODE_EDIT_TYPE));
    }

    ~DebugMode()
    {
        // Make sure the editor manager does not get deleted.
        //EditorManager::instance()->setParent(0);
        delete m_widget;
    }
};

///////////////////////////////////////////////////////////////////////
//
// Misc
//
///////////////////////////////////////////////////////////////////////

static TextEditor::ITextEditor *currentTextEditor()
{
    if (const Core::EditorManager *editorManager = Core::EditorManager::instance())
            if (Core::IEditor *editor = editorManager->currentEditor())
                return qobject_cast<TextEditor::ITextEditor*>(editor);
    return 0;
}

static bool currentTextEditorPosition(ContextData *data)
{
    TextEditor::ITextEditor *textEditor = currentTextEditor();
    if (!textEditor)
        return false;
    const Core::IDocument *document = textEditor->document();
    QTC_ASSERT(document, return false);
    data->fileName = document->fileName();
    if (textEditor->property("DisassemblerView").toBool()) {
        int lineNumber = textEditor->currentLine();
        QString line = textEditor->contents()
            .section(QLatin1Char('\n'), lineNumber - 1, lineNumber - 1);
        data->address = DisassemblerLine::addressFromDisassemblyLine(line);
    } else {
        data->lineNumber = textEditor->currentLine();
    }
    return true;
}

///////////////////////////////////////////////////////////////////////
//
// DebuggerPluginPrivate
//
///////////////////////////////////////////////////////////////////////

static DebuggerPluginPrivate *theDebuggerCore = 0;

/*!
    \class Debugger::Internal::DebuggerCore

    This is the "internal" interface of the debugger plugin that's
    used by debugger views and debugger engines. The interface is
    implemented in DebuggerPluginPrivate.
*/

/*!
    \class Debugger::Internal::DebuggerPluginPrivate

    Implementation of DebuggerCore.
*/

class DebuggerPluginPrivate : public DebuggerCore
{
    Q_OBJECT

public:
    explicit DebuggerPluginPrivate(DebuggerPlugin *plugin);
    ~DebuggerPluginPrivate();

    bool initialize(const QStringList &arguments, QString *errorMessage);
    void extensionsInitialized();
    void aboutToShutdown();

    void connectEngine(DebuggerEngine *engine);
    void disconnectEngine() { connectEngine(0); }
    DebuggerEngine *currentEngine() const { return m_currentEngine; }
    DebuggerEngine *dummyEngine();

    void setThreads(const QStringList &list, int index)
    {
        const bool state = m_threadBox->blockSignals(true);
        m_threadBox->clear();
        foreach (const QString &item, list)
            m_threadBox->addItem(item);
        m_threadBox->setCurrentIndex(index);
        m_threadBox->blockSignals(state);
    }

public slots:
    void writeSettings()
    {
        m_debuggerSettings->writeSettings();
        m_mainWindow->writeSettings();
    }

    void selectThread(int index)
    {
        currentEngine()->selectThread(index);
    }

    void breakpointSetMarginActionTriggered()
    {
        const QAction *action = qobject_cast<const QAction *>(sender());
        QTC_ASSERT(action, return);
        const BreakpointMenuContextData data =
            action->data().value<BreakpointMenuContextData>();
        QString message;
        if (data.mode == BreakpointMenuContextData::MessageTracePoint) {
            if (data.address) {
                //: Message tracepoint: Address hit.
                message = tr("0x%1 hit").arg(data.address, 0, 16);
            } else {
                //: Message tracepoint: %1 file, %2 line %3 function hit.
                message = tr("%1:%2 %3() hit").arg(QFileInfo(data.fileName).fileName()).
                        arg(data.lineNumber).
                        arg(cppFunctionAt(data.fileName, data.lineNumber));
            }
            QInputDialog dialog; // Create wide input dialog.
            dialog.setWindowFlags(dialog.windowFlags()
              & ~(Qt::WindowContextHelpButtonHint|Qt::MSWindowsFixedSizeDialogHint));
            dialog.resize(600, dialog.height());
            dialog.setWindowTitle(tr("Add Message Tracepoint"));
            dialog.setLabelText (tr("Message:"));
            dialog.setTextValue(message);
            if (dialog.exec() != QDialog::Accepted || dialog.textValue().isEmpty())
                return;
            message = dialog.textValue();
        }
        if (data.address)
            toggleBreakpointByAddress(data.address, message);
        else
            toggleBreakpointByFileAndLine(data.fileName, data.lineNumber, message);
    }

    void breakpointRemoveMarginActionTriggered()
    {
        const QAction *act = qobject_cast<QAction *>(sender());
        QTC_ASSERT(act, return);
        BreakpointModelId id = act->data().value<BreakpointModelId>();
        m_breakHandler->removeBreakpoint(id);
     }

    void breakpointEnableMarginActionTriggered()
    {
        const QAction *act = qobject_cast<QAction *>(sender());
        QTC_ASSERT(act, return);
        BreakpointModelId id = act->data().value<BreakpointModelId>();
        breakHandler()->setEnabled(id, true);
    }

    void breakpointDisableMarginActionTriggered()
    {
        const QAction *act = qobject_cast<QAction *>(sender());
        QTC_ASSERT(act, return);
        BreakpointModelId id = act->data().value<BreakpointModelId>();;
        breakHandler()->setEnabled(id, false);
    }

    void updateWatchersHeader(int section, int, int newSize)
    {
        m_watchersWindow->header()->resizeSection(section, newSize);
        m_returnWindow->header()->resizeSection(section, newSize);
    }


    void sourceFilesDockToggled(bool on)
    {
        if (on && m_currentEngine->state() == InferiorStopOk)
            m_currentEngine->reloadSourceFiles();
    }

    void modulesDockToggled(bool on)
    {
        if (on && m_currentEngine->state() == InferiorStopOk)
            m_currentEngine->reloadModules();
    }

    void registerDockToggled(bool on)
    {
        if (on && m_currentEngine->state() == InferiorStopOk)
            m_currentEngine->reloadRegisters();
    }

    void synchronizeBreakpoints()
    {
        showMessage(QLatin1String("ATTEMPT SYNC"), LogDebug);
        for (int i = 0, n = m_snapshotHandler->size(); i != n; ++i) {
            if (DebuggerEngine *engine = m_snapshotHandler->at(i))
                engine->attemptBreakpointSynchronization();
        }
    }

    void synchronizeWatchers()
    {
        for (int i = 0, n = m_snapshotHandler->size(); i != n; ++i) {
            if (DebuggerEngine *engine = m_snapshotHandler->at(i))
                engine->watchHandler()->updateWatchers();
        }
    }

    void editorOpened(Core::IEditor *editor);
    void updateBreakMenuItem(Core::IEditor *editor);
    void setBusyCursor(bool busy);
    void requestMark(TextEditor::ITextEditor *editor,
                     int lineNumber,
                     TextEditor::ITextEditor::MarkRequestKind kind);
    void requestContextMenu(TextEditor::ITextEditor *editor,
        int lineNumber, QMenu *menu);

    void activatePreviousMode();
    void activateDebugMode();
    void toggleBreakpoint();
    void toggleBreakpointByFileAndLine(const QString &fileName, int lineNumber,
                                       const QString &tracePointMessage = QString());
    void toggleBreakpointByAddress(quint64 address,
                                   const QString &tracePointMessage = QString());
    void onModeChanged(Core::IMode *mode);
    void onCoreAboutToOpen();
    void showSettingsDialog();
    void updateDebugWithoutDeployMenu();

    void debugProject();
    void debugProjectWithoutDeploy();
    void debugProjectBreakMain();
    void startExternalApplication();
    void startRemoteCdbSession();
    void startRemoteProcess();
    void startRemoteServer();
    bool queryRemoteParameters(DebuggerStartParameters &sp, bool useScript);
    void attachToRemoteServer();
    void attachToRemoteProcess();
    void attachToQmlPort();
    void startRemoteEngine();
    void attachExternalApplication();
    Q_SLOT void attachExternalApplication(ProjectExplorer::RunControl*rc);
    void runScheduled();
    void attachCore();
    void attachToRemoteServer(const QString &spec);

    void enableReverseDebuggingTriggered(const QVariant &value);
    void languagesChanged();
    void showStatusMessage(const QString &msg, int timeout = -1);
    void openMemoryEditor();

    const CPlusPlus::Snapshot &cppCodeModelSnapshot() const;

    void showQtDumperLibraryWarning(const QString &details);
    DebuggerMainWindow *mainWindow() const { return m_mainWindow; }
    bool isDockVisible(const QString &objectName) const
        { return mainWindow()->isDockVisible(objectName); }

    bool hasSnapshots() const { return m_snapshotHandler->size(); }
    void createNewDock(QWidget *widget);

    void runControlStarted(DebuggerEngine *engine);
    void runControlFinished(DebuggerEngine *engine);
    DebuggerLanguages activeLanguages() const;
    unsigned enabledEngines() const { return m_cmdLineEnabledEngines; }
    QString debuggerForAbi(const Abi &abi, DebuggerEngineType et = NoEngineType) const;
    void remoteCommand(const QStringList &options, const QStringList &);

    bool isReverseDebugging() const;

    BreakHandler *breakHandler() const { return m_breakHandler; }
    SnapshotHandler *snapshotHandler() const { return m_snapshotHandler; }

    void setConfigValue(const QString &name, const QVariant &value);
    QVariant configValue(const QString &name) const;

    DebuggerRunControl *createDebugger(const DebuggerStartParameters &sp,
        RunConfiguration *rc = 0);
    void startDebugger(RunControl *runControl);
    void displayDebugger(DebuggerEngine *engine, bool updateEngine = true);

    void dumpLog();
    void cleanupViews();
    void setInitialState();

    void fontSettingsChanged(const TextEditor::FontSettings &settings);

    void updateState(DebuggerEngine *engine);
    void updateWatchersWindow();
    void onCurrentProjectChanged(ProjectExplorer::Project *project);

    void sessionLoaded();
    void aboutToUnloadSession();
    void aboutToSaveSession();

    void executeDebuggerCommand(const QString &command, DebuggerLanguages languages);
    bool evaluateScriptExpression(const QString &expression);
    void coreShutdown();

#ifdef WITH_TESTS
public slots:
    void testLoadProject(const QString &proFile, const TestCallBack &cb);
    void testProjectLoaded(ProjectExplorer::Project *project);
    void testUnloadProject();
    void testFinished();

    void testRunProject(const DebuggerStartParameters &sp, const TestCallBack &cb);
    void testRunControlFinished();

    void testPythonDumpers1();
    void testPythonDumpers2();
    void testPythonDumpers3();

    void testStateMachine1();
    void testStateMachine2();
    void testStateMachine3();

    void testBenchmark1();

public:
    bool m_testSuccess;
    QList<TestCallBack> m_testCallbacks;

#endif

public slots:
    void updateDebugActions();

    void handleExecDetach()
    {
        currentEngine()->resetLocation();
        currentEngine()->detachDebugger();
    }

    void handleExecContinue()
    {
        currentEngine()->resetLocation();
        currentEngine()->continueInferior();
    }

    void handleExecInterrupt()
    {
        currentEngine()->resetLocation();
        currentEngine()->requestInterruptInferior();
    }

    void handleAbort()
    {
        currentEngine()->resetLocation();
        currentEngine()->abortDebugger();
    }

    void handleExecStep()
    {
        if (currentEngine()->state() == DebuggerNotReady) {
            debugProjectBreakMain();
        } else {
            currentEngine()->resetLocation();
            if (boolSetting(OperateByInstruction))
                currentEngine()->executeStepI();
            else
                currentEngine()->executeStep();
        }
    }

    void handleExecNext()
    {
        if (currentEngine()->state() == DebuggerNotReady) {
            debugProjectBreakMain();
        } else {
            currentEngine()->resetLocation();
            if (boolSetting(OperateByInstruction))
                currentEngine()->executeNextI();
            else
                currentEngine()->executeNext();
        }
    }

    void handleExecStepOut()
    {
        currentEngine()->resetLocation();
        currentEngine()->executeStepOut();
    }

    void handleExecReturn()
    {
        currentEngine()->resetLocation();
        currentEngine()->executeReturn();
    }

    void handleExecJumpToLine()
    {
        //removeTooltip();
        currentEngine()->resetLocation();
        ContextData data;
        if (currentTextEditorPosition(&data))
            currentEngine()->executeJumpToLine(data);
    }

    void handleExecRunToLine()
    {
        //removeTooltip();
        currentEngine()->resetLocation();
        ContextData data;
        if (currentTextEditorPosition(&data))
            currentEngine()->executeRunToLine(data);
    }

    void handleExecRunToSelectedFunction()
    {
        ITextEditor *textEditor = currentTextEditor();
        QTC_ASSERT(textEditor, return);
        QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(textEditor->widget());
        if (!ed)
            return;
        QTextCursor cursor = ed->textCursor();
        QString functionName = cursor.selectedText();
        if (functionName.isEmpty()) {
            const QTextBlock block = cursor.block();
            const QString line = block.text();
            foreach (const QString &str, line.trimmed().split(QLatin1Char('('))) {
                QString a;
                for (int i = str.size(); --i >= 0; ) {
                    if (!str.at(i).isLetterOrNumber())
                        break;
                    a = str.at(i) + a;
                }
                if (!a.isEmpty()) {
                    functionName = a;
                    break;
                }
            }
        }

        if (functionName.isEmpty()) {
            showStatusMessage(tr("No function selected."));
        } else {
            showStatusMessage(tr("Running to function \"%1\".")
                .arg(functionName));
            currentEngine()->resetLocation();
            currentEngine()->executeRunToFunction(functionName);
        }
    }

    void slotEditBreakpoint()
    {
        const QAction *act = qobject_cast<QAction *>(sender());
        QTC_ASSERT(act, return);
        const BreakpointModelId id = act->data().value<BreakpointModelId>();
        QTC_ASSERT(id > 0, return);
        BreakTreeView::editBreakpoint(id, mainWindow());
    }

    void slotRunToLine()
    {
        const QAction *action = qobject_cast<const QAction *>(sender());
        QTC_ASSERT(action, return);
        const BreakpointMenuContextData data = action->data().value<BreakpointMenuContextData>();
        currentEngine()->executeRunToLine(data);
    }

    void slotJumpToLine()
    {
        const QAction *action = qobject_cast<const QAction *>(sender());
        QTC_ASSERT(action, return);
        const BreakpointMenuContextData data = action->data().value<BreakpointMenuContextData>();
        currentEngine()->executeJumpToLine(data);
    }

    void slotDisassembleFunction()
    {
        const QAction *action = qobject_cast<const QAction *>(sender());
        QTC_ASSERT(action, return);
        const StackFrame frame = action->data().value<StackFrame>();
        QTC_ASSERT(!frame.function.isEmpty(), return);
        currentEngine()->openDisassemblerView(Location(frame));
    }

    void handleAddToWatchWindow()
    {
        // Requires a selection, but that's the only case we want anyway.
        EditorManager *editorManager = EditorManager::instance();
        if (!editorManager)
            return;
        IEditor *editor = editorManager->currentEditor();
        if (!editor)
            return;
        ITextEditor *textEditor = qobject_cast<ITextEditor*>(editor);
        if (!textEditor)
            return;
        QTextCursor tc;
        QPlainTextEdit *ptEdit = qobject_cast<QPlainTextEdit*>(editor->widget());
        if (ptEdit)
            tc = ptEdit->textCursor();
        QString exp;
        if (tc.hasSelection()) {
            exp = tc.selectedText();
        } else {
            int line, column;
            exp = cppExpressionAt(textEditor, tc.position(), &line, &column);
        }
        if (exp.isEmpty())
            return;
        currentEngine()->watchHandler()->watchExpression(exp);
    }

    void handleExecExit()
    {
        currentEngine()->exitDebugger();
    }

    void handleFrameDown()
    {
        currentEngine()->frameDown();
    }

    void handleFrameUp()
    {
        currentEngine()->frameUp();
    }

    void handleOperateByInstructionTriggered(bool operateByInstructionTriggered)
    {
        // Go to source only if we have the file.
        if (currentEngine()->stackHandler()->currentIndex() >= 0) {
            const StackFrame frame = currentEngine()->stackHandler()->currentFrame();
            if (operateByInstructionTriggered || frame.isUsable()) {
                currentEngine()->gotoLocation(Location(frame, true));
            }
        }
    }

    bool isActiveDebugLanguage(int lang) const
    {
        return m_mainWindow->activeDebugLanguages() & lang;
    }

    QVariant sessionValue(const QString &name);
    void setSessionValue(const QString &name, const QVariant &value);
    QIcon locationMarkIcon() const { return m_locationMarkIcon; }

    void openTextEditor(const QString &titlePattern0, const QString &contents);
    void clearCppCodeModelSnapshot();
    void showMessage(const QString &msg, int channel, int timeout = -1);

    Utils::SavedAction *action(int code) const;
    bool boolSetting(int code) const;
    QString stringSetting(int code) const;

    void showModuleSymbols(const QString &moduleName, const Symbols &symbols);

    bool parseArgument(QStringList::const_iterator &it,
        const QStringList::const_iterator &cend,
        unsigned *enabledEngines, QString *errorMessage);
    bool parseArguments(const QStringList &args,
        unsigned *enabledEngines, QString *errorMessage);

    DebuggerToolTipManager *toolTipManager() const { return m_toolTipManager; }
    virtual QSharedPointer<GlobalDebuggerOptions> globalDebuggerOptions() const { return m_globalDebuggerOptions; }

    // FIXME: Remove.
    void maybeEnrichParameters(DebuggerStartParameters *sp);

    void gdbServerStarted(const QString &channel, const QString &sysroot, const QString &localExecutable);
    void attachedToProcess(const QString &channel, const QString &sysroot, const QString &localExecutable);

public:
    DebuggerMainWindow *m_mainWindow;
    DebuggerRunControlFactory *m_debuggerRunControlFactory;

    QString m_previousMode;
    QList<DebuggerStartParameters> m_scheduledStarts;

    Utils::ProxyAction *m_visibleStartAction;
    Utils::ProxyAction *m_hiddenStopAction;
    QAction *m_startAction;
    QAction *m_debugWithoutDeployAction;
    QAction *m_startLocalProcessAction;
    QAction *m_startRemoteProcessAction;
    QAction *m_startRemoteServerAction;
    QAction *m_attachToRemoteProcessAction;
    QAction *m_attachToQmlPortAction;
    QAction *m_attachToRemoteServerAction;
    QAction *m_startRemoteCdbAction;
    QAction *m_startRemoteLldbAction;
    QAction *m_attachToLocalProcessAction;
    QAction *m_attachToCoreAction;
    QAction *m_detachAction;
    QAction *m_continueAction;
    QAction *m_exitAction; // On application output button if "Stop" is possible
    QAction *m_interruptAction; // On the fat debug button if "Pause" is possible
    QAction *m_undisturbableAction; // On the fat debug button if nothing can be done
    QAction *m_abortAction;
    QAction *m_stepAction;
    QAction *m_stepOutAction;
    QAction *m_runToLineAction; // In the debug menu
    QAction *m_runToSelectedFunctionAction;
    QAction *m_jumpToLineAction; // In the Debug menu.
    QAction *m_returnFromFunctionAction;
    QAction *m_nextAction;
    QAction *m_watchAction1; // In the Debug menu.
    QAction *m_watchAction2; // In the text editor context menu.
    QAction *m_breakAction;
    QAction *m_reverseDirectionAction;
    QAction *m_frameUpAction;
    QAction *m_frameDownAction;

    QToolButton *m_reverseToolButton;

    QIcon m_startIcon;
    QIcon m_exitIcon;
    QIcon m_continueIcon;
    QIcon m_interruptIcon;
    QIcon m_locationMarkIcon;

    Utils::StatusLabel *m_statusLabel;
    QComboBox *m_threadBox;

    BaseWindow *m_breakWindow;
    BreakHandler *m_breakHandler;
    QtMessageLogWindow *m_qtMessageLogWindow;
    WatchWindow *m_returnWindow;
    WatchWindow *m_localsWindow;
    WatchWindow *m_watchersWindow;
    BaseWindow *m_registerWindow;
    BaseWindow *m_modulesWindow;
    BaseWindow *m_snapshotWindow;
    BaseWindow *m_sourceFilesWindow;
    BaseWindow *m_stackWindow;
    BaseWindow *m_threadsWindow;
    LogWindow *m_logWindow;

    bool m_busy;
    QString m_lastPermanentStatusMessage;

    mutable CPlusPlus::Snapshot m_codeModelSnapshot;
    DebuggerPlugin *m_plugin;

    SnapshotHandler *m_snapshotHandler;
    bool m_shuttingDown;
    DebuggerEngine *m_currentEngine;
    DebuggerSettings *m_debuggerSettings;
    QSettings *m_coreSettings;
    bool m_gdbBinariesChanged;
    uint m_cmdLineEnabledEngines;
    QStringList m_arguments;
    DebuggerToolTipManager *m_toolTipManager;
    CommonOptionsPage *m_commonOptionsPage;
    DummyEngine *m_dummyEngine;
    const QSharedPointer<GlobalDebuggerOptions> m_globalDebuggerOptions;
};

DebuggerPluginPrivate::DebuggerPluginPrivate(DebuggerPlugin *plugin) :
    m_toolTipManager(new DebuggerToolTipManager(this)),
    m_dummyEngine(0),
    m_globalDebuggerOptions(new GlobalDebuggerOptions)
{
    setObjectName(QLatin1String("DebuggerCore"));
    qRegisterMetaType<WatchData>("WatchData");
    qRegisterMetaType<ContextData>("ContextData");
    qRegisterMetaType<DebuggerStartParameters>("DebuggerStartParameters");

    QTC_CHECK(!theDebuggerCore);
    theDebuggerCore = this;

    m_plugin = plugin;

    m_startRemoteCdbAction = 0;
    m_shuttingDown = false;
    m_statusLabel = 0;
    m_threadBox = 0;

    m_breakWindow = 0;
    m_breakHandler = 0;
    m_returnWindow = 0;
    m_localsWindow = 0;
    m_watchersWindow = 0;
    m_registerWindow = 0;
    m_modulesWindow = 0;
    m_snapshotWindow = 0;
    m_sourceFilesWindow = 0;
    m_stackWindow = 0;
    m_threadsWindow = 0;
    m_logWindow = 0;
    m_qtMessageLogWindow = 0;

    m_mainWindow = 0;
    m_snapshotHandler = 0;
    m_currentEngine = 0;
    m_debuggerSettings = 0;

    m_gdbBinariesChanged = true;
    m_cmdLineEnabledEngines = AllEngineTypes;

    m_reverseToolButton = 0;
    m_startAction = 0;
    m_debugWithoutDeployAction = 0;
    m_startLocalProcessAction = 0;
    m_startRemoteProcessAction = 0;
    m_attachToRemoteServerAction = 0;
    m_attachToRemoteProcessAction = 0;
    m_attachToQmlPortAction = 0;
    m_startRemoteCdbAction = 0;
    m_startRemoteLldbAction = 0;
    m_attachToLocalProcessAction = 0;
    m_attachToCoreAction = 0;
    m_detachAction = 0;

    m_commonOptionsPage = 0;
}

DebuggerPluginPrivate::~DebuggerPluginPrivate()
{
    delete m_debuggerSettings;
    m_debuggerSettings = 0;

    // Mainwindow will be deleted by debug mode.

    delete m_snapshotHandler;
    m_snapshotHandler = 0;

    delete m_breakHandler;
    m_breakHandler = 0;
}

DebuggerEngine *DebuggerPluginPrivate::dummyEngine()
{
    if (!m_dummyEngine) {
        m_dummyEngine = new DummyEngine;
        m_dummyEngine->setParent(this);
        m_dummyEngine->setObjectName(_("DummyEngine"));
    }
    return m_dummyEngine;
}

DebuggerCore *debuggerCore()
{
    return theDebuggerCore;
}

static QString msgParameterMissing(const QString &a)
{
    return DebuggerPlugin::tr("Option '%1' is missing the parameter.").arg(a);
}

void DebuggerPluginPrivate::maybeEnrichParameters(DebuggerStartParameters *sp)
{
    if (!boolSetting(AutoEnrichParameters))
        return;
    if (sp->sysroot.isEmpty() &&
              (sp->startMode == AttachToRemoteServer
            || sp->startMode == StartRemoteProcess
            || sp->startMode == AttachToRemoteProcess)) {
        // FIXME: Get from BaseQtVersion.
        sp->sysroot = QString::fromLocal8Bit(qgetenv("QTC_DEBUGGER_SYSROOT"));
        showMessage(QString::fromLatin1("USING QTC_DEBUGGER_SYSROOT %1")
            .arg(sp->sysroot), LogWarning);
    }
    if (sp->debugInfoLocation.isEmpty()) {
        sp->debugInfoLocation = sp->sysroot + QLatin1String("/usr/lib/debug");
    }
    if (sp->debugSourceLocation.isEmpty()) {
        QString base = sp->sysroot + QLatin1String("/usr/src/debug/");
        sp->debugSourceLocation.append(base + QLatin1String("qt5base/src/corelib"));
        sp->debugSourceLocation.append(base + QLatin1String("qt5base/src/gui"));
        sp->debugSourceLocation.append(base + QLatin1String("qt5base/src/network"));
        sp->debugSourceLocation.append(base + QLatin1String("qt5base/src/v8"));
        sp->debugSourceLocation.append(base + QLatin1String("qt5declarative/src/qml"));
    }
}

bool DebuggerPluginPrivate::parseArgument(QStringList::const_iterator &it,
    const QStringList::const_iterator &cend,
    unsigned *enabledEngines, QString *errorMessage)
{
    const QString &option = *it;
    // '-debug <pid>'
    // '-debug <exe>[,server=<server:port>|,core=<core>][,arch=<arch>][,sysroot=<sysroot>]'
    if (*it == _("-debug")) {
        ++it;
        if (it == cend) {
            *errorMessage = msgParameterMissing(*it);
            return false;
        }
        DebuggerStartParameters sp;
        qulonglong pid = it->toULongLong();
        if (pid) {
            sp.startMode = AttachExternal;
            sp.closeMode = DetachAtClose;
            sp.attachPID = pid;
            sp.displayName = tr("Process %1").arg(sp.attachPID);
            sp.startMessage = tr("Attaching to local process %1.").arg(sp.attachPID);
            sp.toolChainAbi = Abi::hostAbi();
        } else {
            QStringList args = it->split(QLatin1Char(','));
            sp.startMode = StartExternal;
            foreach (const QString &arg, args) {
                QString key = arg.section(QLatin1Char('='), 0, 0);
                QString val = arg.section(QLatin1Char('='), 1, 1);
                if (val.isEmpty()) {
                    if (key.isEmpty())
                        continue;
                    else if (sp.executable.isEmpty())
                        sp.executable = key;
                    else {
                        *errorMessage = DebuggerPlugin::tr("Only one executable allowed!");
                        return false;
                    }
                }
                if (key == QLatin1String("server")) {
                    sp.startMode = AttachToRemoteServer;
                    sp.remoteChannel = val;
                    sp.displayName = tr("Remote: \"%1\"").arg(sp.remoteChannel);
                    sp.startMessage = tr("Attaching to remote server %1.").arg(sp.remoteChannel);
                }
                else if (key == QLatin1String("arch"))
                    sp.remoteArchitecture = val;
                else if (key == QLatin1String("core")) {
                    sp.startMode = AttachCore;
                    sp.coreFile = val;
                    sp.displayName = tr("Core file \"%1\"").arg(sp.coreFile);
                    sp.startMessage = tr("Attaching to core file %1.").arg(sp.coreFile);
                }
                else if (key == QLatin1String("sysroot"))
                    sp.sysroot = val;
            }
            sp.toolChainAbi = anyAbiOfBinary(sp.executable);
        }
        if (sp.startMode == StartExternal) {
            sp.displayName = tr("Executable file \"%1\"").arg(sp.executable);
            sp.startMessage = tr("Debugging file %1.").arg(sp.executable);
        }
        m_scheduledStarts.append(sp);
        return true;
    }
    // -wincrashevent <event-handle>:<pid>. A handle used for
    // a handshake when attaching to a crashed Windows process.
    // This is created by $QTC/src/tools/qtcdebugger/main.cpp:
    // args << QLatin1String("-wincrashevent")
    //   << QString("%1:%2").arg(argWinCrashEvent).arg(argProcessId);
    if (*it == _("-wincrashevent")) {
        ++it;
        if (it == cend) {
            *errorMessage = msgParameterMissing(*it);
            return false;
        }
        DebuggerStartParameters sp;
        sp.startMode = AttachCrashedExternal;
        sp.crashParameter = it->section(QLatin1Char(':'), 0, 0);
        sp.attachPID = it->section(QLatin1Char(':'), 1, 1).toULongLong();
        sp.displayName = tr("Crashed process %1").arg(sp.attachPID);
        sp.startMessage = tr("Attaching to crashed process %1").arg(sp.attachPID);
        sp.toolChainAbi = Abi::hostAbi();
        if (!sp.attachPID) {
            *errorMessage = DebuggerPlugin::tr("The parameter '%1' of option '%2' "
                "does not match the pattern <handle>:<pid>.").arg(*it, option);
            return false;
        }
        m_scheduledStarts.append(sp);
        return true;
    }
    // Engine disabling.
    if (option == _("-disable-cdb")) {
        *enabledEngines &= ~CdbEngineType;
        return true;
    }
    if (option == _("-disable-gdb")) {
        *enabledEngines &= ~GdbEngineType;
        return true;
    }
    if (option == _("-disable-qmldb")) {
        *enabledEngines &= ~QmlEngineType;
        return true;
    }
    if (option == _("-disable-sdb")) {
        *enabledEngines &= ~ScriptEngineType;
        return true;
    }
    if (option == _("-disable-lldb")) {
        *enabledEngines &= ~LldbEngineType;
        return true;
    }

    *errorMessage = DebuggerPlugin::tr("Invalid debugger option: %1").arg(option);
    return false;
}

bool DebuggerPluginPrivate::parseArguments(const QStringList &args,
   unsigned *enabledEngines, QString *errorMessage)
{
    const QStringList::const_iterator cend = args.constEnd();
    for (QStringList::const_iterator it = args.constBegin(); it != cend; ++it)
        if (!parseArgument(it, cend, enabledEngines, errorMessage))
            return false;
    if (Constants::Internal::debug)
        qDebug().nospace() << args << "engines=0x"
            << QString::number(*enabledEngines, 16) << '\n';
    return true;
}

bool DebuggerPluginPrivate::initialize(const QStringList &arguments,
    QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    m_arguments = arguments;
    // Cpp/Qml ui setup
    m_mainWindow = new DebuggerMainWindow;

    return true;
}

void DebuggerPluginPrivate::setConfigValue(const QString &name, const QVariant &value)
{
    m_coreSettings->setValue(_("DebugMode/") + name, value);
}

QVariant DebuggerPluginPrivate::configValue(const QString &name) const
{
    const QVariant value = m_coreSettings->value(_("DebugMode/") + name);
    if (value.isValid())
        return value;
    // Legacy (pre-2.1): Check old un-namespaced-settings.
    return m_coreSettings->value(name);
}

void DebuggerPluginPrivate::onCurrentProjectChanged(Project *project)
{
    RunConfiguration *activeRc = 0;
    if (project) {
        Target *target = project->activeTarget();
        if (target)
            activeRc = target->activeRunConfiguration();
        if (!activeRc)
            return;
    }
    for (int i = 0, n = m_snapshotHandler->size(); i != n; ++i) {
        // Run controls might be deleted during exit.
        if (DebuggerEngine *engine = m_snapshotHandler->at(i)) {
            DebuggerRunControl *runControl = engine->runControl();
            RunConfiguration *rc = runControl->runConfiguration();
            if (rc == activeRc) {
                m_snapshotHandler->setCurrentIndex(i);
                updateState(engine);
                return;
            }
        }
    }
    // No corresponding debugger found. So we are ready to start one.
    m_interruptAction->setEnabled(false);
    m_continueAction->setEnabled(false);
    m_exitAction->setEnabled(false);
    m_startAction->setEnabled(true);
    m_debugWithoutDeployAction->setEnabled(true);
    m_visibleStartAction->setAction(m_startAction);
}

void DebuggerPluginPrivate::languagesChanged()
{
}

void DebuggerPluginPrivate::debugProject()
{
    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    if (Project *pro = pe->startupProject())
        pe->runProject(pro, DebugRunMode);
}

void DebuggerPluginPrivate::debugProjectWithoutDeploy()
{
    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    if (Project *pro = pe->startupProject())
        pe->runProject(pro, DebugRunMode, true);
}

void DebuggerPluginPrivate::debugProjectBreakMain()
{
    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    if (Project *pro = pe->startupProject())
        pe->runProject(pro, DebugRunModeWithBreakOnMain);
}

void DebuggerPluginPrivate::startExternalApplication()
{
    DebuggerStartParameters sp;
    if (StartExternalDialog::run(mainWindow(), m_coreSettings, &sp))
        if (RunControl *rc = m_debuggerRunControlFactory->create(sp))
            startDebugger(rc);
}

void DebuggerPluginPrivate::attachExternalApplication()
{
    AttachExternalDialog dlg(mainWindow());
    dlg.setAbiIndex(configValue(_("LastAttachExternalAbiIndex")).toInt());

    if (dlg.exec() != QDialog::Accepted)
        return;

    if (dlg.attachPID() == 0) {
        QMessageBox::warning(mainWindow(), tr("Warning"),
            tr("Cannot attach to process with PID 0"));
        return;
    }

    setConfigValue(_("LastAttachExternalAbiIndex"), QVariant(dlg.abiIndex()));

    DebuggerStartParameters sp;
    sp.attachPID = dlg.attachPID();
    sp.displayName = tr("Process %1").arg(dlg.attachPID());
    sp.executable = dlg.executable();
    sp.startMode = AttachExternal;
    sp.closeMode = DetachAtClose;
    sp.toolChainAbi = dlg.abi();
    sp.debuggerCommand = dlg.debuggerCommand();
    if (DebuggerRunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::attachExternalApplication(RunControl *rc)
{
    DebuggerStartParameters sp;
    sp.attachPID = rc->applicationProcessHandle().pid();
    sp.displayName = tr("Debugger attached to %1").arg(rc->displayName());
    sp.startMode = AttachExternal;
    sp.closeMode = DetachAtClose;
    sp.toolChainAbi = rc->abi();
    if (DebuggerRunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::attachCore()
{
    AttachCoreDialog dlg(mainWindow());
    dlg.setExecutableFile(configValue(_("LastExternalExecutableFile")).toString());
    dlg.setCoreFile(configValue(_("LastExternalCoreFile")).toString());
    dlg.setAbiIndex(configValue(_("LastExternalAbiIndex")).toInt());
    dlg.setSysroot(configValue(_("LastSysroot")).toString());
    dlg.setOverrideStartScript(configValue(_("LastExternalStartScript")).toString());

    if (dlg.exec() != QDialog::Accepted)
        return;

    setConfigValue(_("LastExternalExecutableFile"), dlg.executableFile());
    setConfigValue(_("LastExternalCoreFile"), dlg.coreFile());
    setConfigValue(_("LastExternalAbiIndex"), QVariant(dlg.abiIndex()));
    setConfigValue(_("LastSysroot"), dlg.sysroot());
    setConfigValue(_("LastExternalStartScript"), dlg.overrideStartScript());

    DebuggerStartParameters sp;
    sp.executable = dlg.executableFile();
    sp.coreFile = dlg.coreFile();
    sp.displayName = tr("Core file \"%1\"").arg(dlg.coreFile());
    sp.startMode = AttachCore;
    sp.debuggerCommand = dlg.debuggerCommand();
    sp.toolChainAbi = dlg.abi();
    sp.sysroot = dlg.sysroot();
    sp.overrideStartScript = dlg.overrideStartScript();
    if (DebuggerRunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::attachToRemoteServer(const QString &spec)
{
    // spec is: server:port@executable@architecture
    DebuggerStartParameters sp;
    sp.remoteChannel = spec.section(QLatin1Char('@'), 0, 0);
    sp.executable = spec.section(QLatin1Char('@'), 1, 1);
    sp.remoteArchitecture = spec.section(QLatin1Char('@'), 2, 2);
    sp.displayName = tr("Remote: \"%1\"").arg(sp.remoteChannel);
    sp.startMode = AttachToRemoteServer;
    sp.closeMode = KillAtClose;
    sp.toolChainAbi = anyAbiOfBinary(sp.executable);
    if (DebuggerRunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::startRemoteCdbSession()
{
    const QString connectionKey = _("CdbRemoteConnection");
    DebuggerStartParameters sp;
    Abi hostAbi = Abi::hostAbi();
    sp.toolChainAbi = Abi(hostAbi.architecture(), Abi::WindowsOS,
        Abi::WindowsMsvc2010Flavor, Abi::PEFormat, hostAbi.wordWidth());
    sp.startMode = AttachToRemoteServer;
    sp.closeMode = KillAtClose;
    StartRemoteCdbDialog dlg(mainWindow());
    QString previousConnection = configValue(connectionKey).toString();
    if (previousConnection.isEmpty())
        previousConnection = QLatin1String("localhost:1234");
    dlg.setConnection(previousConnection);
    if (dlg.exec() != QDialog::Accepted)
        return;
    sp.remoteChannel = dlg.connection();
    setConfigValue(connectionKey, sp.remoteChannel);
    if (RunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

bool DebuggerPluginPrivate::queryRemoteParameters(DebuggerStartParameters &sp, bool useScript)
{
    return StartRemoteDialog::run(mainWindow(),
                                  m_coreSettings,
                                  useScript,
                                  &sp);
}

void DebuggerPluginPrivate::startRemoteProcess()
{
    DebuggerStartParameters sp;
    if (StartRemoteDialog::run(mainWindow(), m_coreSettings, true, &sp)) {
        sp.startMode = StartRemoteProcess;
        if (RunControl *rc = createDebugger(sp))
            startDebugger(rc);
    }
}

void DebuggerPluginPrivate::attachToRemoteServer()
{
    DebuggerStartParameters sp;
    if (StartRemoteDialog::run(mainWindow(), m_coreSettings, false, &sp)) {
        sp.startMode = AttachToRemoteServer;
        sp.closeMode = KillAtClose;
        sp.useServerStartScript = false;
        sp.serverStartScript.clear();
        if (RunControl *rc = createDebugger(sp))
            startDebugger(rc);
    }
}

void DebuggerPluginPrivate::startRemoteServer()
{
    PluginManager *pm = PluginManager::instance();
    QTC_ASSERT(pm, return);
    QObject *rl = pm->getObjectByName(_("RemoteLinuxPlugin"));
    QTC_ASSERT(rl, return);
    QMetaObject::invokeMethod(rl, "startGdbServer", Qt::QueuedConnection);
    // Will call back gdbServerStarted() below.
}

void DebuggerPluginPrivate::gdbServerStarted(const QString &channel,
    const QString &sysroot, const QString &remoteCommandLine)
{
    Q_UNUSED(remoteCommandLine);
    Q_UNUSED(sysroot);
    showStatusMessage(tr("gdbserver is now listening at %1").arg(channel));
}

void DebuggerPluginPrivate::attachToRemoteProcess()
{
    PluginManager *pm = PluginManager::instance();
    QTC_ASSERT(pm, return);
    QObject *rl = pm->getObjectByName(_("RemoteLinuxPlugin"));
    QTC_ASSERT(rl, return);
    QMetaObject::invokeMethod(rl, "attachToRemoteProcess", Qt::QueuedConnection);
    // This will call back attachedtToProcess() below.
}

void DebuggerPluginPrivate::attachedToProcess(const QString &channel,
    const QString &sysroot, const QString &remoteCommandLine)
{
    QString binary = remoteCommandLine.section(QLatin1Char(' '), 0, 0);
    QString localExecutable;
    QString candidate = sysroot + QLatin1Char('/') + binary;
    if (QFileInfo(candidate).exists())
        localExecutable = candidate;
    if (localExecutable.isEmpty()) {
        candidate = sysroot + QLatin1String("/usr/bin/") + binary;
        if (QFileInfo(candidate).exists())
            localExecutable = candidate;
    }
    if (localExecutable.isEmpty()) {
        candidate = sysroot + QLatin1String("/bin/") + binary;
        if (QFileInfo(candidate).exists())
            localExecutable = candidate;
    }
    if (localExecutable.isEmpty()) {
        QMessageBox::warning(mainWindow(), tr("Warning"),
            tr("Cannot find local executable for remote process \"%1\".")
                .arg(remoteCommandLine));
        return;
    }

    QList<Abi> abis = Abi::abisOfBinary(Utils::FileName::fromString(localExecutable));
    if (abis.isEmpty()) {
        QMessageBox::warning(mainWindow(), tr("Warning"),
            tr("Cannot find ABI for remote process \"%1\".")
                .arg(remoteCommandLine));
        return;
    }

    DebuggerStartParameters sp;
    sp.toolChainAbi = abis.at(0);
    //sp.remoteArchitecture = abis.at(0).toString();
    sp.displayName = tr("Remote: \"%1\"").arg(channel);
    sp.remoteChannel = channel;
    sp.sysroot = sysroot;
    sp.executable = localExecutable;
    sp.startMode = AttachToRemoteServer;
    sp.closeMode = KillAtClose;
    sp.overrideStartScript.clear();
    sp.useServerStartScript = false;
    sp.serverStartScript.clear();
    //sp.debugInfoLocation = dlg.debugInfoLocation();
    if (RunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::attachToQmlPort()
{
    DebuggerStartParameters sp;
    AttachToQmlPortDialog dlg(mainWindow());

    const QVariant qmlServerAddress = configValue(_("LastQmlServerAddress"));
    if (qmlServerAddress.isValid()) {
        dlg.setHost(qmlServerAddress.toString());
    } else {
        dlg.setHost(sp.qmlServerAddress);
    }

    const QVariant qmlServerPort = configValue(_("LastQmlServerPort"));
    if (qmlServerPort.isValid()) {
        dlg.setPort(qmlServerPort.toInt());
    } else {
        dlg.setPort(sp.qmlServerPort);
    }

    const QVariant sysrootPath = configValue(_("LastSysroot"));
    if (sysrootPath.isValid())
        dlg.setSysroot(sysrootPath.toString());

    if (dlg.exec() != QDialog::Accepted)
        return;

    setConfigValue(_("LastQmlServerAddress"), dlg.host());
    setConfigValue(_("LastQmlServerPort"), dlg.port());
    setConfigValue(_("LastSysroot"), dlg.sysroot());

    sp.qmlServerAddress = dlg.host();
    sp.qmlServerPort = dlg.port();
    sp.sysroot = dlg.sysroot();

    sp.startMode = AttachToRemoteServer;
    sp.closeMode = KillAtClose;
    sp.languages = QmlLanguage;

    //
    // get files from all the projects in the session
    //
    SessionManager *sessionManager = ProjectExplorerPlugin::instance()->session();
    QList<Project *> projects = sessionManager->projects();
    if (Project *startupProject = ProjectExplorerPlugin::instance()->startupProject()) {
        // startup project first
        projects.removeOne(ProjectExplorerPlugin::instance()->startupProject());
        projects.insert(0, startupProject);
    }
    QStringList sourceFiles;
    foreach (Project *project, projects)
        sourceFiles << project->files(Project::ExcludeGeneratedFiles);

    sp.projectSourceFiles = sourceFiles;

    if (RunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::startRemoteEngine()
{
    DebuggerStartParameters sp;
    StartRemoteEngineDialog dlg(mainWindow());
    if (dlg.exec() != QDialog::Accepted)
        return;

    sp.connParams.host = dlg.host();
    sp.connParams.userName = dlg.username();
    sp.connParams.password = dlg.password();

    sp.connParams.timeout = 5;
    sp.connParams.authenticationType = Utils::SshConnectionParameters::AuthenticationByPassword;
    sp.connParams.port = 22;
    sp.connParams.proxyType = Utils::SshConnectionParameters::NoProxy;

    sp.executable = dlg.inferiorPath();
    sp.serverStartScript = dlg.enginePath();
    sp.startMode = StartRemoteEngine;
    if (RunControl *rc = createDebugger(sp))
        startDebugger(rc);
}

void DebuggerPluginPrivate::enableReverseDebuggingTriggered(const QVariant &value)
{
    QTC_ASSERT(m_reverseToolButton, return);
    m_reverseToolButton->setVisible(value.toBool());
    m_reverseDirectionAction->setChecked(false);
    m_reverseDirectionAction->setEnabled(value.toBool());
}

void DebuggerPluginPrivate::runScheduled()
{
    foreach (const DebuggerStartParameters &sp, m_scheduledStarts) {
        RunControl *rc = createDebugger(sp);
        QTC_ASSERT(rc, qDebug() << "CANNOT CREATE RUN CONTROL"; continue);
        showStatusMessage(sp.startMessage);
        startDebugger(rc);
    }
}

void DebuggerPluginPrivate::editorOpened(IEditor *editor)
{
    if (!isEditorDebuggable(editor))
        return;
    ITextEditor *textEditor = qobject_cast<ITextEditor *>(editor);
    if (!textEditor)
        return;
    connect(textEditor,
        SIGNAL(markRequested(TextEditor::ITextEditor*,int,TextEditor::ITextEditor::MarkRequestKind)),
        SLOT(requestMark(TextEditor::ITextEditor*,int,TextEditor::ITextEditor::MarkRequestKind)));
    connect(textEditor,
        SIGNAL(markContextMenuRequested(TextEditor::ITextEditor*,int,QMenu*)),
        SLOT(requestContextMenu(TextEditor::ITextEditor*,int,QMenu*)));
}

void DebuggerPluginPrivate::updateBreakMenuItem(Core::IEditor *editor)
{
    ITextEditor *textEditor = qobject_cast<ITextEditor *>(editor);
    m_breakAction->setEnabled(textEditor != 0);
}

void DebuggerPluginPrivate::requestContextMenu(ITextEditor *editor,
    int lineNumber, QMenu *menu)
{
    if (!isEditorDebuggable(editor))
        return;

    BreakpointMenuContextData args;
    args.lineNumber = lineNumber;
    bool contextUsable = true;

    BreakpointModelId id = BreakpointModelId();
    const QString fileName = editor->document()->fileName();
    if (editor->property("DisassemblerView").toBool()) {
        args.fileName = fileName;
        QString line = editor->contents()
            .section(QLatin1Char('\n'), lineNumber - 1, lineNumber - 1);
        BreakpointResponse needle;
        needle.type = BreakpointByAddress;
        needle.address = DisassemblerLine::addressFromDisassemblyLine(line);
        args.address = needle.address;
        needle.lineNumber = -1;
        id = breakHandler()->findSimilarBreakpoint(needle);
        contextUsable = args.address != 0;
    } else {
        args.fileName = editor->document()->fileName();
        id = breakHandler()
            ->findBreakpointByFileAndLine(args.fileName, lineNumber);
        if (!id)
            id = breakHandler()->findBreakpointByFileAndLine(args.fileName, lineNumber, false);
    }

    if (id) {
        // Remove existing breakpoint.
        QAction *act = new QAction(menu);
        act->setData(QVariant::fromValue(id));
        act->setText(tr("Remove Breakpoint %1").arg(id.toString()));
        connect(act, SIGNAL(triggered()),
            SLOT(breakpointRemoveMarginActionTriggered()));
        menu->addAction(act);

        // Enable/disable existing breakpoint.
        act = new QAction(menu);
        act->setData(QVariant::fromValue(id));
        if (breakHandler()->isEnabled(id)) {
            act->setText(tr("Disable Breakpoint %1").arg(id.toString()));
            connect(act, SIGNAL(triggered()),
                SLOT(breakpointDisableMarginActionTriggered()));
        } else {
            act->setText(tr("Enable Breakpoint %1").arg(id.toString()));
            connect(act, SIGNAL(triggered()),
                SLOT(breakpointEnableMarginActionTriggered()));
        }
        menu->addAction(act);

        // Edit existing breakpoint.
        act = new QAction(menu);
        act->setText(tr("Edit Breakpoint %1...").arg(id.toString()));
        connect(act, SIGNAL(triggered()), SLOT(slotEditBreakpoint()));
        act->setData(QVariant::fromValue(id));
        menu->addAction(act);
    } else {
        // Handle non-existing breakpoint.
        const QString text = args.address
            ? tr("Set Breakpoint at 0x%1").arg(args.address, 0, 16)
            : tr("Set Breakpoint at Line %1").arg(lineNumber);
        QAction *act = new QAction(text, menu);
        act->setData(QVariant::fromValue(args));
        act->setEnabled(contextUsable);
        connect(act, SIGNAL(triggered()),
            SLOT(breakpointSetMarginActionTriggered()));
        menu->addAction(act);
        // Message trace point
        args.mode = BreakpointMenuContextData::MessageTracePoint;
        const QString tracePointText = args.address
            ? tr("Set Message Tracepoint at 0x%1...").arg(args.address, 0, 16)
            : tr("Set Message Tracepoint at Line %1...").arg(lineNumber);
        act = new QAction(tracePointText, menu);
        act->setData(QVariant::fromValue(args));
        act->setEnabled(contextUsable);
        connect(act, SIGNAL(triggered()),
            SLOT(breakpointSetMarginActionTriggered()));
        menu->addAction(act);
    }
    // Run to, jump to line below in stopped state.
    if (currentEngine()->state() == InferiorStopOk && contextUsable) {
        menu->addSeparator();
        if (currentEngine()->hasCapability(RunToLineCapability)) {
            const QString runText = args.address
                ? DebuggerEngine::tr("Run to Address 0x%1").arg(args.address, 0, 16)
                : DebuggerEngine::tr("Run to Line %1").arg(args.lineNumber);
            QAction *runToLineAction  = new QAction(runText, menu);
            runToLineAction->setData(QVariant::fromValue(args));
            connect(runToLineAction, SIGNAL(triggered()), SLOT(slotRunToLine()));
            menu->addAction(runToLineAction);
        }
        if (currentEngine()->hasCapability(JumpToLineCapability)) {
            const QString jumpText = args.address
                ? DebuggerEngine::tr("Jump to Address 0x%1").arg(args.address, 0, 16)
                : DebuggerEngine::tr("Jump to Line %1").arg(args.lineNumber);
            QAction *jumpToLineAction  = new QAction(jumpText, menu);
            jumpToLineAction->setData(QVariant::fromValue(args));
            connect(jumpToLineAction, SIGNAL(triggered()), SLOT(slotJumpToLine()));
            menu->addAction(jumpToLineAction);
        }
        // Disassemble current function in stopped state.
        if (currentEngine()->state() == InferiorStopOk
            && currentEngine()->hasCapability(DisassemblerCapability)) {
            StackFrame frame;
            frame.function = cppFunctionAt(fileName, lineNumber);
            frame.line = 42; // trick gdb into mixed mode.
            if (!frame.function.isEmpty()) {
                const QString text = tr("Disassemble Function \"%1\"")
                    .arg(frame.function);
                QAction *disassembleAction = new QAction(text, menu);
                disassembleAction->setData(QVariant::fromValue(frame));
                connect(disassembleAction, SIGNAL(triggered()), SLOT(slotDisassembleFunction()));
                menu->addAction(disassembleAction );
            }
        }
    }
}

void DebuggerPluginPrivate::toggleBreakpoint()
{
    ITextEditor *textEditor = currentTextEditor();
    QTC_ASSERT(textEditor, return);
    const int lineNumber = textEditor->currentLine();
    if (textEditor->property("DisassemblerView").toBool()) {
        QString line = textEditor->contents()
            .section(QLatin1Char('\n'), lineNumber - 1, lineNumber - 1);
        quint64 address = DisassemblerLine::addressFromDisassemblyLine(line);
        toggleBreakpointByAddress(address);
    } else if (lineNumber >= 0) {
        toggleBreakpointByFileAndLine(textEditor->document()->fileName(), lineNumber);
    }
}

void DebuggerPluginPrivate::toggleBreakpointByFileAndLine(const QString &fileName,
    int lineNumber, const QString &tracePointMessage)
{
    BreakHandler *handler = m_breakHandler;
    BreakpointModelId id =
        handler->findBreakpointByFileAndLine(fileName, lineNumber, true);
    if (!id)
        id = handler->findBreakpointByFileAndLine(fileName, lineNumber, false);

    if (id) {
        handler->removeBreakpoint(id);
    } else {
        BreakpointParameters data(BreakpointByFileAndLine);
        data.tracepoint = !tracePointMessage.isEmpty();
        data.message = tracePointMessage;
        data.fileName = fileName;
        data.lineNumber = lineNumber;
        handler->appendBreakpoint(data);
    }
}

void DebuggerPluginPrivate::toggleBreakpointByAddress(quint64 address,
                                                      const QString &tracePointMessage)
{
    BreakHandler *handler = m_breakHandler;
    BreakpointModelId id = handler->findBreakpointByAddress(address);

    if (id) {
        handler->removeBreakpoint(id);
    } else {
        BreakpointParameters data(BreakpointByAddress);
        data.tracepoint = !tracePointMessage.isEmpty();
        data.message = tracePointMessage;
        data.address = address;
        handler->appendBreakpoint(data);
    }
}

void DebuggerPluginPrivate::requestMark(ITextEditor *editor,
                                        int lineNumber,
                                        ITextEditor::MarkRequestKind kind)
{
    if (kind != ITextEditor::BreakpointRequest)
        return;

    if (editor->property("DisassemblerView").toBool()) {
        QString line = editor->contents()
            .section(QLatin1Char('\n'), lineNumber - 1, lineNumber - 1);
        quint64 address = DisassemblerLine::addressFromDisassemblyLine(line);
        toggleBreakpointByAddress(address);
    } else if (editor->document()) {
        toggleBreakpointByFileAndLine(editor->document()->fileName(), lineNumber);
    }
}

DebuggerRunControl *DebuggerPluginPrivate::createDebugger
    (const DebuggerStartParameters &sp0, RunConfiguration *rc)
{
    DebuggerStartParameters sp = sp0;
    maybeEnrichParameters(&sp);
    return m_debuggerRunControlFactory->create(sp, rc);
}

// If updateEngine is set, the engine will update its threads/modules and so forth.
void DebuggerPluginPrivate::displayDebugger(DebuggerEngine *engine, bool updateEngine)
{
    QTC_ASSERT(engine, return);
    disconnectEngine();
    connectEngine(engine);
    if (updateEngine)
        engine->updateAll();
    engine->updateViews();
}

void DebuggerPluginPrivate::startDebugger(RunControl *rc)
{
    QTC_ASSERT(rc, return);
    ProjectExplorerPlugin::instance()->startRunControl(rc, DebugRunMode);
}


void DebuggerPluginPrivate::connectEngine(DebuggerEngine *engine)
{
    if (!engine)
        engine = dummyEngine();

    if (m_currentEngine == engine)
        return;

    if (m_currentEngine)
        m_currentEngine->resetLocation();
    m_currentEngine = engine;

    m_localsWindow->setModel(engine->localsModel());
    m_modulesWindow->setModel(engine->modulesModel());
    m_registerWindow->setModel(engine->registerModel());
    m_returnWindow->setModel(engine->returnModel());
    m_sourceFilesWindow->setModel(engine->sourceFilesModel());
    m_stackWindow->setModel(engine->stackModel());
    m_threadsWindow->setModel(engine->threadsModel());
    //m_threadBox->setModel(engine->threadsModel());
    //m_threadBox->setModelColumn(ThreadData::ComboNameColumn);
    m_watchersWindow->setModel(engine->watchersModel());
    m_qtMessageLogWindow->setModel(engine->qtMessageLogModel());

    engine->watchHandler()->rebuildModel();

    mainWindow()->setEngineDebugLanguages(engine->languages());
    mainWindow()->setCurrentEngine(engine);
}

static void changeFontSize(QWidget *widget, qreal size)
{
    QFont font = widget->font();
    font.setPointSizeF(size);
    widget->setFont(font);
}

void DebuggerPluginPrivate::fontSettingsChanged
    (const TextEditor::FontSettings &settings)
{
    if (!boolSetting(FontSizeFollowsEditor))
        return;
    qreal size = settings.fontZoom() * settings.fontSize() / 100.;
    changeFontSize(m_breakWindow, size);
    changeFontSize(m_logWindow, size);
    changeFontSize(m_localsWindow, size);
    changeFontSize(m_modulesWindow, size);
    //changeFontSize(m_consoleWindow, size);
    changeFontSize(m_registerWindow, size);
    changeFontSize(m_returnWindow, size);
    changeFontSize(m_sourceFilesWindow, size);
    changeFontSize(m_stackWindow, size);
    changeFontSize(m_threadsWindow, size);
    changeFontSize(m_watchersWindow, size);
}

void DebuggerPluginPrivate::cleanupViews()
{
    m_reverseDirectionAction->setChecked(false);
    m_reverseDirectionAction->setEnabled(false);

    if (!boolSetting(CloseBuffersOnExit))
        return;

    EditorManager *editorManager = EditorManager::instance();
    QTC_ASSERT(editorManager, return);
    QList<IEditor *> toClose;
    foreach (IEditor *editor, editorManager->openedEditors()) {
        if (editor->property(Constants::OPENED_BY_DEBUGGER).toBool()) {
            // Close disassembly views. Close other opened files
            // if they are not modified and not current editor.
            if (editor->property(Constants::OPENED_WITH_DISASSEMBLY).toBool()
                    || (!editor->document()->isModified()
                        && editor != editorManager->currentEditor())) {
                toClose.append(editor);
            } else {
                editor->setProperty(Constants::OPENED_BY_DEBUGGER, false);
            }
        }
    }
    editorManager->closeEditors(toClose);
}

void DebuggerPluginPrivate::setBusyCursor(bool busy)
{
    //STATE_DEBUG("BUSY FROM: " << m_busy << " TO: " << busy);
    if (busy == m_busy)
        return;
    m_busy = busy;
    QCursor cursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
    m_breakWindow->setCursor(cursor);
    //m_consoleWindow->setCursor(cursor);
    m_localsWindow->setCursor(cursor);
    m_modulesWindow->setCursor(cursor);
    m_logWindow->setCursor(cursor);
    m_registerWindow->setCursor(cursor);
    m_returnWindow->setCursor(cursor);
    m_sourceFilesWindow->setCursor(cursor);
    m_stackWindow->setCursor(cursor);
    m_threadsWindow->setCursor(cursor);
    m_watchersWindow->setCursor(cursor);
    m_snapshotWindow->setCursor(cursor);
}

void DebuggerPluginPrivate::setInitialState()
{
    m_watchersWindow->setVisible(false);
    m_returnWindow->setVisible(false);
    setBusyCursor(false);
    m_reverseDirectionAction->setChecked(false);
    m_reverseDirectionAction->setEnabled(false);
    m_toolTipManager->closeAllToolTips();

    m_startLocalProcessAction->setEnabled(true);
    m_attachToLocalProcessAction->setEnabled(true);
    m_attachToQmlPortAction->setEnabled(true);
    m_attachToCoreAction->setEnabled(true);
    m_startRemoteProcessAction->setEnabled(true);
    m_attachToRemoteServerAction->setEnabled(true);
    m_attachToRemoteProcessAction->setEnabled(true);
    m_detachAction->setEnabled(false);

    m_watchAction1->setEnabled(true);
    m_watchAction2->setEnabled(true);
    m_breakAction->setEnabled(false);
    //m_snapshotAction->setEnabled(false);
    action(OperateByInstruction)->setEnabled(false);

    m_exitAction->setEnabled(false);
    m_abortAction->setEnabled(false);

    m_interruptAction->setEnabled(false);
    m_continueAction->setEnabled(false);

    m_stepAction->setEnabled(true);
    m_stepOutAction->setEnabled(false);
    m_runToLineAction->setEnabled(false);
    m_runToSelectedFunctionAction->setEnabled(true);
    m_returnFromFunctionAction->setEnabled(false);
    m_jumpToLineAction->setEnabled(false);
    m_nextAction->setEnabled(true);

    action(AutoDerefPointers)->setEnabled(true);
    action(ExpandStack)->setEnabled(false);

    m_qtMessageLogWindow->setEnabled(true);
}

void DebuggerPluginPrivate::updateWatchersWindow()
{
    m_watchersWindow->setVisible(
        m_watchersWindow->model()->rowCount(QModelIndex()) > 0);
    m_returnWindow->setVisible(
        m_returnWindow->model()->rowCount(QModelIndex()) > 0);
}

void DebuggerPluginPrivate::updateState(DebuggerEngine *engine)
{
    QTC_ASSERT(engine, return);
    QTC_ASSERT(m_watchersWindow->model(), return);
    QTC_ASSERT(m_returnWindow->model(), return);
    QTC_ASSERT(!engine->isSlaveEngine(), return);

    m_threadBox->setCurrentIndex(engine->threadsHandler()->currentThread());

    updateWatchersWindow();

    const DebuggerState state = engine->state();
    //showMessage(QString("PLUGIN SET STATE: ")
    //    + DebuggerEngine::stateName(state), LogStatus);
    //qDebug() << "PLUGIN SET STATE: " << state;

    static DebuggerState previousState = DebuggerNotReady;
    if (state == previousState)
        return;

    bool actionsEnabled = DebuggerEngine::debuggerActionsEnabled(state);

    if (state == DebuggerNotReady) {
        QTC_ASSERT(false, /* We use the Core's m_debugAction here */);
        // F5 starts debugging. It is "startable".
        m_interruptAction->setEnabled(false);
        m_continueAction->setEnabled(false);
        m_exitAction->setEnabled(false);
        m_startAction->setEnabled(true);
        m_debugWithoutDeployAction->setEnabled(true);
        m_visibleStartAction->setAction(m_startAction);
        m_hiddenStopAction->setAction(m_undisturbableAction);
    } else if (state == InferiorStopOk) {
        // F5 continues, Shift-F5 kills. It is "continuable".
        m_interruptAction->setEnabled(false);
        m_continueAction->setEnabled(true);
        m_exitAction->setEnabled(true);
        m_startAction->setEnabled(false);
        m_debugWithoutDeployAction->setEnabled(false);
        m_visibleStartAction->setAction(m_continueAction);
        m_hiddenStopAction->setAction(m_exitAction);
    } else if (state == InferiorRunOk) {
        // Shift-F5 interrupts. It is also "interruptible".
        m_interruptAction->setEnabled(true);
        m_continueAction->setEnabled(false);
        m_exitAction->setEnabled(true);
        m_startAction->setEnabled(false);
        m_debugWithoutDeployAction->setEnabled(false);
        m_visibleStartAction->setAction(m_interruptAction);
        m_hiddenStopAction->setAction(m_interruptAction);
    } else if (state == DebuggerFinished) {
        // We don't want to do anything anymore.
        m_interruptAction->setEnabled(false);
        m_continueAction->setEnabled(false);
        m_exitAction->setEnabled(false);
        m_startAction->setEnabled(true);
        m_debugWithoutDeployAction->setEnabled(true);
        m_visibleStartAction->setAction(m_startAction);
        m_hiddenStopAction->setAction(m_undisturbableAction);
        m_codeModelSnapshot = CPlusPlus::Snapshot();
        setBusyCursor(false);
        cleanupViews();
    } else if (state == InferiorUnrunnable) {
        // We don't want to do anything anymore.
        m_interruptAction->setEnabled(false);
        m_continueAction->setEnabled(false);
        m_exitAction->setEnabled(true);
        m_startAction->setEnabled(false);
        m_debugWithoutDeployAction->setEnabled(false);
        m_visibleStartAction->setAction(m_undisturbableAction);
        m_hiddenStopAction->setAction(m_exitAction);
    } else {
        // Everything else is "undisturbable".
        m_interruptAction->setEnabled(false);
        m_continueAction->setEnabled(false);
        m_exitAction->setEnabled(false);
        m_startAction->setEnabled(false);
        m_debugWithoutDeployAction->setEnabled(false);
        m_visibleStartAction->setAction(m_undisturbableAction);
        m_hiddenStopAction->setAction(m_undisturbableAction);
    }

    m_startLocalProcessAction->setEnabled(true);
    m_attachToQmlPortAction->setEnabled(true);
    m_attachToLocalProcessAction->setEnabled(true);
    m_attachToCoreAction->setEnabled(true);
    m_startRemoteProcessAction->setEnabled(true);
    m_attachToRemoteServerAction->setEnabled(true);
    m_attachToRemoteProcessAction->setEnabled(true);

    m_threadBox->setEnabled(state == InferiorStopOk);

    const bool isCore = engine->startParameters().startMode == AttachCore;
    const bool stopped = state == InferiorStopOk;
    const bool detachable = stopped && !isCore;
    m_detachAction->setEnabled(detachable);

    if (stopped)
        QApplication::alert(mainWindow(), 3000);

    const bool canReverse = engine->hasCapability(ReverseSteppingCapability)
                && boolSetting(EnableReverseDebugging);
    m_reverseDirectionAction->setEnabled(canReverse);

    m_watchAction1->setEnabled(true);
    m_watchAction2->setEnabled(true);
    m_breakAction->setEnabled(true);

    const bool canOperateByInstruction = engine->hasCapability(OperateByInstructionCapability)
            && (stopped || isCore);
    action(OperateByInstruction)->setEnabled(canOperateByInstruction);

    m_abortAction->setEnabled(state != DebuggerNotReady
                                      && state != DebuggerFinished);

    m_stepAction->setEnabled(stopped || state == DebuggerNotReady);
    m_nextAction->setEnabled(stopped || state == DebuggerNotReady);
    m_stepAction->setToolTip(QString());
    m_nextAction->setToolTip(QString());

    m_stepOutAction->setEnabled(stopped);
    m_runToLineAction->setEnabled(stopped && engine->hasCapability(RunToLineCapability));
    m_runToSelectedFunctionAction->setEnabled(stopped);
    m_returnFromFunctionAction->
        setEnabled(stopped && engine->hasCapability(ReturnFromFunctionCapability));

    const bool canJump = stopped && engine->hasCapability(JumpToLineCapability);
    m_jumpToLineAction->setEnabled(canJump);

    const bool canDeref = actionsEnabled && engine->hasCapability(AutoDerefPointersCapability);
    action(AutoDerefPointers)->setEnabled(canDeref);
    action(AutoDerefPointers)->setEnabled(true);
    action(ExpandStack)->setEnabled(actionsEnabled);

    const bool notbusy = state == InferiorStopOk
        || state == DebuggerNotReady
        || state == DebuggerFinished
        || state == InferiorUnrunnable;
    setBusyCursor(!notbusy);

}

void DebuggerPluginPrivate::updateDebugActions()
{
    //if we're currently debugging the actions are controlled by engine
    if (m_currentEngine->state() != DebuggerNotReady)
        return;

    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    Project *project = pe->startupProject();
    const bool canRun = pe->canRun(project, DebugRunMode);
    m_startAction->setEnabled(canRun);
    m_startAction->setToolTip(canRun ? QString() : pe->cannotRunReason(project, DebugRunMode));
    m_debugWithoutDeployAction->setEnabled(canRun);

    // Step into/next: Start and break at 'main' unless a debugger is running.
    if (m_snapshotHandler->currentIndex() < 0) {
        const bool canRunAndBreakMain = pe->canRun(project, DebugRunModeWithBreakOnMain);
        m_stepAction->setEnabled(canRunAndBreakMain);
        m_nextAction->setEnabled(canRunAndBreakMain);
        QString toolTip;
        if (canRunAndBreakMain) {
            QTC_ASSERT(project, return ; );
            toolTip = tr("Start '%1' and break at function 'main()'")
                      .arg(project->displayName());
        } else {
            // Do not display long tooltip saying run mode is not supported
            // for project for projects to which 'break at main' is not applicable.
            if (!canRun)
                toolTip = pe->cannotRunReason(project, DebugRunModeWithBreakOnMain);
        }
        m_stepAction->setToolTip(toolTip);
        m_nextAction->setToolTip(toolTip);
    }
}

void DebuggerPluginPrivate::onCoreAboutToOpen()
{
    m_mainWindow->onModeChanged(ModeManager::currentMode());
}

void DebuggerPluginPrivate::onModeChanged(IMode *mode)
{
     // FIXME: This one gets always called, even if switching between modes
     //        different then the debugger mode. E.g. Welcome and Help mode and
     //        also on shutdown.

    m_mainWindow->onModeChanged(mode);

    if (mode->id() != QLatin1String(Constants::MODE_DEBUG)) {
        m_toolTipManager->leavingDebugMode();
        return;
    }

    EditorManager *editorManager = EditorManager::instance();
    if (editorManager->currentEditor())
        editorManager->currentEditor()->widget()->setFocus();
    m_toolTipManager->debugModeEntered();
}

void DebuggerPluginPrivate::showSettingsDialog()
{
    ICore::showOptionsDialog(
        _(DEBUGGER_SETTINGS_CATEGORY),
        _(DEBUGGER_COMMON_SETTINGS_ID));
}

void DebuggerPluginPrivate::updateDebugWithoutDeployMenu()
{
    const bool state = ProjectExplorerPlugin::instance()->projectExplorerSettings().deployBeforeRun;
    m_debugWithoutDeployAction->setVisible(state);
}

void DebuggerPluginPrivate::dumpLog()
{
    QString fileName = QFileDialog::getSaveFileName(mainWindow(),
        tr("Save Debugger Log"), QDir::tempPath());
    if (fileName.isEmpty())
        return;
    Utils::FileSaver saver(fileName);
    if (!saver.hasError()) {
        QTextStream ts(saver.file());
        ts << m_logWindow->inputContents();
        ts << "\n\n=======================================\n\n";
        ts << m_logWindow->combinedContents();
        saver.setResult(&ts);
    }
    saver.finalize(mainWindow());
}

/*! Activates the previous mode when the current mode is the debug mode. */
void DebuggerPluginPrivate::activatePreviousMode()
{
    if (ModeManager::currentMode() == ModeManager::mode(QLatin1String(MODE_DEBUG))
            && !m_previousMode.isEmpty()) {
        ModeManager::activateMode(m_previousMode);
        m_previousMode.clear();
    }
}

void DebuggerPluginPrivate::activateDebugMode()
{
    m_reverseDirectionAction->setChecked(false);
    m_reverseDirectionAction->setEnabled(false);
    m_previousMode = ModeManager::currentMode()->id();
    ModeManager::activateMode(_(MODE_DEBUG));
}

void DebuggerPluginPrivate::sessionLoaded()
{
    m_breakHandler->loadSessionData();
    dummyEngine()->watchHandler()->loadSessionData();
    m_toolTipManager->loadSessionData();
}

void DebuggerPluginPrivate::aboutToUnloadSession()
{
    m_breakHandler->removeSessionData();
    m_toolTipManager->sessionAboutToChange();
    // Stop debugging the active project when switching sessions.
    // Note that at startup, session switches may occur, which interfere
    // with command-line debugging startup.
    // FIXME ABC: Still wanted? Iterate?
    //if (d->m_engine && state() != DebuggerNotReady
    //    && engine()->sp().startMode == StartInternal)
    //        d->m_engine->shutdown();
}

void DebuggerPluginPrivate::aboutToSaveSession()
{
    dummyEngine()->watchHandler()->saveSessionData();
    m_toolTipManager->saveSessionData();
    m_breakHandler->saveSessionData();
}

void DebuggerPluginPrivate::executeDebuggerCommand(const QString &command, DebuggerLanguages languages)
{
    if (currentEngine()->acceptsDebuggerCommands())
        currentEngine()->executeDebuggerCommand(command, languages);
    else
        showStatusMessage(tr("User commands are not accepted in the current state."));
}

void DebuggerPluginPrivate::showStatusMessage(const QString &msg0, int timeout)
{
    showMessage(msg0, LogStatus);
    QString msg = msg0;
    msg.remove(QLatin1Char('\n'));
    m_statusLabel->showStatusMessage(msg, timeout);
}

bool DebuggerPluginPrivate::evaluateScriptExpression(const QString &expression)
{
    return currentEngine()->evaluateScriptExpression(expression);
}

void DebuggerPluginPrivate::openMemoryEditor()
{
    AddressDialog dialog;
    if (dialog.exec() == QDialog::Accepted)
        currentEngine()->openMemoryView(dialog.address(), 0, QList<MemoryMarkup>(), QPoint());
}

void DebuggerPluginPrivate::coreShutdown()
{
    m_shuttingDown = true;
}

const CPlusPlus::Snapshot &DebuggerPluginPrivate::cppCodeModelSnapshot() const
{
    if (m_codeModelSnapshot.isEmpty() && action(UseCodeModel)->isChecked())
        m_codeModelSnapshot = CPlusPlus::CppModelManagerInterface::instance()->snapshot();
    return m_codeModelSnapshot;
}

void DebuggerPluginPrivate::setSessionValue(const QString &name, const QVariant &value)
{
    QTC_ASSERT(sessionManager(), return);
    sessionManager()->setValue(name, value);
    //qDebug() << "SET SESSION VALUE: " << name;
}

QVariant DebuggerPluginPrivate::sessionValue(const QString &name)
{
    QTC_ASSERT(sessionManager(), return QVariant());
    //qDebug() << "GET SESSION VALUE: " << name;
    return sessionManager()->value(name);
}

void DebuggerPluginPrivate::openTextEditor(const QString &titlePattern0,
    const QString &contents)
{
    if (m_shuttingDown)
        return;
    QString titlePattern = titlePattern0;
    EditorManager *editorManager = EditorManager::instance();
    QTC_ASSERT(editorManager, return);
    IEditor *editor = editorManager->openEditorWithContents(
        CC::K_DEFAULT_TEXT_EDITOR_ID, &titlePattern, contents);
    QTC_ASSERT(editor, return);
    editorManager->activateEditor(editor, EditorManager::IgnoreNavigationHistory);
}


void DebuggerPluginPrivate::clearCppCodeModelSnapshot()
{
    m_codeModelSnapshot = CPlusPlus::Snapshot();
}

void DebuggerPluginPrivate::showMessage(const QString &msg, int channel, int timeout)
{
    //qDebug() << "PLUGIN OUTPUT: " << channel << msg;
    //ConsoleWindow *cw = m_consoleWindow;
    QTC_ASSERT(m_logWindow, return);
    switch (channel) {
        case StatusBar:
            // This will append to m_logWindow's output pane, too.
            showStatusMessage(msg, timeout);
            break;
        case LogMiscInput:
            m_logWindow->showInput(LogMisc, msg);
            m_logWindow->showOutput(LogMisc, msg);
            break;
        case LogInput:
            m_logWindow->showInput(LogInput, msg);
            m_logWindow->showOutput(LogInput, msg);
            break;
        case LogError:
            m_logWindow->showInput(LogError, QLatin1String("ERROR: ") + msg);
            m_logWindow->showOutput(LogError, QLatin1String("ERROR: ") + msg);
            break;
        case QtMessageLogStatus:
            QTC_ASSERT(m_qtMessageLogWindow, return);
            m_qtMessageLogWindow->showStatus(msg, timeout);
            break;
        default:
            m_logWindow->showOutput(channel, msg);
            break;
    }
}

void DebuggerPluginPrivate::showQtDumperLibraryWarning(const QString &details)
{
    QMessageBox dialog(mainWindow());
    QPushButton *qtPref = dialog.addButton(tr("Open Qt Options"),
        QMessageBox::ActionRole);
    QPushButton *helperOff = dialog.addButton(tr("Turn off Helper Usage"),
        QMessageBox::ActionRole);
    QPushButton *justContinue = dialog.addButton(tr("Continue Anyway"),
        QMessageBox::AcceptRole);
    dialog.setDefaultButton(justContinue);
    dialog.setWindowTitle(tr("Debugging Helper Missing"));
    dialog.setText(tr("The debugger could not load the debugging helper library."));
    dialog.setInformativeText(tr(
        "The debugging helper is used to nicely format the values of some Qt "
        "and Standard Library data types. "
        "It must be compiled for each used Qt version separately. "
        "In the Qt Creator Build and Run preferences page, select a Qt version, "
        "expand the Details section and click Build All."));
    if (!details.isEmpty())
        dialog.setDetailedText(details);
    dialog.exec();
    if (dialog.clickedButton() == qtPref) {
        ICore::showOptionsDialog(
            _(ProjectExplorer::Constants::PROJECTEXPLORER_SETTINGS_CATEGORY),
            _(QtSupport::Constants::QTVERSION_SETTINGS_PAGE_ID));
    } else if (dialog.clickedButton() == helperOff) {
        action(UseDebuggingHelpers)->setValue(qVariantFromValue(false), false);
    }
}

void DebuggerPluginPrivate::createNewDock(QWidget *widget)
{
    QDockWidget *dockWidget =
        m_mainWindow->createDockWidget(CppLanguage, widget);
    dockWidget->setWindowTitle(widget->windowTitle());
    dockWidget->setFeatures(QDockWidget::DockWidgetClosable);
    dockWidget->show();
}

static QString formatStartParameters(DebuggerStartParameters &sp)
{
    QString rc;
    QTextStream str(&rc);
    str << "Start parameters: '" << sp.displayName << "' mode: " << sp.startMode
        << "\nABI: " << sp.toolChainAbi.toString() << '\n';
    str << "Languages: ";
    if (sp.languages == AnyLanguage)
        str << "any";
    if (sp.languages & CppLanguage)
        str << "c++ ";
    if (sp.languages & QmlLanguage)
        str << "qml";
    str << '\n';
    if (!sp.executable.isEmpty()) {
        str << "Executable: " << QDir::toNativeSeparators(sp.executable)
            << ' ' << sp.processArgs;
        if (sp.useTerminal)
            str << " [terminal]";
        str << '\n';
        if (!sp.workingDirectory.isEmpty())
            str << "Directory: " << QDir::toNativeSeparators(sp.workingDirectory)
                << '\n';
        if (sp.executableUid) {
            str << "UID: 0x";
            str.setIntegerBase(16);
            str << sp.executableUid << '\n';
            str.setIntegerBase(10);
        }
    }
    if (!sp.debuggerCommand.isEmpty())
        str << "Debugger: " << QDir::toNativeSeparators(sp.debuggerCommand) << '\n';
    if (!sp.coreFile.isEmpty())
        str << "Core: " << QDir::toNativeSeparators(sp.coreFile) << '\n';
    if (sp.attachPID > 0)
        str << "PID: " << sp.attachPID << ' ' << sp.crashParameter << '\n';
    if (!sp.projectSourceDirectory.isEmpty()) {
        str << "Project: " << QDir::toNativeSeparators(sp.projectSourceDirectory);
        if (!sp.projectBuildDirectory.isEmpty())
            str << " (built: " << QDir::toNativeSeparators(sp.projectBuildDirectory)
                << ')';
        str << '\n';
    }
    if (!sp.qtInstallPath.isEmpty())
        str << "Qt: " << QDir::toNativeSeparators(sp.qtInstallPath) << '\n';
    if (!sp.qmlServerAddress.isEmpty())
        str << "QML server: " << sp.qmlServerAddress << ':'
            << sp.qmlServerPort << '\n';
    if (!sp.remoteChannel.isEmpty()) {
        str << "Remote: " << sp.remoteChannel << ", "
            << sp.remoteArchitecture << '\n';
        if (!sp.remoteDumperLib.isEmpty())
            str << "Remote dumpers: " << sp.remoteDumperLib << '\n';
        if (!sp.remoteSourcesDir.isEmpty())
            str << "Remote sources: " << sp.remoteSourcesDir << '\n';
        if (!sp.remoteMountPoint.isEmpty())
            str << "Remote mount point: " << sp.remoteMountPoint
                << " Local: " << sp.localMountDir << '\n';
    }
    if (!sp.gnuTarget.isEmpty())
        str << "Gnu target: " << sp.gnuTarget << '\n';
    str << "Sysroot: " << sp.sysroot << '\n';
    str << "Debug Source Location: " << sp.debugSourceLocation.join(QLatin1String(":")) << '\n';
    str << "Symbol file: " << sp.symbolFileName << '\n';
    if (sp.useServerStartScript)
        str << "Using server start script: " << sp.serverStartScript;
    str << "Dumper libraries: " << QDir::toNativeSeparators(sp.dumperLibrary);
    foreach (const QString &dl, sp.dumperLibraryLocations)
        str << ' ' << QDir::toNativeSeparators(dl);
    str << '\n';
    return rc;
}

void DebuggerPluginPrivate::runControlStarted(DebuggerEngine *engine)
{
    activateDebugMode();
    const QString message = tr("Starting debugger \"%1\" for ABI \"%2\"...")
            .arg(engine->objectName())
            .arg(engine->startParameters().toolChainAbi.toString());
    showStatusMessage(message);
    showMessage(formatStartParameters(engine->startParameters()), LogDebug);
    showMessage(m_debuggerSettings->dump(), LogDebug);
    m_snapshotHandler->appendSnapshot(engine);
    connectEngine(engine);
}

void DebuggerPluginPrivate::runControlFinished(DebuggerEngine *engine)
{
    showStatusMessage(tr("Debugger finished."));
    m_snapshotHandler->removeSnapshot(engine);
    if (m_snapshotHandler->size() == 0) {
        // Last engine quits.
        disconnectEngine();
        if (boolSetting(SwitchModeOnExit))
            activatePreviousMode();
    } else {
        // Connect to some existing engine.
        m_snapshotHandler->activateSnapshot(0);
    }
    action(OperateByInstruction)->setValue(QVariant(false));
}

void DebuggerPluginPrivate::remoteCommand(const QStringList &options,
    const QStringList &)
{
    if (options.isEmpty())
        return;

    unsigned enabledEngines = 0;
    QString errorMessage;

    if (!parseArguments(options, &enabledEngines, &errorMessage)) {
        qWarning("%s", qPrintable(errorMessage));
        return;
    }
    runScheduled();
}

QString DebuggerPluginPrivate::debuggerForAbi(const Abi &abi, DebuggerEngineType et) const
{
    enum { debug = 0 };
    QList<Abi> searchAbis;
    searchAbis.push_back(abi);
    // Pick the right tool chain in case cdb/gdb were started with other tool chains.
    // Also, lldb should be preferred over gdb.
    if (abi.os() == Abi::WindowsOS) {
        switch (et) {
        case CdbEngineType:
            searchAbis.clear();
            searchAbis.push_back(Abi(abi.architecture(), abi.os(),
                Abi::WindowsMsvc2010Flavor, abi.binaryFormat(), abi.wordWidth()));
            searchAbis.push_back(Abi(abi.architecture(), abi.os(),
                Abi::WindowsMsvc2008Flavor, abi.binaryFormat(), abi.wordWidth()));
            searchAbis.push_back(Abi(abi.architecture(), abi.os(),
                Abi::WindowsMsvc2005Flavor, abi.binaryFormat(), abi.wordWidth()));
            break;
        case GdbEngineType:
            searchAbis.clear();
            searchAbis.push_back(Abi(abi.architecture(), abi.os(),
                Abi::WindowsMSysFlavor, abi.binaryFormat(), abi.wordWidth()));
            break;
        default:
            break;
        }
    }
    if (debug)
        qDebug() << "debuggerForAbi" << abi.toString() << searchAbis.size()
                 << searchAbis.front().toString() << et;

    foreach (const Abi &searchAbi, searchAbis) {
        const QList<ToolChain *> toolchains =
            ToolChainManager::instance()->findToolChains(searchAbi);
        // Find manually configured ones first
        for (int i = toolchains.size() - 1; i >= 0; i--) {
            const QString debugger = toolchains.at(i)->debuggerCommand().toString();
            if (debug)
                qDebug() << i << toolchains.at(i)->displayName() << debugger;
            if (!debugger.isEmpty())
                return debugger;
        }
    }
    return QString();
}

DebuggerLanguages DebuggerPluginPrivate::activeLanguages() const
{
    QTC_ASSERT(m_mainWindow, return AnyLanguage);
    return m_mainWindow->activeDebugLanguages();
}

bool DebuggerPluginPrivate::isReverseDebugging() const
{
    return m_reverseDirectionAction->isChecked();
}

QMessageBox *showMessageBox(int icon, const QString &title,
    const QString &text, int buttons)
{
    QMessageBox *mb = new QMessageBox(QMessageBox::Icon(icon),
        title, text, QMessageBox::StandardButtons(buttons),
        debuggerCore()->mainWindow());
    mb->setAttribute(Qt::WA_DeleteOnClose);
    mb->show();
    return mb;
}

void DebuggerPluginPrivate::extensionsInitialized()
{
    m_coreSettings = ICore::settings();

    m_debuggerSettings = new DebuggerSettings(m_coreSettings);
    m_debuggerSettings->readSettings();

    connect(ICore::instance(), SIGNAL(coreAboutToClose()), this, SLOT(coreShutdown()));

    ActionManager *am = ICore::actionManager();
    QTC_ASSERT(am, return);

    m_plugin->addObject(this);

    const Context globalcontext(CC::C_GLOBAL);
    const Context cppDebuggercontext(C_CPPDEBUGGER);
    const Context cppeditorcontext(CppEditor::Constants::C_CPPEDITOR);

    m_startIcon = QIcon(_(":/debugger/images/debugger_start_small.png"));
    m_startIcon.addFile(QLatin1String(":/debugger/images/debugger_start.png"));
    m_exitIcon = QIcon(_(":/debugger/images/debugger_stop_small.png"));
    m_exitIcon.addFile(QLatin1String(":/debugger/images/debugger_stop.png"));
    m_continueIcon = QIcon(QLatin1String(":/debugger/images/debugger_continue_small.png"));
    m_continueIcon.addFile(QLatin1String(":/debugger/images/debugger_continue.png"));
    m_interruptIcon = QIcon(_(":/debugger/images/debugger_interrupt_small.png"));
    m_interruptIcon.addFile(QLatin1String(":/debugger/images/debugger_interrupt.png"));
    m_locationMarkIcon = QIcon(_(":/debugger/images/location_16.png"));

    m_busy = false;

    m_statusLabel = new Utils::StatusLabel;

    m_breakHandler = new BreakHandler;
    m_breakWindow = new BreakWindow;
    m_breakWindow->setObjectName(QLatin1String(DOCKWIDGET_BREAK));
    m_breakWindow->setModel(m_breakHandler->model());

    m_qtMessageLogWindow = new QtMessageLogWindow();
    m_qtMessageLogWindow->setObjectName(QLatin1String(DOCKWIDGET_QML_SCRIPTCONSOLE));
    m_modulesWindow = new ModulesWindow;
    m_modulesWindow->setObjectName(QLatin1String(DOCKWIDGET_MODULES));
    m_logWindow = new LogWindow;
    m_logWindow->setObjectName(QLatin1String(DOCKWIDGET_OUTPUT));
    m_registerWindow = new RegisterWindow;
    m_registerWindow->setObjectName(QLatin1String(DOCKWIDGET_REGISTER));
    m_stackWindow = new StackWindow;
    m_stackWindow->setObjectName(QLatin1String(DOCKWIDGET_STACK));
    m_sourceFilesWindow = new SourceFilesWindow;
    m_sourceFilesWindow->setObjectName(QLatin1String(DOCKWIDGET_SOURCE_FILES));
    m_threadsWindow = new ThreadsWindow;
    m_threadsWindow->setObjectName(QLatin1String(DOCKWIDGET_THREADS));
    m_returnWindow = new WatchWindow(WatchTreeView::ReturnType);
    m_returnWindow->setObjectName(QLatin1String("CppDebugReturn"));
    m_localsWindow = new WatchWindow(WatchTreeView::LocalsType);
    m_localsWindow->setObjectName(QLatin1String("CppDebugLocals"));
    m_watchersWindow = new WatchWindow(WatchTreeView::WatchersType);
    m_watchersWindow->setObjectName(QLatin1String("CppDebugWatchers"));

    // Snapshot
    m_snapshotHandler = new SnapshotHandler;
    m_snapshotWindow = new SnapshotWindow(m_snapshotHandler);
    m_snapshotWindow->setObjectName(QLatin1String(DOCKWIDGET_SNAPSHOTS));
    m_snapshotWindow->setModel(m_snapshotHandler->model());

    // Watchers
    connect(m_localsWindow->header(), SIGNAL(sectionResized(int,int,int)),
        SLOT(updateWatchersHeader(int,int,int)), Qt::QueuedConnection);

    QAction *act = 0;

    act = m_continueAction = new QAction(tr("Continue"), this);
    act->setIcon(m_continueIcon);
    connect(act, SIGNAL(triggered()), SLOT(handleExecContinue()));

    act = m_exitAction = new QAction(tr("Exit Debugger"), this);
    act->setIcon(m_exitIcon);
    connect(act, SIGNAL(triggered()), SLOT(handleExecExit()));

    act = m_interruptAction = new QAction(tr("Interrupt"), this);
    act->setIcon(m_interruptIcon);
    connect(act, SIGNAL(triggered()), SLOT(handleExecInterrupt()));

    // A "disabled pause" seems to be a good choice.
    act = m_undisturbableAction = new QAction(tr("Debugger is Busy"), this);
    act->setIcon(m_interruptIcon);
    act->setEnabled(false);

    act = m_abortAction = new QAction(tr("Abort Debugging"), this);
    act->setToolTip(tr("Aborts debugging and "
        "resets the debugger to the initial state."));
    connect(act, SIGNAL(triggered()), SLOT(handleAbort()));

    act = m_nextAction = new QAction(tr("Step Over"), this);
    act->setIcon(QIcon(QLatin1String(":/debugger/images/debugger_stepover_small.png")));
    connect(act, SIGNAL(triggered()), SLOT(handleExecNext()));

    act = m_stepAction = new QAction(tr("Step Into"), this);
    act->setIcon(QIcon(QLatin1String(":/debugger/images/debugger_stepinto_small.png")));
    connect(act, SIGNAL(triggered()), SLOT(handleExecStep()));

    act = m_stepOutAction = new QAction(tr("Step Out"), this);
    act->setIcon(QIcon(QLatin1String(":/debugger/images/debugger_stepout_small.png")));
    connect(act, SIGNAL(triggered()), SLOT(handleExecStepOut()));

    act = m_runToLineAction = new QAction(tr("Run to Line"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleExecRunToLine()));

    act = m_runToSelectedFunctionAction =
        new QAction(tr("Run to Selected Function"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleExecRunToSelectedFunction()));

    act = m_returnFromFunctionAction =
        new QAction(tr("Immediately Return From Inner Function"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleExecReturn()));

    act = m_jumpToLineAction = new QAction(tr("Jump to Line"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleExecJumpToLine()));

    act = m_breakAction = new QAction(tr("Toggle Breakpoint"), this);

    act = m_watchAction1 = new QAction(tr("Add to Watch Window"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleAddToWatchWindow()));

    act = m_watchAction2 = new QAction(tr("Add to Watch Window"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleAddToWatchWindow()));

    //m_snapshotAction = new QAction(tr("Create Snapshot"), this);
    //m_snapshotAction->setProperty(Role, RequestCreateSnapshotRole);
    //m_snapshotAction->setIcon(
    //    QIcon(__(":/debugger/images/debugger_snapshot_small.png")));

    act = m_reverseDirectionAction =
        new QAction(tr("Reverse Direction"), this);
    act->setCheckable(true);
    act->setChecked(false);
    act->setCheckable(false);
    act->setIcon(QIcon(QLatin1String(":/debugger/images/debugger_reversemode_16.png")));
    act->setIconVisibleInMenu(false);

    act = m_frameDownAction = new QAction(tr("Move to Called Frame"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleFrameDown()));

    act = m_frameUpAction = new QAction(tr("Move to Calling Frame"), this);
    connect(act, SIGNAL(triggered()), SLOT(handleFrameUp()));

    connect(action(OperateByInstruction), SIGNAL(triggered(bool)),
        SLOT(handleOperateByInstructionTriggered(bool)));

    ActionContainer *debugMenu =
        am->actionContainer(ProjectExplorer::Constants::M_DEBUG);

    // Dock widgets
    QDockWidget *dock = 0;
    dock = m_mainWindow->createDockWidget(CppLanguage, m_modulesWindow);
    connect(dock->toggleViewAction(), SIGNAL(toggled(bool)),
        SLOT(modulesDockToggled(bool)), Qt::QueuedConnection);

    dock = m_mainWindow->createDockWidget(CppLanguage, m_registerWindow);
    connect(dock->toggleViewAction(), SIGNAL(toggled(bool)),
        SLOT(registerDockToggled(bool)), Qt::QueuedConnection);

    dock = m_mainWindow->createDockWidget(CppLanguage, m_sourceFilesWindow);
    connect(dock->toggleViewAction(), SIGNAL(toggled(bool)),
        SLOT(sourceFilesDockToggled(bool)), Qt::QueuedConnection);

    dock = m_mainWindow->createDockWidget(AnyLanguage, m_logWindow);
    dock->setProperty(DOCKWIDGET_DEFAULT_AREA, Qt::TopDockWidgetArea);

    m_mainWindow->createDockWidget(CppLanguage, m_breakWindow);
    m_mainWindow->createDockWidget(QmlLanguage, m_qtMessageLogWindow);
    m_mainWindow->createDockWidget(CppLanguage, m_snapshotWindow);
    m_mainWindow->createDockWidget(CppLanguage, m_stackWindow);
    m_mainWindow->createDockWidget(CppLanguage, m_threadsWindow);

    QSplitter *localsAndWatchers = new MiniSplitter(Qt::Vertical);
    localsAndWatchers->setObjectName(QLatin1String(DOCKWIDGET_WATCHERS));
    localsAndWatchers->setWindowTitle(m_localsWindow->windowTitle());
    localsAndWatchers->addWidget(m_localsWindow);
    localsAndWatchers->addWidget(m_returnWindow);
    localsAndWatchers->addWidget(m_watchersWindow);
    localsAndWatchers->setStretchFactor(0, 3);
    localsAndWatchers->setStretchFactor(1, 1);
    localsAndWatchers->setStretchFactor(2, 1);

    dock = m_mainWindow->createDockWidget(CppLanguage, localsAndWatchers);
    dock->setProperty(DOCKWIDGET_DEFAULT_AREA, Qt::RightDockWidgetArea);

    m_mainWindow->addStagedMenuEntries();

    // Do not fail to load the whole plugin if something goes wrong here.
    QString errorMessage;
    if (!parseArguments(m_arguments, &m_cmdLineEnabledEngines, &errorMessage)) {
        errorMessage = tr("Error evaluating command line arguments: %1")
            .arg(errorMessage);
        qWarning("%s\n", qPrintable(errorMessage));
    }

    // Register factory of DebuggerRunControl.
    m_debuggerRunControlFactory = new DebuggerRunControlFactory
        (m_plugin, DebuggerEngineType(m_cmdLineEnabledEngines));
    m_plugin->addAutoReleasedObject(m_debuggerRunControlFactory);

    // The main "Start Debugging" action.
    act = m_startAction = new QAction(this);
    QIcon debuggerIcon(QLatin1String(":/projectexplorer/images/debugger_start_small.png"));
    debuggerIcon.addFile(QLatin1String(":/projectexplorer/images/debugger_start.png"));
    act->setIcon(debuggerIcon);
    act->setText(tr("Start Debugging"));
    connect(act, SIGNAL(triggered()), this, SLOT(debugProject()));

    act = m_debugWithoutDeployAction = new QAction(this);
    act->setText(tr("Start Debugging Without Deployment"));
    connect(act, SIGNAL(triggered()), this, SLOT(debugProjectWithoutDeploy()));

    // Handling of external applications.
    act = m_startLocalProcessAction = new QAction(this);
    act->setText(tr("Start and Debug External Application..."));
    connect(act, SIGNAL(triggered()), SLOT(startExternalApplication()));

#ifdef WITH_LLDB
    act = m_startRemoteLldbAction = new QAction(this);
    act->setText(tr("Start and Debug External Application with External Engine..."));
    connect(act, SIGNAL(triggered()), SLOT(startRemoteEngine()));
#endif

    act = m_attachToLocalProcessAction = new QAction(this);
    act->setText(tr("Attach to Running Local Application..."));
    connect(act, SIGNAL(triggered()), SLOT(attachExternalApplication()));

    act = m_attachToCoreAction = new QAction(this);
    act->setText(tr("Load Core File..."));
    connect(act, SIGNAL(triggered()), SLOT(attachCore()));

    act = m_startRemoteProcessAction = new QAction(this);
    act->setText(tr("Start and Debug Remote Application..."));
    connect(act, SIGNAL(triggered()), SLOT(startRemoteProcess()));

    act = m_attachToRemoteServerAction = new QAction(this);
    act->setText(tr("Attach to Remote Debug Server..."));
    connect(act, SIGNAL(triggered()), SLOT(attachToRemoteServer()));

    act = m_startRemoteServerAction = new QAction(this);
    act->setText(tr("Start Remote Debug Server Attached to Process..."));
    connect(act, SIGNAL(triggered()), SLOT(startRemoteServer()));

    act = m_attachToRemoteProcessAction = new QAction(this);
    act->setText(tr("Attach to Running Remote Process..."));
    connect(act, SIGNAL(triggered()), SLOT(attachToRemoteProcess()));

    act = m_attachToQmlPortAction = new QAction(this);
    act->setText(tr("Attach to QML Port..."));
    connect(act, SIGNAL(triggered()), SLOT(attachToQmlPort()));

#ifdef Q_OS_WIN
    m_startRemoteCdbAction = new QAction(tr("Attach to Remote CDB Session..."), this);
    connect(m_startRemoteCdbAction, SIGNAL(triggered()),
        SLOT(startRemoteCdbSession()));
#endif

    act = m_detachAction = new QAction(this);
    act->setText(tr("Detach Debugger"));
    connect(act, SIGNAL(triggered()), SLOT(handleExecDetach()));

    // "Start Debugging" sub-menu
    // groups:
    //   G_DEFAULT_ONE
    //   G_START_LOCAL
    //   G_START_REMOTE
    //   G_START_QML

    Command *cmd = 0;
    ActionContainer *mstart = am->actionContainer(PE::M_DEBUG_STARTDEBUGGING);

    cmd = am->registerAction(m_startAction, Constants::DEBUG, globalcontext);
    cmd->setDescription(tr("Start Debugging"));
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::DEBUG_KEY)));
    cmd->setAttribute(Command::CA_UpdateText);
    mstart->addAction(cmd, CC::G_DEFAULT_ONE);

    m_visibleStartAction = new Utils::ProxyAction(this);
    m_visibleStartAction->initialize(m_startAction);
    m_visibleStartAction->setAttribute(Utils::ProxyAction::UpdateText);
    m_visibleStartAction->setAttribute(Utils::ProxyAction::UpdateIcon);
    m_visibleStartAction->setAction(cmd->action());

    ModeManager::addAction(m_visibleStartAction, Constants::P_ACTION_DEBUG);

    cmd = am->registerAction(m_debugWithoutDeployAction,
        "Debugger.DebugWithoutDeploy", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, CC::G_DEFAULT_ONE);

    cmd = am->registerAction(m_attachToLocalProcessAction,
        "Debugger.AttachToLocalProcess", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Constants::G_START_LOCAL);

    cmd = am->registerAction(m_startLocalProcessAction,
        "Debugger.StartLocalProcess", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Debugger::Constants::G_START_LOCAL);

    // FIXME: The following actions should some be less
    // visible in the start menu, but still be "there".
    // m_startLocalProcessAction->setVisible(on);
    // m_attachToRemoteServerAction->setVisible(on);
    // m_startRemoteProcessAction->setVisible(on);
    // m_startRemoteServerAction->setVisible(on);

    cmd = am->registerAction(m_attachToCoreAction,
        "Debugger.AttachCore", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Constants::G_START_LOCAL);

    cmd = am->registerAction(m_attachToRemoteServerAction,
        "Debugger.AttachToRemoteServer", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Constants::G_MANUAL_REMOTE);

    cmd = am->registerAction(m_startRemoteProcessAction,
        "Debugger.StartRemoteProcess", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Constants::G_MANUAL_REMOTE);

    cmd = am->registerAction(m_startRemoteServerAction,
         "Debugger.StartRemoteServer", globalcontext);
    cmd->setDescription(tr("Start Gdbserver"));
    mstart->addAction(cmd, Constants::G_MANUAL_REMOTE);

    cmd = am->registerAction(m_attachToRemoteProcessAction,
         "Debugger.AttachToRemoteProcess", globalcontext);
    cmd->setDescription(tr("Attach to Remote Process"));
    mstart->addAction(cmd, Debugger::Constants::G_AUTOMATIC_REMOTE);

#ifdef WITH_LLDB
    cmd = am->registerAction(m_startRemoteLldbAction,
        "Debugger.RemoteLldb", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Constants::G_MANUAL_REMOTE);
#endif

    if (m_startRemoteCdbAction) {
        cmd = am->registerAction(m_startRemoteCdbAction,
             "Debugger.AttachRemoteCdb", globalcontext);
        cmd->setAttribute(Command::CA_Hide);
        mstart->addAction(cmd, Constants::G_MANUAL_REMOTE);
    }

    QAction *sep = new QAction(mstart);
    sep->setSeparator(true);
    cmd = am->registerAction(sep,
        "Debugger.Start.Qml", globalcontext);
    mstart->addAction(cmd, Constants::G_START_QML);

    cmd = am->registerAction(m_attachToQmlPortAction,
        "Debugger.AttachToQmlPort", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    mstart->addAction(cmd, Constants::G_START_QML);

    cmd = am->registerAction(m_detachAction,
        "Debugger.Detach", globalcontext);
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd, CC::G_DEFAULT_ONE);

    cmd = am->registerAction(m_interruptAction,
        Constants::INTERRUPT, globalcontext);
    cmd->setDescription(tr("Interrupt Debugger"));
    debugMenu->addAction(cmd, CC::G_DEFAULT_ONE);

    cmd = am->registerAction(m_continueAction,
        Constants::CONTINUE, globalcontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::DEBUG_KEY)));
    debugMenu->addAction(cmd, CC::G_DEFAULT_ONE);

    cmd = am->registerAction(m_exitAction,
        Constants::STOP, globalcontext);
    cmd->setDescription(tr("Stop Debugger"));
    debugMenu->addAction(cmd, CC::G_DEFAULT_ONE);

    m_hiddenStopAction = new Utils::ProxyAction(this);
    m_hiddenStopAction->initialize(m_exitAction);
    m_hiddenStopAction->setAttribute(Utils::ProxyAction::UpdateText);
    m_hiddenStopAction->setAttribute(Utils::ProxyAction::UpdateIcon);

    cmd = am->registerAction(m_hiddenStopAction,
        Constants::HIDDEN_STOP, globalcontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::STOP_KEY)));

    cmd = am->registerAction(m_abortAction,
        Constants::ABORT, globalcontext);
    //cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::RESET_KEY)));
    cmd->setDescription(tr("Reset Debugger"));
    debugMenu->addAction(cmd, CC::G_DEFAULT_ONE);

    sep = new QAction(this);
    sep->setSeparator(true);
    cmd = am->registerAction(sep, "Debugger.Sep.Step", globalcontext);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_nextAction,
        Constants::NEXT, globalcontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::NEXT_KEY)));
    cmd->setAttribute(Command::CA_Hide);
    cmd->setAttribute(Command::CA_UpdateText);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_stepAction,
        Constants::STEP, globalcontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::STEP_KEY)));
    cmd->setAttribute(Command::CA_Hide);
    cmd->setAttribute(Command::CA_UpdateText);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_stepOutAction,
        Constants::STEPOUT, cppDebuggercontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::STEPOUT_KEY)));
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_runToLineAction,
        "Debugger.RunToLine", cppDebuggercontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::RUN_TO_LINE_KEY)));
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_runToSelectedFunctionAction,
        "Debugger.RunToSelectedFunction", cppDebuggercontext);
    cmd->setDefaultKeySequence(QKeySequence(
        QLatin1String(Constants::RUN_TO_SELECTED_FUNCTION_KEY)));
    cmd->setAttribute(Command::CA_Hide);
    // Don't add to menu by default as keeping its enabled state
    // and text up-to-date is a lot of hassle.
    // debugMenu->addAction(cmd);

    cmd = am->registerAction(m_jumpToLineAction,
        "Debugger.JumpToLine", cppDebuggercontext);
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_returnFromFunctionAction,
        "Debugger.ReturnFromFunction", cppDebuggercontext);
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_reverseDirectionAction,
        Constants::REVERSE, cppDebuggercontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::REVERSE_KEY)));
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd);

    sep = new QAction(this);
    sep->setSeparator(true);
    cmd = am->registerAction(sep, "Debugger.Sep.Break", globalcontext);
    debugMenu->addAction(cmd);

    //cmd = am->registerAction(m_snapshotAction,
    //    "Debugger.Snapshot", cppDebuggercontext);
    //cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::SNAPSHOT_KEY)));
    //cmd->setAttribute(Command::CA_Hide);
    //debugMenu->addAction(cmd);

    cmd = am->registerAction(m_frameDownAction,
        "Debugger.FrameDown", cppDebuggercontext);
    cmd = am->registerAction(m_frameUpAction,
        "Debugger.FrameUp", cppDebuggercontext);

    cmd = am->registerAction(action(OperateByInstruction),
        Constants::OPERATE_BY_INSTRUCTION, cppDebuggercontext);
    cmd->setAttribute(Command::CA_Hide);
    debugMenu->addAction(cmd);

    cmd = am->registerAction(m_breakAction,
        "Debugger.ToggleBreak", globalcontext);
    cmd->setDefaultKeySequence(QKeySequence(QLatin1String(Constants::TOGGLE_BREAK_KEY)));
    debugMenu->addAction(cmd);
    connect(m_breakAction, SIGNAL(triggered()),
        SLOT(toggleBreakpoint()));

    sep = new QAction(this);
    sep->setSeparator(true);
    cmd = am->registerAction(sep, "Debugger.Sep.Watch", globalcontext);
    debugMenu->addAction(cmd);

    // Don't add '1' to the string as it shows up in the shortcut dialog.
    cmd = am->registerAction(m_watchAction1,
        "Debugger.AddToWatch", cppeditorcontext);
    //cmd->setDefaultKeySequence(QKeySequence(tr("Ctrl+D,Ctrl+W")));
    debugMenu->addAction(cmd);

    // If the CppEditor plugin is there, we want to add something to
    // the editor context menu.
    if (ActionContainer *editorContextMenu =
            am->actionContainer(CppEditor::Constants::M_CONTEXT)) {
        cmd = am->registerAction(sep, "Debugger.Sep.Views",
            cppDebuggercontext);
        editorContextMenu->addAction(cmd);
        cmd->setAttribute(Command::CA_Hide);

        cmd = am->registerAction(m_watchAction2,
            "Debugger.AddToWatch2", cppDebuggercontext);
        cmd->action()->setEnabled(true);
        editorContextMenu->addAction(cmd);
        cmd->setAttribute(Command::CA_Hide);
        cmd->setAttribute(Command::CA_NonConfigurable);
        // Debugger.AddToWatch is enough.
    }

    QList<IOptionsPage *> engineOptionPages;
    if (m_cmdLineEnabledEngines & GdbEngineType)
        addGdbOptionPages(&engineOptionPages);
   addCdbOptionPages(&engineOptionPages);
#ifdef WITH_LLDB
    if (m_cmdLineEnabledEngines & LldbEngineType)
        addLldbOptionPages(&engineOptionPages);
#endif

    //if (m_cmdLineEnabledEngines & ScriptEngineType)
    //    addScriptOptionPages(&engineOptionPages);
    //if (m_cmdLineEnabledEngines & TcfEngineType)
    //    addTcfOptionPages(&engineOptionPages);

    foreach (IOptionsPage *op, engineOptionPages)
        m_plugin->addAutoReleasedObject(op);
    m_plugin->addAutoReleasedObject(new DebuggingHelperOptionPage);

    connect(ModeManager::instance(), SIGNAL(currentModeChanged(Core::IMode*)),
        SLOT(onModeChanged(Core::IMode*)));
    connect(ICore::instance(), SIGNAL(coreAboutToOpen()),
        SLOT(onCoreAboutToOpen()));
    connect(ProjectExplorerPlugin::instance(), SIGNAL(settingsChanged()),
            this, SLOT(updateDebugWithoutDeployMenu()));

    // Debug mode setup
    DebugMode *debugMode = new DebugMode;
    QWidget *widget = m_mainWindow->createContents(debugMode);
    widget->setFocusProxy(EditorManager::instance());
    debugMode->setWidget(widget);

    m_plugin->addAutoReleasedObject(debugMode);

    //
    //  Connections
    //

    // Core
    connect(ICore::instance(), SIGNAL(saveSettingsRequested()), SLOT(writeSettings()));

    // TextEditor
    connect(TextEditorSettings::instance(),
        SIGNAL(fontSettingsChanged(TextEditor::FontSettings)),
        SLOT(fontSettingsChanged(TextEditor::FontSettings)));

    // ProjectExplorer
    connect(sessionManager(), SIGNAL(sessionLoaded(QString)),
        SLOT(sessionLoaded()));
    connect(sessionManager(), SIGNAL(aboutToSaveSession()),
        SLOT(aboutToSaveSession()));
    connect(sessionManager(), SIGNAL(aboutToUnloadSession(QString)),
        SLOT(aboutToUnloadSession()));
    connect(ProjectExplorerPlugin::instance(), SIGNAL(updateRunActions()),
        SLOT(updateDebugActions()));

    // EditorManager
    QObject *editorManager = ICore::editorManager();
    connect(editorManager, SIGNAL(editorOpened(Core::IEditor*)),
        SLOT(editorOpened(Core::IEditor*)));
    connect(editorManager, SIGNAL(currentEditorChanged(Core::IEditor*)),
            SLOT(updateBreakMenuItem(Core::IEditor*)));

    // Application interaction
    connect(action(SettingsDialog), SIGNAL(triggered()),
        SLOT(showSettingsDialog()));

    // Toolbar
    QWidget *toolbarContainer = new QWidget;

    QHBoxLayout *hbox = new QHBoxLayout(toolbarContainer);
    hbox->setMargin(0);
    hbox->setSpacing(0);
    hbox->addWidget(toolButton(m_visibleStartAction));
    hbox->addWidget(toolButton(m_exitAction));
    hbox->addWidget(toolButton(m_nextAction));
    hbox->addWidget(toolButton(m_stepAction));
    hbox->addWidget(toolButton(m_stepOutAction));
    hbox->addWidget(toolButton(action(OperateByInstruction)));

    //hbox->addWidget(new Utils::StyledSeparator);
    m_reverseToolButton = toolButton(m_reverseDirectionAction);
    hbox->addWidget(m_reverseToolButton);
    //m_reverseToolButton->hide();

    hbox->addWidget(new Utils::StyledSeparator);
    hbox->addWidget(new QLabel(tr("Threads:")));

    m_threadBox = new QComboBox;
    m_threadBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_threadBox, SIGNAL(activated(int)), SLOT(selectThread(int)));

    hbox->addWidget(m_threadBox);
    hbox->addSpacerItem(new QSpacerItem(4, 0));

    m_mainWindow->setToolBar(CppLanguage, toolbarContainer);
    m_mainWindow->setToolBar(AnyLanguage, m_statusLabel);

    connect(action(EnableReverseDebugging),
        SIGNAL(valueChanged(QVariant)),
        SLOT(enableReverseDebuggingTriggered(QVariant)));

    setInitialState();
    connectEngine(0);

    connect(sessionManager(),
        SIGNAL(startupProjectChanged(ProjectExplorer::Project*)),
        SLOT(onCurrentProjectChanged(ProjectExplorer::Project*)));

    m_commonOptionsPage = new CommonOptionsPage(m_globalDebuggerOptions);
    m_plugin->addAutoReleasedObject(m_commonOptionsPage);

    QTC_CHECK(m_coreSettings);
    m_globalDebuggerOptions->fromSettings(m_coreSettings);
    m_watchersWindow->setVisible(false);
    m_returnWindow->setVisible(false);

    // time gdb -i mi -ex 'b debuggerplugin.cpp:800' -ex r -ex q bin/qtcreator.bin
    if (!m_scheduledStarts.isEmpty())
        QTimer::singleShot(0, this, SLOT(runScheduled()));
}

Utils::SavedAction *DebuggerPluginPrivate::action(int code) const
{
    return m_debuggerSettings->item(code);
}

bool DebuggerPluginPrivate::boolSetting(int code) const
{
    return m_debuggerSettings->item(code)->value().toBool();
}

QString DebuggerPluginPrivate::stringSetting(int code) const
{
    return m_debuggerSettings->item(code)->value().toString();
}

void DebuggerPluginPrivate::showModuleSymbols(const QString &moduleName,
    const Symbols &symbols)
{
    QTreeWidget *w = new QTreeWidget;
    w->setUniformRowHeights(true);
    w->setColumnCount(5);
    w->setRootIsDecorated(false);
    w->setAlternatingRowColors(true);
    w->setSortingEnabled(true);
    w->setObjectName(QLatin1String("Symbols.") + moduleName);
    QStringList header;
    header.append(tr("Symbol"));
    header.append(tr("Address"));
    header.append(tr("Code"));
    header.append(tr("Section"));
    header.append(tr("Name"));
    w->setHeaderLabels(header);
    w->setWindowTitle(tr("Symbols in \"%1\"").arg(moduleName));
    foreach (const Symbol &s, symbols) {
        QTreeWidgetItem *it = new QTreeWidgetItem;
        it->setData(0, Qt::DisplayRole, s.name);
        it->setData(1, Qt::DisplayRole, s.address);
        it->setData(2, Qt::DisplayRole, s.state);
        it->setData(3, Qt::DisplayRole, s.section);
        it->setData(4, Qt::DisplayRole, s.demangled);
        w->addTopLevelItem(it);
    }
    createNewDock(w);
}

void DebuggerPluginPrivate::aboutToShutdown()
{
    m_plugin->removeObject(this);
    disconnect(sessionManager(),
        SIGNAL(startupProjectChanged(ProjectExplorer::Project*)),
        this, 0);
}

} // namespace Internal


///////////////////////////////////////////////////////////////////////
//
// DebuggerPlugin
//
///////////////////////////////////////////////////////////////////////

using namespace Debugger::Internal;

/*!
    \class Debugger::DebuggerPlugin

    This is the "external" interface of the debugger plugin that's visible
    from Qt Creator core. The internal interface to global debugger
    functionality that is used by debugger views and debugger engines
    is DebuggerCore, implemented in DebuggerPluginPrivate. */

DebuggerPlugin::DebuggerPlugin()
{
    theDebuggerCore = new DebuggerPluginPrivate(this);
}

DebuggerPlugin::~DebuggerPlugin()
{
    delete theDebuggerCore;
    theDebuggerCore = 0;
}

bool DebuggerPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    // Menu groups
    const Context globalcontext(CC::C_GLOBAL);

    ActionManager *am = ICore::actionManager();
    ActionContainer *mstart = am->actionContainer(PE::M_DEBUG_STARTDEBUGGING);

    mstart->appendGroup(Constants::G_START_LOCAL);
    mstart->appendGroup(Constants::G_MANUAL_REMOTE);
    mstart->appendGroup(Constants::G_AUTOMATIC_REMOTE);
    mstart->appendGroup(Constants::G_START_QML);

    // Separators
    QAction *sep = new QAction(mstart);
    sep->setSeparator(true);
    Command *cmd = am->registerAction(sep, "Debugger.Local.Cpp", globalcontext);
    mstart->addAction(cmd, Constants::G_START_LOCAL);

    sep = new QAction(mstart);
    sep->setSeparator(true);
    cmd = am->registerAction(sep, "Debugger.StartRemote.Cpp", globalcontext);
    mstart->addAction(cmd, Constants::G_MANUAL_REMOTE);

    sep = new QAction(mstart);
    sep->setSeparator(true);
    cmd = am->registerAction(sep, "Debugger.AttachRemote.Cpp", globalcontext);
    mstart->addAction(cmd, Constants::G_AUTOMATIC_REMOTE);

    return theDebuggerCore->initialize(arguments, errorMessage);
}

IPlugin::ShutdownFlag DebuggerPlugin::aboutToShutdown()
{
    theDebuggerCore->aboutToShutdown();
    return SynchronousShutdown;
}

void DebuggerPlugin::remoteCommand(const QStringList &options,
    const QStringList &list)
{
    theDebuggerCore->remoteCommand(options, list);
}

DebuggerRunControl *DebuggerPlugin::createDebugger
    (const DebuggerStartParameters &sp, RunConfiguration *rc)
{
    return theDebuggerCore->createDebugger(sp, rc);
}

void DebuggerPlugin::startDebugger(RunControl *runControl)
{
    theDebuggerCore->startDebugger(runControl);
}

void DebuggerPlugin::extensionsInitialized()
{
    theDebuggerCore->extensionsInitialized();
}

bool DebuggerPlugin::isActiveDebugLanguage(int language)
{
    return theDebuggerCore->isActiveDebugLanguage(language);
}

DebuggerMainWindow *DebuggerPlugin::mainWindow()
{
    return theDebuggerCore->m_mainWindow;
}

QAction *DebuggerPlugin::visibleDebugAction()
{
    return theDebuggerCore->m_visibleStartAction;
}


#ifdef WITH_TESTS

void DebuggerPluginPrivate::testLoadProject(const QString &proFile, const TestCallBack &cb)
{
    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    connect(pe, SIGNAL(currentProjectChanged(ProjectExplorer::Project*)),
            this, SLOT(testProjectLoaded(ProjectExplorer::Project*)));

    m_testCallbacks.append(cb);
    QString error;
    if (pe->openProject(proFile, &error))
        return; // Will end up in callback.

    // Eat the unused callback.
    qWarning("Cannot open %s: %s", qPrintable(proFile), qPrintable(error));
    QVERIFY(false);
    m_testCallbacks.pop_back();
}

void DebuggerPluginPrivate::testProjectLoaded(Project *project)
{
    if (!project) {
        qWarning("Changed to null project.");
        return;
    }
    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    disconnect(pe, SIGNAL(currentProjectChanged(ProjectExplorer::Project*)),
            this, SLOT(testProjectLoaded(ProjectExplorer::Project*)));

    QString fileName = project->document()->fileName();
    QVERIFY(!fileName.isEmpty());
    qWarning("Project %s loaded", qPrintable(fileName));

    QVERIFY(!m_testCallbacks.isEmpty());
    TestCallBack cb = m_testCallbacks.takeLast();
    invoke<void>(cb.receiver, cb.slot);
}

void DebuggerPluginPrivate::testUnloadProject()
{
    ProjectExplorerPlugin *pe = ProjectExplorerPlugin::instance();
    invoke<void>(pe, "unloadProject");
}

static Project *currentProject()
{
    return ProjectExplorerPlugin::instance()->currentProject();
}

static Target *activeTarget()
{
    Project *project = currentProject();
    return project->activeTarget();
}

static BuildConfiguration *activeBuildConfiguration()
{
    return activeTarget()->activeBuildConfiguration();
}

static RunConfiguration *activeRunConfiguration()
{
    return activeTarget()->activeRunConfiguration();
}

static ToolChain *currentToolChain()
{
    return activeBuildConfiguration()->toolChain();
}

static LocalApplicationRunConfiguration *activeLocalRunConfiguration()
{
    return qobject_cast<LocalApplicationRunConfiguration *>(activeRunConfiguration());
}

void DebuggerPluginPrivate::testRunProject(const DebuggerStartParameters &sp, const TestCallBack &cb)
{
    m_testCallbacks.append(cb);
    RunControl *rctl = createDebugger(sp);
    QVERIFY(rctl);
    connect(rctl, SIGNAL(finished()), this, SLOT(testRunControlFinished()));
    ProjectExplorerPlugin::instance()->startRunControl(rctl, DebugRunMode);
}

void DebuggerPluginPrivate::testRunControlFinished()
{
    QVERIFY(!m_testCallbacks.isEmpty());
    TestCallBack cb = m_testCallbacks.takeLast();
    ExtensionSystem::invoke<void>(cb.receiver, cb.slot);
}

void DebuggerPluginPrivate::testFinished()
{
    QTestEventLoop::instance().exitLoop();
    QVERIFY(m_testSuccess);
}

///////////////////////////////////////////////////////////////////////////

void DebuggerPlugin::testPythonDumpers()
{
    theDebuggerCore->testPythonDumpers1();
}

void DebuggerPluginPrivate::testPythonDumpers1()
{
    m_testSuccess = true;
    QString proFile = ICore::resourcePath()
        + "/../../tests/manual/debugger/simple/simple.pro";
    testLoadProject(proFile, TestCallBack(this,  "testPythonDumpers2"));
    QVERIFY(m_testSuccess);
    QTestEventLoop::instance().enterLoop(20);
}

void DebuggerPluginPrivate::testPythonDumpers2()
{
    DebuggerStartParameters sp;
    sp.toolChainAbi = currentToolChain()->targetAbi();
    sp.executable = activeLocalRunConfiguration()->executable();
    testRunProject(sp, TestCallBack(this, "testPythonDumpers3"));
}

void DebuggerPluginPrivate::testPythonDumpers3()
{
    testUnloadProject();
    testFinished();
}


///////////////////////////////////////////////////////////////////////////

void DebuggerPlugin::testStateMachine()
{
    theDebuggerCore->testStateMachine1();
}

void DebuggerPluginPrivate::testStateMachine1()
{
    m_testSuccess = true;
    QString proFile = ICore::resourcePath()
        + "/../../tests/manual/debugger/simple/simple.pro";
    testLoadProject(proFile, TestCallBack(this,  "testStateMachine2"));
    QVERIFY(m_testSuccess);
    QTestEventLoop::instance().enterLoop(20);
}

void DebuggerPluginPrivate::testStateMachine2()
{
    DebuggerStartParameters sp;
    sp.toolChainAbi = currentToolChain()->targetAbi();
    sp.executable = activeLocalRunConfiguration()->executable();
    sp.testCase = TestNoBoundsOfCurrentFunction;
    testRunProject(sp, TestCallBack(this, "testStateMachine3"));
}

void DebuggerPluginPrivate::testStateMachine3()
{
    testUnloadProject();
    testFinished();
}


///////////////////////////////////////////////////////////////////////////

void DebuggerPlugin::testBenchmark()
{
    theDebuggerCore->testBenchmark1();
}

enum FakeEnum { FakeDebuggerCommonSettingsId };

void DebuggerPluginPrivate::testBenchmark1()
{
#ifdef WITH_BENCHMARK
    CALLGRIND_START_INSTRUMENTATION;
    volatile Core::Id id1 = Core::Id(DEBUGGER_COMMON_SETTINGS_ID);
    CALLGRIND_STOP_INSTRUMENTATION;
    CALLGRIND_DUMP_STATS;

    CALLGRIND_START_INSTRUMENTATION;
    volatile FakeEnum id2 = FakeDebuggerCommonSettingsId;
    CALLGRIND_STOP_INSTRUMENTATION;
    CALLGRIND_DUMP_STATS;
#endif
}


#endif // if  WITH_TESTS

} // namespace Debugger

#include "debuggerplugin.moc"

Q_EXPORT_PLUGIN(Debugger::DebuggerPlugin)
