#include <QTest>
#include "VolumeManager.h"

class TestVolumeManager : public QObject {
    Q_OBJECT

private slots:
    void testRootVolume();
    void testMountedVolumes();
    void testIsMountPoint();
    void testIsUsbDevice();
    void testMountDeviceEmpty();
};

void TestVolumeManager::testRootVolume()
{
    VolumeEntry root = VolumeManager::rootVolume();
    QCOMPARE(root.path, QStringLiteral("/"));
    QCOMPARE(root.mountPoint, QStringLiteral("/"));
    QVERIFY(root.isRoot);
    QVERIFY(root.isMounted);
    QVERIFY(root.totalBytes > 0);
}

void TestVolumeManager::testMountedVolumes()
{
    QVector<VolumeEntry> volumes = VolumeManager::mountedVolumes();
    QVERIFY(!volumes.isEmpty());

    bool foundRoot = false;
    for (const auto& vol : volumes) {
        if (vol.path == QStringLiteral("/")) {
            foundRoot = true;
            QVERIFY(vol.isRoot);
            QVERIFY(vol.isMounted);
        }
        QVERIFY(!vol.name.isEmpty());
        QVERIFY(!vol.path.isEmpty());
    }
    QVERIFY(foundRoot);
}

void TestVolumeManager::testIsMountPoint()
{
    QVERIFY(VolumeManager::isMountPoint(QStringLiteral("/")));
    QVERIFY(!VolumeManager::isMountPoint(QStringLiteral("/nonexistent_path_12345")));
}

void TestVolumeManager::testIsUsbDevice()
{
    QVERIFY(!VolumeManager::isUsbDevice(QString()));
    QVERIFY(VolumeManager::isUsbDevice(QStringLiteral("mtp://[usb:001,002]")));
    QVERIFY(VolumeManager::isUsbDevice(QStringLiteral("/run/user/1000/gvfs/mtp:host=foo")));
}

void TestVolumeManager::testMountDeviceEmpty()
{
    QString err;
    QString result = VolumeManager::mountDevice(QString(), &err);
    QVERIFY(result.isEmpty());
    QVERIFY(!err.isEmpty());
}

QTEST_MAIN(TestVolumeManager)
#include "test_volumemanager.moc"
