#ifndef UDPSERVER_H
#define UDPSERVER_H

#include <QObject>
#include <QtNetwork/QtNetwork>
#include "osgViewer/Viewer"
#include <osgEarthAnnotation/LocalGeometryNode>
#include <osgEarthAnnotation/PlaceNode>
#include "MyPlaceNode.h"

class UDPServer : public QObject
{
	Q_OBJECT

public:
	UDPServer(osg::Geode* pGeode, int nPort
		, osgViewer::ViewerBase*, osgEarth::Annotation::MyPlaceNode*, QObject *parent = nullptr);
	~UDPServer();

	QUdpSocket *receiver;

	osgViewer::ViewerBase* m_pViewer;

	osgEarth::Annotation::MyPlaceNode* m_pPlaneNode;

	osg::ref_ptr<osg::Vec3dArray> m_verticesPlanePath;

	int m_nPort;

	osg::Geode* m_pGeodePath;

	//ÐÅºÅ²Û
private slots:
	void readPendingDatagrams();

signals:

	void sigPosChanged(double dLon, double dLat);

private:

};

#endif // UDPSERVER_H
