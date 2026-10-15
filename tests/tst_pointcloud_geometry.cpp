#include "PointCloudGeometry.h"
#include <QtTest>
#include <limits>

class PointCloudGeometryTest : public QObject {
    Q_OBJECT
private slots:
    void emptyAndNonfinite();
    void smallCloudKeepsExactExtent();
    void rejectsIncompleteNonfinitePoints();
    void trimsOutlierReturns();
    void handlesFlatAndRepeatedCoordinates();
    void clampsTrimFraction();
};

void PointCloudGeometryTest::emptyAndNonfinite()
{
    QVERIFY(!cloudBounds({}).valid);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    QVERIFY(!cloudBounds({{nan, 0, 0, 0}, {0, inf, 0, 0}, {0, 0, -inf, 0}}).valid);
}

void PointCloudGeometryTest::smallCloudKeepsExactExtent()
{
    const auto single = cloudBounds({{2, -3, 4, 0}});
    QVERIFY(single.valid);
    QCOMPARE(single.low, QVector3D(2, -3, 4));
    QCOMPARE(single.high, single.low);

    QVector<MapPoint> points;
    for (int index = 0; index < 999; ++index) points.append({float(index), -float(index), 2, 0});
    const auto bounds = cloudBounds(points);
    QCOMPARE(bounds.low, QVector3D(0, -998, 2));
    QCOMPARE(bounds.high, QVector3D(998, 0, 2));
}

void PointCloudGeometryTest::rejectsIncompleteNonfinitePoints()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto bounds = cloudBounds({{1, 2, 3, nan}, {-100, inf, -100, 0},
                                     {nan, 100, 100, 0}, {4, 5, 6, 0}});
    QVERIFY(bounds.valid);
    QCOMPARE(bounds.low, QVector3D(1, 2, 3));
    QCOMPARE(bounds.high, QVector3D(4, 5, 6));
}

void PointCloudGeometryTest::trimsOutlierReturns()
{
    QVector<MapPoint> points;
    // A room-sized cloud with distant outliers on both sides of every axis.
    for (int index = 0; index < 980; ++index) {
        const float coordinate = float(index) / 979;
        points.append({coordinate * 8, coordinate * 6, coordinate * 3, 0});
    }
    for (int index = 0; index < 10; ++index) {
        points.append({-10000, -10000, -10000, 0});
        points.append({10000, 10000, 10000, 0});
    }
    const auto bounds = cloudBounds(points);
    QVERIFY(bounds.valid);
    QCOMPARE(bounds.low, QVector3D(0, 0, 0));
    QCOMPARE(bounds.high, QVector3D(8, 6, 3));
    // Bounds calculation must not reorder the published immutable cloud.
    QCOMPARE(points[0].x, 0.0f);
    QCOMPARE(points[979].x, 8.0f);
    QCOMPARE(points[980].x, -10000.0f);
}

void PointCloudGeometryTest::handlesFlatAndRepeatedCoordinates()
{
    QVector<MapPoint> points(1200, MapPoint{1, 2, 0, 0});
    const auto bounds = cloudBounds(points);
    QVERIFY(bounds.valid);
    QCOMPARE(bounds.low, QVector3D(1, 2, 0));
    QCOMPARE(bounds.high, bounds.low);
}

void PointCloudGeometryTest::clampsTrimFraction()
{
    QVector<MapPoint> points;
    for (int index = 0; index < 1000; ++index) points.append({float(index), float(index), float(index), 0});
    const auto untrimmed = cloudBounds(points, -1);
    QCOMPARE(untrimmed.low.x(), 0.0f);
    QCOMPARE(untrimmed.high.x(), 999.0f);
    const auto central = cloudBounds(points, 5);
    QCOMPARE(central.low.x(), 490.0f);
    QCOMPARE(central.high.x(), 509.0f);
    const auto defaultTrim = cloudBounds(points, std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(defaultTrim.low.x(), 10.0f);
    QCOMPARE(defaultTrim.high.x(), 989.0f);
}

QTEST_APPLESS_MAIN(PointCloudGeometryTest)
#include "tst_pointcloud_geometry.moc"
