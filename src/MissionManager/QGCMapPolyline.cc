/****************************************************************************
 *
 *   (c) 2009-2016 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCMapPolyline.h"
#include "QGCGeo.h"
#include "JsonHelper.h"
#include "QGCQGeoCoordinate.h"
#include "QGCApplication.h"

#include <QGeoRectangle>
#include <QDebug>
#include <QJsonArray>
#include <QLineF>
#include <QFile>
#include <QDomDocument>

const char* QGCMapPolyline::jsonPolylineKey = "polyline";

QGCMapPolyline::QGCMapPolyline(QObject* parent)
    : QObject               (parent)
    , _dirty                (false)
    , _interactive          (false)
{
    _init();
}

QGCMapPolyline::QGCMapPolyline(const QGCMapPolyline& other, QObject* parent)
    : QObject               (parent)
    , _dirty                (false)
    , _interactive          (false)
{
    *this = other;

    _init();
}

const QGCMapPolyline& QGCMapPolyline::operator=(const QGCMapPolyline& other)
{
    clear();

    QVariantList vertices = other.path();
    for (int i=0; i<vertices.count(); i++) {
        appendVertex(vertices[i].value<QGeoCoordinate>());
    }

    setDirty(true);

    return *this;
}

void QGCMapPolyline::_init(void)
{
    connect(&_polylineModel, &QmlObjectListModel::dirtyChanged, this, &QGCMapPolyline::_polylineModelDirtyChanged);
    connect(&_polylineModel, &QmlObjectListModel::countChanged, this, &QGCMapPolyline::_polylineModelCountChanged);
}

void QGCMapPolyline::clear(void)
{
    _polylinePath.clear();
    emit pathChanged();

    _polylineModel.clearAndDeleteContents();

    emit cleared();

    setDirty(true);
}

void QGCMapPolyline::adjustVertex(int vertexIndex, const QGeoCoordinate coordinate)
{
    _polylinePath[vertexIndex] = QVariant::fromValue(coordinate);
    emit pathChanged();
    _polylineModel.value<QGCQGeoCoordinate*>(vertexIndex)->setCoordinate(coordinate);
    setDirty(true);
}

void QGCMapPolyline::setDirty(bool dirty)
{
    if (_dirty != dirty) {
        _dirty = dirty;
        if (!dirty) {
            _polylineModel.setDirty(false);
        }
        emit dirtyChanged(dirty);
    }
}
QGeoCoordinate QGCMapPolyline::_coordFromPointF(const QPointF& point) const
{
    QGeoCoordinate coord;

    if (_polylinePath.count() > 0) {
        QGeoCoordinate tangentOrigin = _polylinePath[0].value<QGeoCoordinate>();
        convertNedToGeo(-point.y(), point.x(), 0, tangentOrigin, &coord);
    }

    return coord;
}

QPointF QGCMapPolyline::_pointFFromCoord(const QGeoCoordinate& coordinate) const
{
    if (_polylinePath.count() > 0) {
        double y, x, down;
        QGeoCoordinate tangentOrigin = _polylinePath[0].value<QGeoCoordinate>();

        convertGeoToNed(coordinate, tangentOrigin, &y, &x, &down);
        return QPointF(x, -y);
    }

    return QPointF();
}

void QGCMapPolyline::setPath(const QList<QGeoCoordinate>& path)
{
    _polylinePath.clear();
    _polylineModel.clearAndDeleteContents();
    foreach (const QGeoCoordinate& coord, path) {
        _polylinePath.append(QVariant::fromValue(coord));
        _polylineModel.append(new QGCQGeoCoordinate(coord, this));
    }

    setDirty(true);
    emit pathChanged();
}

void QGCMapPolyline::setPath(const QVariantList& path)
{
    _polylinePath = path;

    _polylineModel.clearAndDeleteContents();
    for (int i=0; i<_polylinePath.count(); i++) {
        _polylineModel.append(new QGCQGeoCoordinate(_polylinePath[i].value<QGeoCoordinate>(), this));
    }

    setDirty(true);
    emit pathChanged();
}


void QGCMapPolyline::saveToJson(QJsonObject& json)
{
    QJsonValue jsonValue;

    JsonHelper::saveGeoCoordinateArray(_polylinePath, false /* writeAltitude*/, jsonValue);
    json.insert(jsonPolylineKey, jsonValue);
    setDirty(false);
}

bool QGCMapPolyline::loadFromJson(const QJsonObject& json, bool required, QString& errorString)
{
    errorString.clear();
    clear();

    if (required) {
        if (!JsonHelper::validateRequiredKeys(json, QStringList(jsonPolylineKey), errorString)) {
            return false;
        }
    } else if (!json.contains(jsonPolylineKey)) {
        return true;
    }

    if (!JsonHelper::loadGeoCoordinateArray(json[jsonPolylineKey], false /* altitudeRequired */, _polylinePath, errorString)) {
        return false;
    }

    for (int i=0; i<_polylinePath.count(); i++) {
        _polylineModel.append(new QGCQGeoCoordinate(_polylinePath[i].value<QGeoCoordinate>(), this));
    }

    setDirty(false);
    emit pathChanged();

    return true;
}

QList<QGeoCoordinate> QGCMapPolyline::coordinateList(void) const
{
    QList<QGeoCoordinate> coords;

    for (int i=0; i<_polylinePath.count(); i++) {
        coords.append(_polylinePath[i].value<QGeoCoordinate>());
    }

    return coords;
}

void QGCMapPolyline::splitSegment(int vertexIndex)
{
    int nextIndex = vertexIndex + 1;
    if (nextIndex > _polylinePath.length() - 1) {
        return;
    }

    QGeoCoordinate firstVertex = _polylinePath[vertexIndex].value<QGeoCoordinate>();
    QGeoCoordinate nextVertex = _polylinePath[nextIndex].value<QGeoCoordinate>();

    double distance = firstVertex.distanceTo(nextVertex);
    double azimuth = firstVertex.azimuthTo(nextVertex);
    QGeoCoordinate newVertex = firstVertex.atDistanceAndAzimuth(distance / 2, azimuth);

    if (nextIndex == 0) {
        appendVertex(newVertex);
    } else {
        _polylineModel.insert(nextIndex, new QGCQGeoCoordinate(newVertex, this));
        _polylinePath.insert(nextIndex, QVariant::fromValue(newVertex));
        emit pathChanged();
    }
}

void QGCMapPolyline::appendVertex(const QGeoCoordinate& coordinate)
{
    _polylinePath.append(QVariant::fromValue(coordinate));
    _polylineModel.append(new QGCQGeoCoordinate(coordinate, this));
    emit pathChanged();
}

void QGCMapPolyline::removeVertex(int vertexIndex)
{
    if (vertexIndex < 0 || vertexIndex > _polylinePath.length() - 1) {
        qWarning() << "Call to removeVertex with bad vertexIndex:count" << vertexIndex << _polylinePath.length();
        return;
    }

    if (_polylinePath.length() <= 2) {
        // Don't allow the user to trash the polyline
        return;
    }

    QObject* coordObj = _polylineModel.removeAt(vertexIndex);
    coordObj->deleteLater();

    _polylinePath.removeAt(vertexIndex);
    emit pathChanged();
}

void QGCMapPolyline::setInteractive(bool interactive)
{
    if (_interactive != interactive) {
        _interactive = interactive;
        emit interactiveChanged(interactive);
    }
}

QGeoCoordinate QGCMapPolyline::vertexCoordinate(int vertex) const
{
    if (vertex >= 0 && vertex < _polylinePath.count()) {
        return _polylinePath[vertex].value<QGeoCoordinate>();
    } else {
        qWarning() << "QGCMapPolyline::vertexCoordinate bad vertex requested";
        return QGeoCoordinate();
    }
}

QList<QPointF> QGCMapPolyline::nedPolyline(void)
{
    QList<QPointF>  nedPolyline;

    if (count() > 0) {
        QGeoCoordinate  tangentOrigin = vertexCoordinate(0);

        for (int i=0; i<_polylinePath.count(); i++) {
            double y, x, down;
            QGeoCoordinate vertex = vertexCoordinate(i);
            if (i == 0) {
                // This avoids a nan calculation that comes out of convertGeoToNed
                x = y = 0;
            } else {
                convertGeoToNed(vertex, tangentOrigin, &y, &x, &down);
            }
            nedPolyline += QPointF(x, y);
        }
    }

    return nedPolyline;
}


QList<QGeoCoordinate> QGCMapPolyline::offsetPolyline(double distance)
{
    QList<QGeoCoordinate> rgNewPolyline;

    // I'm sure there is some beautiful famous algorithm to do this, but here is a brute force method

    if (count() > 1) {
        // Convert the polygon to NED
        QList<QPointF> rgNedVertices = nedPolyline();

        // Walk the edges, offsetting by the specified distance
        QList<QLineF> rgOffsetEdges;
        for (int i=0; i<rgNedVertices.count() - 1; i++) {
            QLineF  offsetEdge;
            QLineF  originalEdge(rgNedVertices[i], rgNedVertices[i + 1]);

            QLineF workerLine = originalEdge;
            workerLine.setLength(distance);
            workerLine.setAngle(workerLine.angle() - 90.0);
            offsetEdge.setP1(workerLine.p2());

            workerLine.setPoints(originalEdge.p2(), originalEdge.p1());
            workerLine.setLength(distance);
            workerLine.setAngle(workerLine.angle() + 90.0);
            offsetEdge.setP2(workerLine.p2());

            rgOffsetEdges.append(offsetEdge);
        }

        QGeoCoordinate  tangentOrigin = vertexCoordinate(0);

        // Add first vertex
        QGeoCoordinate coord;
        convertNedToGeo(rgOffsetEdges[0].p1().y(), rgOffsetEdges[0].p1().x(), 0, tangentOrigin, &coord);
        rgNewPolyline.append(coord);

        // Intersect the offset edges to generate new central vertices
        QPointF  newVertex;
        for (int i=1; i<rgOffsetEdges.count(); i++) {
            if (rgOffsetEdges[i - 1].intersect(rgOffsetEdges[i], &newVertex) == QLineF::NoIntersection) {
                // Two lines are colinear
                newVertex = rgOffsetEdges[i].p2();
            }
            convertNedToGeo(newVertex.y(), newVertex.x(), 0, tangentOrigin, &coord);
            rgNewPolyline.append(coord);
        }

        // Add last vertex
        int lastIndex = rgOffsetEdges.count() - 1;
        convertNedToGeo(rgOffsetEdges[lastIndex].p2().y(), rgOffsetEdges[lastIndex].p2().x(), 0, tangentOrigin, &coord);
        rgNewPolyline.append(coord);
    }

    return rgNewPolyline;
}

bool QGCMapPolyline::loadKMLFile(const QString& kmlFile)
{
    QFile file(kmlFile);

    if (!file.exists()) {
        qgcApp()->showMessage(tr("File not found: %1").arg(kmlFile));
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qgcApp()->showMessage(tr("Unable to open file: %1 error: $%2").arg(kmlFile).arg(file.errorString()));
        return false;
    }

    QDomDocument doc;
    QString errorMessage;
    int errorLine;
    if (!doc.setContent(&file, &errorMessage, &errorLine)) {
        qgcApp()->showMessage(tr("Unable to parse KML file: %1 error: %2 line: %3").arg(kmlFile).arg(errorMessage).arg(errorLine));
        return false;
    }

    QDomNodeList rgNodes = doc.elementsByTagName("Polygon");
    if (rgNodes.count() == 0) {
        qgcApp()->showMessage(tr("Unable to find Polygon node in KML"));
        return false;
    }

    QDomNode coordinatesNode = rgNodes.item(0).namedItem("outerBoundaryIs").namedItem("LinearRing").namedItem("coordinates");
    if (coordinatesNode.isNull()) {
        qgcApp()->showMessage(tr("Internal error: Unable to find coordinates node in KML"));
        return false;
    }

    QString coordinatesString = coordinatesNode.toElement().text().simplified();
    QStringList rgCoordinateStrings = coordinatesString.split(" ");

    QList<QGeoCoordinate> rgCoords;
    for (int i=0; i<rgCoordinateStrings.count()-1; i++) {
        QString coordinateString = rgCoordinateStrings[i];

        QStringList rgValueStrings = coordinateString.split(",");

        QGeoCoordinate coord;
        coord.setLongitude(rgValueStrings[0].toDouble());
        coord.setLatitude(rgValueStrings[1].toDouble());

        rgCoords.append(coord);
    }

    // Determine winding, reverse if needed
    double sum = 0;
    for (int i=0; i<rgCoords.count(); i++) {
        QGeoCoordinate coord1 = rgCoords[i];
        QGeoCoordinate coord2 = (i == rgCoords.count() - 1) ? rgCoords[0] : rgCoords[i+1];

        sum += (coord2.longitude() - coord1.longitude()) * (coord2.latitude() + coord1.latitude());
    }
    bool reverse = sum < 0.0;

    if (reverse) {
        QList<QGeoCoordinate> rgReversed;

        for (int i=0; i<rgCoords.count(); i++) {
            rgReversed.prepend(rgCoords[i]);
        }
        rgCoords = rgReversed;
    }

    clear();
    for (int i=0; i<rgCoords.count(); i++) {
        appendVertex(rgCoords[i]);
    }

    return true;
}

void QGCMapPolyline::_polylineModelDirtyChanged(bool dirty)
{
    if (dirty) {
        setDirty(true);
    }
}

void QGCMapPolyline::_polylineModelCountChanged(int count)
{
    emit countChanged(count);
}


double QGCMapPolyline::length(void) const
{
    double length = 0;

    for (int i=0; i<_polylinePath.count() - 1; i++) {
        QGeoCoordinate from = _polylinePath[i].value<QGeoCoordinate>();
        QGeoCoordinate to = _polylinePath[i+1].value<QGeoCoordinate>();
        length += from.distanceTo(to);
    }

    return length;
}
