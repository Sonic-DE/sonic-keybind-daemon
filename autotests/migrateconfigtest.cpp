/*
    SPDX-FileCopyrightText: 2023 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include "globalshortcutsregistry.h"

using namespace Qt::StringLiterals;

class MigrateConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void compareGroups(const KConfigGroup &a, const KConfigGroup &b)
    {
        for (auto [key, value] : a.entryMap().asKeyValueRange()) {
            QCOMPARE(value, b.readEntry(key));
        }

        for (auto [key, value] : b.entryMap().asKeyValueRange()) {
            QCOMPARE(value, a.readEntry(key));
        }
    }

    void compareGroupList(const KConfigBase &a, const KConfigBase &b)
    {
        QCOMPARE(a.groupList(), b.groupList());

        const QStringList groups = a.groupList();
        for (const QString &group : groups) {
            compareGroups(a.group(group), b.group(group));
        }
    }

    void testMigrate()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir systemDataDir;
        QVERIFY(systemDataDir.isValid());
        const bool dataDirsWasSet = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
        const QByteArray originalDataDirs = qgetenv("XDG_DATA_DIRS");
        const auto restoreDataDirs = qScopeGuard([dataDirsWasSet, originalDataDirs] {
            if (dataDirsWasSet) {
                qputenv("XDG_DATA_DIRS", originalDataDirs);
            } else {
                qunsetenv("XDG_DATA_DIRS");
            }
        });
        // An unset XDG_DATA_DIRS falls back to system paths even in test mode.
        QVERIFY(qputenv("XDG_DATA_DIRS", QFile::encodeName(systemDataDir.path())));

        QDir configDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
        configDir.mkpath(QStringLiteral("."));
        configDir.remove(QStringLiteral("kglobalshortcutsrc"));
        configDir.remove(QStringLiteral("sonicglobalshortcutsrc"));

        QDir dataDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
        dataDir.mkpath(QStringLiteral("kglobalaccel"));

        QFile::copy(QFINDTESTDATA("kglobalshortcutsrc"),
                    QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/kglobalshortcutsrc"));
        const QByteArray kdeBefore = readFile(configDir.filePath(QStringLiteral("kglobalshortcutsrc")));

        QFile::copy(QFINDTESTDATA("org.kde.test.desktop"),
                    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/kglobalaccel/org.kde.test.desktop"));

        // Creating the registry will migrate the shortcut config
        GlobalShortcutsRegistry registry;

        // Compare actual with expected config
        KConfig actual(QStringLiteral("sonicglobalshortcutsrc"));
        KConfig expected(QFINDTESTDATA("kglobalshortcutsrc.expected"));

        compareGroupList(actual, expected);
        compareGroupList(actual.group(QStringLiteral("services")), expected.group(QStringLiteral("services")));
        QCOMPARE(readFile(configDir.filePath(QStringLiteral("kglobalshortcutsrc"))), kdeBefore);
    }

    void testImportShell_data()
    {
        QTest::addColumn<QByteArray>("kdeConfig");
        QTest::addColumn<QString>("expectedKey");

        QTest::newRow("plasmashell only") << QByteArray("[plasmashell]\nshow-on-mouse-pos=Meta+V,Meta+V,Show\n[kwin]\nExpose=Ctrl+F9,Ctrl+F9,Expose\n")
                                          << u"Meta+V"_s;
        QTest::newRow("sonicshell from 6.8.2 kept")
            << QByteArray("[plasmashell]\nshow-on-mouse-pos=Meta+V,Meta+V,Show\n[kwin]\nExpose=Ctrl+F9,Ctrl+F9,Expose\n[sonicshell]\nshow-on-mouse-pos=Meta+B,Meta+V,Show\n")
            << u"Meta+B"_s;
    }

    void testImportShell()
    {
        QFETCH(QByteArray, kdeConfig);
        QFETCH(QString, expectedKey);
        QStandardPaths::setTestModeEnabled(true);

        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
        QVERIFY(QDir().mkpath(configDir) && QDir().mkpath(stateDir));
        for (const QString& path : {configDir + u"/kglobalshortcutsrc"_s,
                 configDir + u"/sonicglobalshortcutsrc"_s,
                 stateDir + u"/kglobalshortcutsstaterc"_s,
                 stateDir + u"/sonicglobalshortcutsstaterc"_s}) {
            QFile::remove(path);
        }
        const QByteArray kdeState("[plasmashell]\nshow-on-mouse-pos=25\n");
        QVERIFY(writeFile(configDir + u"/kglobalshortcutsrc"_s, kdeConfig));
        QVERIFY(writeFile(stateDir + u"/kglobalshortcutsstaterc"_s, kdeState));

        {
            GlobalShortcutsRegistry registry;
        }

        KConfig sonic(u"sonicglobalshortcutsrc"_s, KConfig::SimpleConfig);
        QVERIFY(!sonic.hasGroup(u"plasmashell"_s));
        QCOMPARE(sonic.group(u"sonicshell"_s).readEntry(u"show-on-mouse-pos"_s, QStringList()).value(0), expectedKey);
        QCOMPARE(sonic.group(u"kwin"_s).readEntry(u"Expose"_s, QStringList()).value(0), u"Ctrl+F9"_s);
        KConfig state(u"sonicglobalshortcutsstaterc"_s, KConfig::SimpleConfig, QStandardPaths::GenericStateLocation);
        QVERIFY(!state.hasGroup(u"plasmashell"_s));
        QCOMPARE(state.group(u"sonicshell"_s).readEntry(u"show-on-mouse-pos"_s, 0), 25);

        // KDE's files lose only the sonicshell group 6.8.2 put there.
        if (kdeConfig.contains("[sonicshell]")) {
            KConfig kde(configDir + u"/kglobalshortcutsrc"_s, KConfig::SimpleConfig);
            QVERIFY(!kde.hasGroup(u"sonicshell"_s));
            QCOMPARE(kde.group(u"plasmashell"_s).readEntry(u"show-on-mouse-pos"_s, QStringList()).value(0), u"Meta+V"_s);
            QCOMPARE(kde.group(u"kwin"_s).readEntry(u"Expose"_s, QStringList()).value(0), u"Ctrl+F9"_s);
        } else {
            QCOMPARE(readFile(configDir + u"/kglobalshortcutsrc"_s), kdeConfig);
        }
        QCOMPARE(readFile(stateDir + u"/kglobalshortcutsstaterc"_s), kdeState);
    }

    void testCleanKdeState()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
        QVERIFY(QDir().mkpath(configDir) && QDir().mkpath(stateDir));
        QFile::remove(configDir + u"/kglobalshortcutsrc"_s);
        QFile::remove(configDir + u"/sonicglobalshortcutsrc"_s);
        QFile::remove(stateDir + u"/sonicglobalshortcutsstaterc"_s);
        QVERIFY(writeFile(stateDir + u"/kglobalshortcutsstaterc"_s, "[plasmashell]\nshow-on-mouse-pos=25\n[sonicshell]\nshow-on-mouse-pos=312\n"));

        {
            GlobalShortcutsRegistry registry;
        }

        KConfig kde(stateDir + u"/kglobalshortcutsstaterc"_s, KConfig::SimpleConfig);
        QVERIFY(!kde.hasGroup(u"sonicshell"_s));
        QCOMPARE(kde.group(u"plasmashell"_s).readEntry(u"show-on-mouse-pos"_s, 0), 25);
        KConfig sonic(u"sonicglobalshortcutsstaterc"_s, KConfig::SimpleConfig, QStandardPaths::GenericStateLocation);
        QCOMPARE(sonic.group(u"sonicshell"_s).readEntry(u"show-on-mouse-pos"_s, 0), 312);
    }

private:
    static QByteArray readFile(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static bool writeFile(const QString& path, const QByteArray& data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
    }
};

QTEST_MAIN(MigrateConfigTest)

#include "migrateconfigtest.moc"
