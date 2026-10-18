#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "../src/OnboardManager.h"

class OnboardManagerTest : public QObject {
    Q_OBJECT
private slots:
    void requestsAreExplicitAndSshOwnsOnlyItsTunnel()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString fake = dir.filePath("ssh");
        QFile file(fake); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray script = "#!/usr/bin/env python3\nimport sys,json,time\n"
            "if '-N' in sys.argv:\n time.sleep(100)\nelse:\n"
            " d=json.loads(sys.stdin.readline())\n"
            " with open('" + dir.filePath("requests").toUtf8() + "','a') as f:f.write(json.dumps(d)+'\\n')\n"
            " print(json.dumps({'ok':True,'status':{'modules':[{'name':'flight','state':'stopped','missing':[]}]},'logs':'test-log'}))\n";
        file.write(script); file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        OnboardManager manager(nullptr, fake);
        manager.connectHost("-oProxyCommand=x", "ly", "/home/ly/work");
        QVERIFY(!manager.busy()); QVERIFY(!manager.connected());
        manager.connectHost("192.168.2.112", "ly", "/home/ly/work;echo");
        QVERIFY(!manager.busy());
        manager.connectHost("192.168.2.112", "ly", "/home/ly/work");
        QTRY_VERIFY(manager.connected()); QTRY_VERIFY(!manager.busy());
        QFile requests(dir.filePath("requests")); QVERIFY(requests.open(QIODevice::ReadOnly));
        auto lines = requests.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 1); QCOMPARE(QJsonDocument::fromJson(lines[0]).object()["action"].toString(), QString("status"));
        requests.close();
        manager.startSelected({{"save_video", true}, {"flight", false}});
        QTRY_VERIFY(!manager.busy());
        QVERIFY(requests.open(QIODevice::ReadOnly)); lines = requests.readAll().trimmed().split('\n'); requests.close();
        QCOMPARE(lines.size(), 2);
        const auto start = QJsonDocument::fromJson(lines[1]).object();
        QCOMPARE(start["action"].toString(), QString("start")); QVERIFY(!start["options"].toObject()["flight"].toBool());
        manager.readLogs("yolo"); QTRY_VERIFY(!manager.busy()); QCOMPARE(manager.logs(), QString("test-log"));
        manager.disconnectHost(); QVERIFY(!manager.connected());
        QVERIFY(requests.open(QIODevice::ReadOnly)); lines = requests.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 3); // Disconnect never sends stop to the aircraft.
    }
};
QTEST_GUILESS_MAIN(OnboardManagerTest)
#include "tst_onboard_manager.moc"
