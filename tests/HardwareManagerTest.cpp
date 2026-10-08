#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include "../HardwareManager/HardwareManager.h"
#include "../DefaultLightGunSettings.h"
#include "../Global.h"

class HardwareManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void exactSerialMatch();
    void strongPhysicalPortMatch();
    void weakModelMatch();
    void unrelatedDeviceDoesNotMatch();
    void identityRoundTrips();
    void serialAndHidSharePhysicalIdentity();
    void hidWithoutSerialMatchesSerialByUsbParent();
    void legacyHidPathDerivesUsbParent();
    void endpointRegistryRoundTrips();
    void eightPlayerAssignmentsRoundTrip();
    void manualAssignmentReplacesOccupiedSlot();
    void builtInLightGunProfilesInitialize();
};

static DeviceFingerprint rs3(const QString &serial = QString())
{
    DeviceFingerprint fingerprint;
    fingerprint.transport = "serial";
    fingerprint.vid = "3a";
    fingerprint.pid = "5750";
    fingerprint.serialNumber = serial;
    fingerprint.stableByPath = "/dev/serial/by-path/usb-1";
    return fingerprint;
}

void HardwareManagerTest::exactSerialMatch()
{
    const DeviceFingerprint current = rs3("ABC123");
    const DeviceIdentity saved = current.identity();
    QCOMPARE(HardwareManager::matchConfidence(saved, current), 100);
}

void HardwareManagerTest::strongPhysicalPortMatch()
{
    DeviceIdentity saved;
    saved.type = "serial";
    saved.vid = "3a";
    saved.pid = "5750";
    saved.stableByPath = "/dev/serial/by-path/usb-1";
    QCOMPARE(HardwareManager::matchConfidence(saved, rs3()), 80);
}

void HardwareManagerTest::weakModelMatch()
{
    DeviceIdentity saved;
    saved.type = "serial";
    saved.vid = "3a";
    saved.pid = "5750";
    QCOMPARE(HardwareManager::matchConfidence(saved, rs3()), 40);
}

void HardwareManagerTest::unrelatedDeviceDoesNotMatch()
{
    DeviceIdentity saved;
    saved.type = "serial";
    saved.vid = "3a";
    saved.pid = "5750";
    DeviceFingerprint other = rs3();
    other.pid = "1234";
    QCOMPARE(HardwareManager::matchConfidence(saved, other), 0);
}

void HardwareManagerTest::identityRoundTrips()
{
    DeviceIdentity original;
    original.type = "serial";
    original.value = "3a:5750:ABC123";
    original.vid = "3a";
    original.pid = "5750";
    original.serial = "ABC123";
    original.stableByIdPath = "/dev/serial/by-id/gun";
    original.stableByPath = "/dev/serial/by-path/usb-1";
    const DeviceIdentity restored = DeviceIdentity::fromJson(original.toJson());
    QCOMPARE(restored.key(), original.key());
    QCOMPARE(restored.stableByIdPath, original.stableByIdPath);
    QCOMPARE(restored.stableByPath, original.stableByPath);
}

void HardwareManagerTest::serialAndHidSharePhysicalIdentity()
{
    const DeviceFingerprint serial = rs3("ABC123");
    DeviceFingerprint hid = serial;
    hid.transport = "hid";
    hid.path = "hidraw-test";
    hid.interfaceNumber = 1;

    QCOMPARE(serial.physicalKey(), hid.physicalKey());
    QCOMPARE(HardwareManager::matchConfidence(serial.identity(), hid), 100);
}

void HardwareManagerTest::hidWithoutSerialMatchesSerialByUsbParent()
{
    DeviceFingerprint serial = rs3("ABC123");
    serial.usbParentPath = "1-3";
    DeviceFingerprint hid = serial;
    hid.transport = "hid";
    hid.path = "1-3:1.0";
    hid.serialNumber.clear();

    QCOMPARE(hid.physicalKey(), QString("usb-port:3a:5750:1-3"));
    QCOMPARE(HardwareManager::matchConfidence(serial.identity(), hid), 95);
}

void HardwareManagerTest::legacyHidPathDerivesUsbParent()
{
    const DeviceFingerprint restored = DeviceFingerprint::fromJson(QJsonObject{
        {"transport", "hid"}, {"vid", "483"}, {"pid", "5750"},
        {"path", "1-3:1.0"}, {"interface", 0}});
    QCOMPARE(restored.usbParentPath, QString("1-3"));
}

void HardwareManagerTest::endpointRegistryRoundTrips()
{
    HardwareDevice original;
    original.identity = rs3("ABC123").identity();
    original.fingerprint = rs3("ABC123");
    original.endpoints.append(original.fingerprint);

    DeviceFingerprint hid = original.fingerprint;
    hid.transport = "hid";
    hid.path = "hidraw-test";
    hid.interfaceNumber = 1;
    original.endpoints.append(hid);

    const HardwareDevice restored = HardwareDevice::fromJson(original.toJson());
    QCOMPARE(restored.endpoints.size(), 2);
    QCOMPARE(restored.endpoints.at(0).transport, QString("serial"));
    QCOMPARE(restored.endpoints.at(1).transport, QString("hid"));
    QCOMPARE(restored.fingerprint.transport, QString("serial"));
}

void HardwareManagerTest::eightPlayerAssignmentsRoundTrip()
{
    HardwareDevice original;
    original.identity = rs3("P8").identity();
    original.fingerprint = rs3("P8");
    original.player = 8;
    original.assignmentMode = HardwareAssignmentMode::Manual;
    original.manual = true;

    const HardwareDevice restored = HardwareDevice::fromJson(original.toJson());
    QCOMPARE(restored.player, quint8(8));
    QCOMPARE(restored.assignmentMode, HardwareAssignmentMode::Manual);
    QCOMPARE(restored.manual, true);
}

void HardwareManagerTest::manualAssignmentReplacesOccupiedSlot()
{
    QTemporaryDir dataDir;
    QVERIFY(dataDir.isValid());
    qputenv("HOTR_DATA_DIR", dataDir.path().toUtf8());

    HardwareDevice first;
    first.identity = rs3("FIRST").identity();
    first.fingerprint = rs3("FIRST");
    first.deviceType = "rs3reaper";
    first.profile = "rs3Reaper";

    HardwareDevice second;
    second.identity = rs3("SECOND").identity();
    second.fingerprint = rs3("SECOND");
    second.deviceType = "rs3reaper";
    second.profile = "rs3Reaper";

    QFile registry(dataDir.filePath("devices.json"));
    QVERIFY(registry.open(QIODevice::WriteOnly));
    registry.write(QJsonDocument(QJsonObject{
        {"version", 1}, {"devices", QJsonArray{first.toJson(), second.toJson()}}
    }).toJson());
    registry.close();

    HardwareManager manager;
    QVERIFY(manager.assignPlayer(first.identity.key(), 1, HardwareAssignmentMode::Manual));
    QVERIFY(manager.assignPlayer(second.identity.key(), 1, HardwareAssignmentMode::Manual));
    QVERIFY(manager.assignPlayer(first.identity.key(), 8, HardwareAssignmentMode::Manual));

    const QList<HardwareDevice> devices = manager.devices();
    QCOMPARE(devices.size(), 2);
    for (const HardwareDevice &device : devices)
    {
        if (device.identity.key() == first.identity.key())
        {
            QCOMPARE(device.player, quint8(8));
            QCOMPARE(device.assignmentMode, HardwareAssignmentMode::Manual);
            QCOMPARE(device.manual, true);
        }
        else if (device.identity.key() == second.identity.key())
        {
            QCOMPARE(device.player, quint8(1));
        }
    }

    HardwareManager reloaded;
    QCOMPARE(reloaded.devices().size(), 2);
    for (const HardwareDevice &device : reloaded.devices())
    {
        if (device.identity.key() == first.identity.key())
        {
            QCOMPARE(device.player, quint8(8));
            QCOMPARE(device.assignmentMode, HardwareAssignmentMode::Manual);
        }
    }

    qunsetenv("HOTR_DATA_DIR");
}

void HardwareManagerTest::builtInLightGunProfilesInitialize()
{
    // Calling the initializer repeatedly must be safe because GUI and service
    // startup can both use the same shared profile setup code.
    initializeDefaultLightGunSettings();
    initializeDefaultLightGunSettings();

    QCOMPARE(DEFAULTLG_ARRAY[RS3_REAPER].MAXAMMON,
             quint16(REAPERMAXAMMONUM));
    QCOMPARE(DEFAULTLG_ARRAY[RS3_REAPER].RELOADVALUEN,
             quint16(REAPERRELOADNUM));
    QCOMPARE(DEFAULTLG_ARRAY[OPENFIRE].BAUD,
             quint8(OPENFIREBAUD));
    QCOMPARE(DEFAULTLG_ARRAY[OPENFIRE].MAXAMMO,
             QString::fromLatin1(OPENFIREMAXAMMO));
    QCOMPARE(DEFAULTLG_ARRAY[CUSTOMUSB].RELOADVALUE,
             QString::fromLatin1(CUSTOMUSBRELOAD));
}

QTEST_MAIN(HardwareManagerTest)
#include "HardwareManagerTest.moc"
