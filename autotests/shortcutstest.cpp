/*
    SPDX-FileCopyrightText: 2024 Yifan Zhu <fanzhuyifan@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include <QTest>

#include "component.h"
#include "dummy.h"
#include "globalshortcut.h"
#include "globalshortcutsregistry.h"
#include "kglobalacceld.h"

#include <KConfig>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

Q_IMPORT_PLUGIN(KGlobalAccelImpl)

class RegistryTestBackend : public KGlobalAccelInterface {
public:
    explicit RegistryTestBackend(bool* destroyed = nullptr)
        : KGlobalAccelInterface(nullptr)
        , m_destroyed(destroyed)
    {
    }

    ~RegistryTestBackend() override
    {
        if (m_destroyed) {
            *m_destroyed = true;
        }
    }

    bool grabKey(int key, bool grab) override
    {
        Q_UNUSED(key);
        Q_UNUSED(grab);
        return true;
    }

private:
    bool* m_destroyed;
};

class ShortcutsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testShortcuts_data();
    void testShortcuts();
    void testSerialization();
    void testContestedKeys();
    void testComponentPaths_data();
    void testComponentPaths();
    void testNormalizedShortcuts_data();
    void testNormalizedShortcuts();
    void testBacktab_data();
    void testBacktab();
    void testEmptyShortcut_data();
    void testEmptyShortcut();
    void testRegistryBackendIsolation();
    void testInverseActionLifecycle();
    void testInverseActionPersistence_data();
    void testInverseActionPersistence();
    void testInverseServiceActions_data();
    void testInverseServiceActions();

private:
    void sendKeyCombination(QKeyCombination keyCombination, ShortcutKeyState state);
    void sendKeyCombinationPressAndRelease(QKeyCombination keyCombination);

    std::unique_ptr<KGlobalAccelD> m_globalacceld;
    KGlobalAccelImpl *m_interface; // implementation of KGlobalAccelInterface * for this test
    KGlobalAccel *m_globalaccel;
};

void ShortcutsTest::sendKeyCombination(QKeyCombination keyCombination, ShortcutKeyState state)
{
    struct {
        Qt::KeyboardModifier modifier;
        Qt::Key key;
    } modifiers[] = {
        {
            .modifier = Qt::MetaModifier,
            .key = Qt::Key_Meta,
        },
        {
            .modifier = Qt::AltModifier,
            .key = Qt::Key_Alt,
        },
        {
            .modifier = Qt::ControlModifier,
            .key = Qt::Key_Control,
        },
        {
            .modifier = Qt::ShiftModifier,
            .key = Qt::Key_Shift,
        },
    };

    if (state == ShortcutKeyState::Pressed || state == ShortcutKeyState::Repeated) {
        Qt::KeyboardModifiers formerModifiers;
        for (const auto& [modifier, key] : modifiers) {
            if (keyCombination.keyboardModifiers() & modifier) {
                m_interface->checkKeyEvent((formerModifiers | key).toCombined(), state);
                formerModifiers |= modifier;
            }
        }

        if (keyCombination.key()) {
            m_interface->checkKeyEvent(keyCombination.toCombined(), state);
        }
    } else {
        if (keyCombination.key()) {
            m_interface->checkKeyEvent(keyCombination.toCombined(), state);
        }

        Qt::KeyboardModifiers formerModifiers = keyCombination.keyboardModifiers();
        for (const auto& [modifier, key] : modifiers) {
            if (formerModifiers & modifier) {
                formerModifiers &= ~modifier;
                m_interface->checkKeyEvent((formerModifiers | key).toCombined(), state);
            }
        }
    }
}

void ShortcutsTest::sendKeyCombinationPressAndRelease(QKeyCombination keyCombination)
{
    sendKeyCombination(keyCombination, ShortcutKeyState::Pressed);
    sendKeyCombination(keyCombination, ShortcutKeyState::Released);
}

void ShortcutsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);

    if (const QString filePath = QStandardPaths::locate(QStandardPaths::ConfigLocation, QStringLiteral("kglobalshortcutsrc")); !filePath.isEmpty()) {
        QFile::remove(filePath);
    }

    qputenv("KGLOBALACCELD_PLATFORM", "dummy");
    m_globalacceld = std::make_unique<KGlobalAccelD>();
    QVERIFY(m_globalacceld->init());
    m_interface = KGlobalAccelImpl::instance();
    QVERIFY(m_interface);
    m_globalaccel = KGlobalAccel::self();
    QVERIFY(m_globalaccel);
}

typedef std::pair<QEvent::Type, int> Event;
typedef QList<Event> Events;

void ShortcutsTest::testShortcuts_data()
{
    QTest::addColumn<QKeySequence>("shortcut");
    QTest::addColumn<Events>("events");
    QTest::addColumn<bool>("triggered");

    // make sure all pressed modifiers are released
    QTest::newRow("no mod") << QKeySequence(Qt::Key_A)
                            << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_A) << std::make_pair(QEvent::KeyRelease, Qt::Key_A)) << true;
    QTest::newRow("mod+key trigger") << QKeySequence(Qt::ControlModifier | Qt::Key_P)
                                     << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control)
                                                  << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_P).toCombined())
                                                  << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_P).toCombined())
                                                  << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
                                     << true;
    QTest::newRow("mods+key trigger") << QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_M)
                                      << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control)
                                                   << std::make_pair(QEvent::KeyPress, (Qt::Key_Alt | Qt::ControlModifier).toCombined())
                                                   << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::AltModifier | Qt::Key_M).toCombined())
                                                   << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::AltModifier | Qt::Key_M).toCombined())
                                                   << std::make_pair(QEvent::KeyRelease, (Qt::Key_Alt | Qt::ControlModifier).toCombined())
                                                   << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
                                      << true;
    QTest::newRow("mods+key does not trigger mod+key")
        << QKeySequence(Qt::ControlModifier | Qt::Key_P)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::KeyPress, (Qt::Key_Alt | Qt::ControlModifier).toCombined())
                     << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::AltModifier | Qt::Key_P).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::AltModifier | Qt::Key_P).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::Key_Alt | Qt::ControlModifier).toCombined())
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << false;
    QTest::newRow("mod+key does not trigger mods+key")
        << QKeySequence(Qt::ControlModifier | Qt::AltModifier | Qt::Key_M)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_M).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_M).toCombined())
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << false;

    QTest::newRow("modifier-only single mod") << QKeySequence(
        Qt::Key_Control) << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
                                              << true;
    QTest::newRow("mod+key does not trigger modifier-only single mod")
        << QKeySequence(Qt::Key_Control)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_P).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_P).toCombined())
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << false;
    QTest::newRow("modifier-only multiple mods") << QKeySequence(Qt::ControlModifier | Qt::Key_Alt)
                                                 << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control)
                                                              << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                                                              << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                                                              << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
                                                 << true;
    QTest::newRow("modifier-only multiple mods trigger when released out of order")
        << QKeySequence(Qt::ControlModifier | Qt::Key_Alt)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Alt) << std::make_pair(QEvent::KeyPress, (Qt::AltModifier | Qt::Key_Control).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << true;
    QTest::newRow("modifier-only multiple mods does not trigger modifier-only single mod")
        << QKeySequence(Qt::Key_Control)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << false;
    QTest::newRow("modifier-only multiple mods trigger when part of the sequence")
        << QKeySequence(Qt::ControlModifier | Qt::Key_Alt)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control)
                     << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_Shift).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_Shift).toCombined())
                     << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::AltModifier | Qt::Key_Control).toCombined()) << std::make_pair(QEvent::KeyRelease, Qt::Key_Alt))
        << true;
    QTest::newRow("modifier-only multiple mods trigger when any mod is released")
        << QKeySequence(Qt::ControlModifier | Qt::Key_Shift)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control)
                     << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_Shift).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::ControlModifier | Qt::Key_Shift).toCombined())
                     << std::make_pair(QEvent::KeyPress, (Qt::ControlModifier | Qt::Key_Alt).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::AltModifier | Qt::Key_Control).toCombined()) << std::make_pair(QEvent::KeyRelease, Qt::Key_Alt))
        << true;
    QTest::newRow("mod+pointer does not trigger modifier-only single mod")
        << QKeySequence(Qt::Key_Control)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::MouseButtonPress, Qt::LeftButton)
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << false;
    QTest::newRow("mod+wheel does not trigger modifier-only single mod")
        << QKeySequence(Qt::Key_Control)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_Control) << std::make_pair(QEvent::Wheel, 0)
                     << std::make_pair(QEvent::KeyRelease, Qt::Key_Control))
        << false;
    QTest::newRow("mod+invalid does not trigger modifier-only single mod")
        << QKeySequence(Qt::Key_Control)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::ControlModifier) << std::make_pair(QEvent::KeyRelease, Qt::ControlModifier)) << false;
    QTest::newRow("shift-backtab matches shift-tab")
        << QKeySequence(Qt::SHIFT | Qt::Key_Tab)
        << (Events() << std::make_pair(QEvent::KeyPress, (Qt::SHIFT | Qt::Key_Backtab).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::SHIFT | Qt::Key_Backtab).toCombined()))
        << true;
    QTest::newRow("normalized multikey suffix after unrelated keys")
        << QKeySequence(Qt::CTRL | Qt::Key_K, Qt::SHIFT | Qt::Key_Tab)
        << (Events() << std::make_pair(QEvent::KeyPress, Qt::Key_A) << std::make_pair(QEvent::KeyRelease, Qt::Key_A)
                     << std::make_pair(QEvent::KeyPress, Qt::Key_B) << std::make_pair(QEvent::KeyRelease, Qt::Key_B)
                     << std::make_pair(QEvent::KeyPress, Qt::Key_C) << std::make_pair(QEvent::KeyRelease, Qt::Key_C)
                     << std::make_pair(QEvent::KeyPress, (Qt::CTRL | Qt::Key_K).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::CTRL | Qt::Key_K).toCombined())
                     << std::make_pair(QEvent::KeyPress, (Qt::SHIFT | Qt::Key_Backtab).toCombined())
                     << std::make_pair(QEvent::KeyRelease, (Qt::SHIFT | Qt::Key_Backtab).toCombined()))
        << true;
}

void ShortcutsTest::testShortcuts()
{
    auto action = std::make_unique<QAction>();
    action->setObjectName(QStringLiteral("ActionForShortcutTest"));
    QFETCH(QKeySequence, shortcut);
    QVERIFY(KGlobalAccel::setGlobalShortcut(action.get(), shortcut));
    QCOMPARE(m_globalaccel->shortcut(action.get()), QList<QKeySequence>() << shortcut);

    QSignalSpy spy(action.get(), &QAction::triggered);

    QFETCH(Events, events);
    for (const auto &event : events) {
        switch (event.first) {
        case QEvent::KeyPress:
            m_interface->checkKeyEvent(event.second, ShortcutKeyState::Pressed);
            break;
        case QEvent::KeyRelease:
            m_interface->checkKeyEvent(event.second, ShortcutKeyState::Released);
            break;
        case QEvent::MouseButtonPress:
            m_interface->checkPointerPressed(static_cast<Qt::MouseButtons>(event.second));
            break;
        case QEvent::Wheel:
            m_interface->checkAxisTriggered(event.second);
            break;
        default:
            qFatal("Unknown event type");
        }
    }

    QFETCH(bool, triggered);
    if (triggered) {
        QVERIFY(spy.wait());
        QCOMPARE(spy.count(), 1);
    } else {
        QVERIFY(!spy.wait(100));
        QCOMPARE(spy.count(), 0);
    }
    m_globalaccel->removeAllShortcuts(action.get());
}

void ShortcutsTest::testSerialization()
{
    QCOMPARE(Component::keysFromString(QLatin1String("")), QSet<QKeySequence>());
    QCOMPARE(Component::keysFromString(QLatin1String("\t\t")), QSet<QKeySequence>());
    QCOMPARE(Component::keysFromString(QLatin1String("none")), QSet<QKeySequence>());
    QCOMPARE(Component::stringFromKeys(QSet<QKeySequence>()), QLatin1String("none"));

    QCOMPARE(Component::keysFromString(QLatin1String("Ctrl+P")), QSet<QKeySequence>() << QKeySequence(Qt::CTRL | Qt::Key_P));
    QCOMPARE(Component::stringFromKeys(QSet<QKeySequence>() << QKeySequence(Qt::CTRL | Qt::Key_P)), QLatin1String("Ctrl+P"));

    QCOMPARE(Component::keysFromString(QLatin1String("Backtab\tShift+Backtab\tShift+Tab")), QSet<QKeySequence>{QKeySequence(Qt::SHIFT | Qt::Key_Tab)});

    QCOMPARE(Component::keysFromString(QLatin1String("\tCtrl+P")), QSet<QKeySequence>() << QKeySequence(Qt::CTRL | Qt::Key_P));
    QCOMPARE(Component::keysFromString(QLatin1String("\tCtrl+P\t")), QSet<QKeySequence>() << QKeySequence(Qt::CTRL | Qt::Key_P));
    QCOMPARE(Component::stringFromKeys(QSet<QKeySequence>() << QKeySequence(Qt::CTRL | Qt::Key_P)), QLatin1String("Ctrl+P"));
}

void ShortcutsTest::testContestedKeys()
{
    const QKeySequence shortcut(Qt::META | Qt::CTRL | Qt::Key_K);

    auto firstAction = std::make_unique<QAction>();
    firstAction->setObjectName(QStringLiteral("First Shortcut"));
    QVERIFY(KGlobalAccel::setGlobalShortcut(firstAction.get(), shortcut));
    QSignalSpy firstActionTriggeredSpy(firstAction.get(), &QAction::triggered);

    auto secondAction = std::make_unique<QAction>();
    secondAction->setObjectName(QStringLiteral("Second Shortcut"));
    QVERIFY(KGlobalAccel::setGlobalShortcut(secondAction.get(), shortcut));
    QSignalSpy secondActionTriggeredSpy(secondAction.get(), &QAction::triggered);

    sendKeyCombination(Qt::MetaModifier | Qt::ControlModifier | Qt::Key_K, ShortcutKeyState::Pressed);

    QVERIFY(firstActionTriggeredSpy.wait());
    QCOMPARE(firstActionTriggeredSpy.count(), 1);
    QVERIFY(!secondActionTriggeredSpy.wait(100));
    QCOMPARE(secondActionTriggeredSpy.count(), 0);

    sendKeyCombination(Qt::MetaModifier | Qt::ControlModifier | Qt::Key_K, ShortcutKeyState::Released);

    m_globalaccel->removeAllShortcuts(firstAction.get());
    m_globalaccel->removeAllShortcuts(secondAction.get());
}

void ShortcutsTest::testComponentPaths_data()
{
    QTest::addColumn<QString>("component");
    QTest::addColumn<QString>("expectedPath");
    QTest::newRow("simple") << QStringLiteral("path_test") << QStringLiteral("/component/path_test");
    QTest::newRow("punctuation") << QStringLiteral("path.test-app") << QStringLiteral("/component/path_test_app");
    QTest::newRow("slash-and-space") << QStringLiteral("path/test app") << QStringLiteral("/component/path_test_app");
}

void ShortcutsTest::testComponentPaths()
{
    QFETCH(QString, component);
    QFETCH(QString, expectedPath);
    const QString action = QStringLiteral("path-test-action");
    m_globalacceld->doRegister({component, action, component, action});
    const QDBusObjectPath path = m_globalacceld->getComponent(component);
    QCOMPARE(path.path(), expectedPath);
    QVERIFY(m_globalacceld->allComponents().contains(path));
    QVERIFY(m_globalacceld->unregister(component, action));
    QTRY_VERIFY(!m_globalacceld->allComponents().contains(path));
}

void ShortcutsTest::testNormalizedShortcuts_data()
{
    QTest::addColumn<QKeySequence>("input");
    QTest::addColumn<QKeySequence>("normalized");

    QTest::newRow("control-only") << QKeySequence(Qt::ControlModifier) << QKeySequence(Qt::Key_Control);
    QTest::newRow("control-alt-only") << QKeySequence(Qt::ControlModifier | Qt::AltModifier)
                                      << QKeySequence(Qt::ControlModifier | Qt::Key_Alt);
    QTest::newRow("shift-backtab") << QKeySequence(Qt::SHIFT | Qt::Key_Backtab) << QKeySequence(Qt::SHIFT | Qt::Key_Tab);
    QTest::newRow("bare-backtab") << QKeySequence(Qt::Key_Backtab) << QKeySequence(Qt::SHIFT | Qt::Key_Tab);
    QTest::newRow("control-backtab") << QKeySequence(Qt::CTRL | Qt::Key_Backtab) << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab);
    QTest::newRow("multikey-backtab") << QKeySequence(Qt::CTRL | Qt::Key_K, Qt::SHIFT | Qt::Key_Backtab)
                                      << QKeySequence(Qt::CTRL | Qt::Key_K, Qt::SHIFT | Qt::Key_Tab);
    QTest::newRow("multikey-bare-backtab") << QKeySequence(Qt::CTRL | Qt::Key_K, Qt::Key_Backtab)
                                           << QKeySequence(Qt::CTRL | Qt::Key_K, Qt::SHIFT | Qt::Key_Tab);
}

void ShortcutsTest::testNormalizedShortcuts()
{
    QFETCH(QKeySequence, input);
    QFETCH(QKeySequence, normalized);

    const QStringList actionId{QStringLiteral("normalization-test"), QStringLiteral("action"), QStringLiteral("Normalization Test"), QStringLiteral("Action")};
    m_globalacceld->doRegister(actionId);
    QCOMPARE(
        m_globalacceld->setShortcutKeys(actionId, {input, normalized}, KGlobalAccelD::NoAutoloading | KGlobalAccelD::SetPresent), QSet<QKeySequence>{normalized});
    QCOMPARE(m_globalacceld->setShortcutKeys(actionId, {input}, KGlobalAccelD::IsDefault), QSet<QKeySequence>{normalized});
    QCOMPARE(m_globalacceld->defaultShortcutKeys(actionId), QSet<QKeySequence>{normalized});
    QCOMPARE(m_globalacceld->actionList(input), actionId);
    QCOMPARE(m_globalacceld->globalShortcutsByKey(input, KGlobalAccel::Equal).size(), 1);
    QVERIFY(!m_globalacceld->globalShortcutAvailable(normalized, QStringLiteral("another-component")));
    const bool inputAvailable = m_globalacceld->globalShortcutAvailable(input, QStringLiteral("another-component"));
    QVERIFY(m_globalacceld->unregister(actionId[0], actionId[1]));
    QVERIFY(!inputAvailable);
    QVERIFY(m_globalacceld->globalShortcutAvailable(input, QStringLiteral("another-component")));
}

void ShortcutsTest::testBacktab_data()
{
    QTest::addColumn<QKeySequence>("shortcut");

    QTest::addRow("Shift+Tab") << QKeySequence(Qt::ShiftModifier | Qt::Key_Tab);
    QTest::addRow("Backtab") << QKeySequence(Qt::Key_Backtab);
    QTest::addRow("Shift+Backtab") << QKeySequence(Qt::ShiftModifier | Qt::Key_Backtab);
}

void ShortcutsTest::testBacktab()
{
    QFETCH(QKeySequence, shortcut);

    auto action = std::make_unique<QAction>();
    action->setObjectName(QStringLiteral("Backtab Shortcut"));
    QVERIFY(KGlobalAccel::setGlobalShortcut(action.get(), shortcut));
    QSignalSpy actionTriggeredSpy(action.get(), &QAction::triggered);

    // Shift+Tab should be treated as Backtab or Shift+Backtab. Note that Tab should not treated as Backtab.
    sendKeyCombinationPressAndRelease(Qt::Key_Tab);
    QVERIFY(!actionTriggeredSpy.wait(100));

    sendKeyCombinationPressAndRelease(Qt::ShiftModifier | Qt::Key_Tab);
    QVERIFY(actionTriggeredSpy.wait());

    sendKeyCombinationPressAndRelease(Qt::MetaModifier | Qt::Key_Tab);
    QVERIFY(!actionTriggeredSpy.wait(100));

    sendKeyCombinationPressAndRelease(Qt::MetaModifier | Qt::ShiftModifier | Qt::Key_Tab);
    QVERIFY(!actionTriggeredSpy.wait(100));

    // Backtab and Shift+Backtab should be treated as Shift+Tab.
    sendKeyCombinationPressAndRelease(Qt::Key_Backtab);
    QVERIFY(actionTriggeredSpy.wait());

    sendKeyCombinationPressAndRelease(Qt::ShiftModifier | Qt::Key_Backtab);
    QVERIFY(actionTriggeredSpy.wait());

    sendKeyCombinationPressAndRelease(Qt::MetaModifier | Qt::Key_Backtab);
    QVERIFY(!actionTriggeredSpy.wait(100));

    sendKeyCombinationPressAndRelease(Qt::MetaModifier | Qt::ShiftModifier | Qt::Key_Backtab);
    QVERIFY(!actionTriggeredSpy.wait(100));

    m_globalaccel->removeAllShortcuts(action.get());
}

void ShortcutsTest::testEmptyShortcut_data()
{
    QTest::addColumn<QList<QKeySequence>>("shortcuts");
    QTest::addColumn<QList<QKeySequence>>("expected");

    QTest::addRow("List()") << QList<QKeySequence>{} << QList<QKeySequence>{};

    QTest::addRow("List(QKeySequence())") << QList<QKeySequence>{QKeySequence()} << QList<QKeySequence>{};

    QTest::addRow("List(QKeySequence(), QKeySequence(Meta+A))")
        << QList<QKeySequence>{QKeySequence(), QKeySequence(Qt::MetaModifier | Qt::Key_A)} << QList<QKeySequence>{QKeySequence(Qt::MetaModifier | Qt::Key_A)};
    QTest::addRow("List(QKeySequence(Meta+A), QKeySequence())")
        << QList<QKeySequence>{QKeySequence(Qt::MetaModifier | Qt::Key_A), QKeySequence()} << QList<QKeySequence>{QKeySequence(Qt::MetaModifier | Qt::Key_A)};
}

void ShortcutsTest::testEmptyShortcut()
{
    auto action = std::make_unique<QAction>();
    action->setObjectName(QStringLiteral("Empty Shortcut"));
    QFETCH(QList<QKeySequence>, shortcuts);
    QVERIFY(KGlobalAccel::setGlobalShortcut(action.get(), shortcuts));
    QTEST(m_globalaccel->shortcut(action.get()), "expected");

    m_globalaccel->removeAllShortcuts(action.get());

    const QStringList actionId{QStringLiteral("empty-test"), QStringLiteral("action"), QStringLiteral("Empty Test"), QStringLiteral("Action")};
    const QSet<QKeySequence> input(shortcuts.cbegin(), shortcuts.cend());
    QFETCH(QList<QKeySequence>, expected);
    const QSet<QKeySequence> expectedKeys(expected.cbegin(), expected.cend());
    const QKeySequence previousShortcut(Qt::CTRL | Qt::ALT | Qt::Key_E);
    m_globalacceld->doRegister(actionId);
    QCOMPARE(
        m_globalacceld->setShortcutKeys(actionId, {previousShortcut}, KGlobalAccelD::NoAutoloading | KGlobalAccelD::SetPresent), QSet<QKeySequence>{previousShortcut});
    QCOMPARE(m_globalacceld->setShortcutKeys(actionId, input, KGlobalAccelD::NoAutoloading | KGlobalAccelD::SetPresent), expectedKeys);
    QCOMPARE(m_globalacceld->shortcutKeys(actionId), expectedKeys);
    QVERIFY(!m_interface->checkKeyEvent(previousShortcut[0].toCombined(), ShortcutKeyState::Pressed));
    m_interface->checkKeyEvent(previousShortcut[0].toCombined(), ShortcutKeyState::Released);
    QCOMPARE(m_globalacceld->setShortcutKeys(actionId, input, KGlobalAccelD::IsDefault), expectedKeys);
    QCOMPARE(m_globalacceld->defaultShortcutKeys(actionId), expectedKeys);
    QCOMPARE(Component::keysFromString(Component::stringFromKeys(expectedKeys)), expectedKeys);
    QVERIFY(m_globalacceld->unregister(actionId[0], actionId[1]));
}

void ShortcutsTest::testRegistryBackendIsolation()
{
    bool backendDestroyed = false;
    QObject* mainRegistry = m_interface->parent();
    {
        auto backend = std::make_unique<RegistryTestBackend>(&backendDestroyed);
        auto* backendPtr = backend.get();
        GlobalShortcutsRegistry registry(std::move(backend));
        QCOMPARE(registry.interface(), static_cast<KGlobalAccelInterface*>(backendPtr));
        QCOMPARE(backendPtr->parent(), static_cast<QObject*>(&registry));
        QVERIFY(!backendDestroyed);
        QCOMPARE(m_interface->parent(), mainRegistry);
        QCOMPARE(KGlobalAccelImpl::instance(), m_interface);
    }
    QVERIFY(backendDestroyed);
    QCOMPARE(m_interface->parent(), mainRegistry);
    QCOMPARE(KGlobalAccelImpl::instance(), m_interface);

    auto action = std::make_unique<QAction>();
    action->setObjectName(QStringLiteral("backend-isolation-action"));
    const QKeyCombination key = Qt::CTRL | Qt::Key_F11;
    QVERIFY(KGlobalAccel::setGlobalShortcut(action.get(), QKeySequence(key)));
    QSignalSpy triggered(action.get(), &QAction::triggered);
    sendKeyCombinationPressAndRelease(key);
    QVERIFY(triggered.wait());
    m_globalaccel->removeAllShortcuts(action.get());
}

void ShortcutsTest::testInverseActionLifecycle()
{
    const QString component = QStringLiteral("inverse-lifecycle-test");
    for (const QString& action : {QStringLiteral("forward"), QStringLiteral("backward"), QStringLiteral("other")}) {
        const QStringList actionId{component, action, component, action};
        m_globalacceld->doRegister(actionId);
        m_globalacceld->setShortcutKeys(actionId, {}, KGlobalAccelD::NoAutoloading | KGlobalAccelD::SetPresent);
    }
    QVERIFY(!m_globalacceld->setInverseShortcutActions(component, QStringLiteral("forward"), QStringLiteral("missing"), 0));
    QVERIFY(!m_globalacceld->setInverseShortcutActions(component, QStringLiteral("forward"), QStringLiteral("backward"), 1));
    QVERIFY(m_globalacceld->setInverseShortcutActions(component, QStringLiteral("forward"), QStringLiteral("backward"), 0));
    QVERIFY(m_globalacceld->setInverseShortcutActions(component, QStringLiteral("backward"), QStringLiteral("forward"), 0));
    QVERIFY(!m_globalacceld->setInverseShortcutActions(component, QStringLiteral("forward"), QStringLiteral("other"), 0));
    QVERIFY(m_globalacceld->unregister(component, QStringLiteral("forward")));
    const bool reassigned = m_globalacceld->setInverseShortcutActions(component, QStringLiteral("backward"), QStringLiteral("other"), 0);
    QVERIFY(m_globalacceld->unregister(component, QStringLiteral("backward")));
    QVERIFY(m_globalacceld->unregister(component, QStringLiteral("other")));
    QVERIFY(reassigned);
}

void ShortcutsTest::testInverseActionPersistence_data()
{
    QTest::addColumn<QString>("context");
    QTest::addColumn<QString>("partner");
    QTest::addColumn<bool>("fresh");
    QTest::newRow("default") << QStringLiteral("default") << QStringLiteral("backward") << false;
    QTest::newRow("named-context") << QStringLiteral("workspace") << QStringLiteral("backward") << false;
    QTest::newRow("session-partner") << QStringLiteral("default") << QStringLiteral("_k_session:backward") << false;
    QTest::newRow("fresh-partner") << QStringLiteral("default") << QStringLiteral("backward") << true;
}

void ShortcutsTest::testInverseActionPersistence()
{
    QFETCH(QString, context);
    QFETCH(QString, partner);
    QFETCH(bool, fresh);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    KConfig config(directory.filePath(QStringLiteral("shortcutsrc")), KConfig::SimpleConfig);
    KConfig state(directory.filePath(QStringLiteral("staterc")), KConfig::SimpleConfig);
    KConfigGroup configGroup(&config, QStringLiteral("inverse-persistence"));
    KConfigGroup stateGroup(&state, QStringLiteral("inverse-persistence"));
    GlobalShortcutsRegistry registry(std::make_unique<RegistryTestBackend>());
    Component component(QStringLiteral("inverse-persistence"), QString(), &registry);
    if (context != QLatin1String("default")) {
        QVERIFY(component.createGlobalShortcutContext(context));
        QVERIFY(component.activateGlobalShortcutContext(context));
    }
    component.registerShortcut(QStringLiteral("forward"), QString(), QStringLiteral("none"), QStringLiteral("none"), 1);
    auto* backward = component.registerShortcut(partner, QString(), QStringLiteral("none"), QStringLiteral("none"), 2);
    backward->_isFresh = fresh;
    component.loadInverseAction(QStringLiteral("forward"), {partner});
    QCOMPARE(backward->_inverseActionUniqueName, QStringLiteral("forward"));
    component.writeSettings(configGroup, stateGroup);

    const KConfigGroup contextGroup = context == QLatin1String("default") ? configGroup : configGroup.group(context);
    const bool persistent = !fresh && !partner.startsWith(QLatin1String("_k_session:"));
    QCOMPARE(contextGroup.group(QStringLiteral("$InverseAction")).keyList().size(), persistent ? 1 : 0);
    Component reloaded(QStringLiteral("inverse-persistence-reloaded"), QString(), &registry);
    if (context != QLatin1String("default")) {
        QVERIFY(reloaded.createGlobalShortcutContext(context));
        QVERIFY(reloaded.activateGlobalShortcutContext(context));
    }
    reloaded.loadSettings(contextGroup, stateGroup);
    const auto* forward = reloaded.getShortcutByName(QStringLiteral("forward"), context);
    QVERIFY(forward);
    QCOMPARE(forward->_inverseActionUniqueName, persistent ? partner : QString());
    if (persistent) {
        QCOMPARE(reloaded.getShortcutByName(partner, context)->_inverseActionUniqueName, QStringLiteral("forward"));
    }
    QVERIFY(Component::isReservedConfigGroupName(QStringLiteral("$InverseAction")));
}

void ShortcutsTest::testInverseServiceActions_data()
{
    QTest::addColumn<bool>("metadata");
    QTest::newRow("runtime-pair-roundtrip") << false;
    QTest::newRow("metadata-pair-with-launch") << true;
}

void ShortcutsTest::testInverseServiceActions()
{
    QFETCH(bool, metadata);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kglobalaccel");
    QVERIFY(QDir().mkpath(directory));
    QTemporaryFile desktop(directory + QStringLiteral("/inverse-service-XXXXXX.desktop"));
    QVERIFY(desktop.open());
    QByteArray contents = "[Desktop Entry]\nType=Application\nName=Inverse Test\nExec=true\nActions=forward;backward;\n"
                          "[Desktop Action forward]\nName=Forward\nExec=true\n";
    if (metadata) {
        contents += "X-KDE-InverseAction=_launch\n";
    }
    contents += "[Desktop Action backward]\nName=Backward\nExec=true\n";
    QCOMPARE(desktop.write(contents), contents.size());
    QVERIFY(desktop.flush());
    const QString componentName = QFileInfo(desktop.fileName()).fileName();
    GlobalShortcutsRegistry registry(std::make_unique<RegistryTestBackend>());
    registry.loadSettings();
    Component* component = registry.getComponent(componentName);
    QVERIFY(component);
    const QString partner = metadata ? QStringLiteral("_launch") : QStringLiteral("backward");
    if (!metadata) {
        component->loadInverseAction(QStringLiteral("forward"), {partner});
    }
    QCOMPARE(component->getShortcutByName(QStringLiteral("forward"))->_inverseActionUniqueName, partner);
    registry.writeSettings();
    registry.clear();
    registry.loadSettings();
    component = registry.getComponent(componentName);
    QVERIFY(component);
    QCOMPARE(component->getShortcutByName(QStringLiteral("forward"))->_inverseActionUniqueName, partner);
    QCOMPARE(component->getShortcutByName(partner)->_inverseActionUniqueName, QStringLiteral("forward"));
    for (const QString& action : {QStringLiteral("forward"), QStringLiteral("backward"), QStringLiteral("_launch")}) {
        component->unregisterShortcut(action);
    }
    registry.writeSettings();
}

QTEST_MAIN(ShortcutsTest)

#include "shortcutstest.moc"
