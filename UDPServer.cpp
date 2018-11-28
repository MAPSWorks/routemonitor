#include "udpserver.h"
#include "GPSPosEvent.h"
#include "osgEarth/SpatialReference"
#include "osgEarthUtil/EarthManipulator"
#include <osg/LineStipple>
#include <osg/LineWidth>

using namespace osgEarth;
using namespace osgEarth::Symbology;
using namespace osgEarth::Util;

extern osg::Node* g_earthNode;

double g_dPlanePosLon = 0.0;
double g_dPlanePosLat = 0.0;
double g_dPlanePosAngle = 0.0;

double g_dTargetPosLon = 0.0;
double g_dTargetPosLat = 0.0;

bool g_bPlaneMove = true;

UDPServer::UDPServer(osg::Geode* pGeode, int nPort
	, osgViewer::ViewerBase* pViewer, osgEarth::Annotation::MyPlaceNode* pLocalGeometryNode, QObject *parent)
	: QObject(parent), m_dLon(dLon), m_dLat(dLat), m_dAngle(dAngle)
{
	m_pViewer = pViewer;
	m_pPlaneNode = pLocalGeometryNode;
	m_nPort = nPort;

	m_pGeodePath = pGeode;

	receiver = new QUdpSocket(this);
	receiver->bind(QHostAddress::LocalHost, nPort);
	connect(receiver, SIGNAL(readyRead()), this, SLOT(readPendingDatagrams()));
}

UDPServer::~UDPServer()
{

}

void UDPServer::readPendingDatagrams()
{
	osg::ref_ptr<osgEarth::MapNode> mapNode = osgEarth::MapNode::findMapNode(g_earthNode);
	SpatialReference* pwgs84 = osgEarth::SpatialReference::get("wgs84");
	const SpatialReference* mapSRS = mapNode->getMapSRS();

	while (receiver->hasPendingDatagrams()) {
		QByteArray datagram;
		datagram.resize(receiver->pendingDatagramSize());
		receiver->readDatagram(datagram.data(), datagram.size());
		//数据接收在datagram里
		/* readDatagram 函数原型
		qint64 readDatagram(char *data,qint64 maxSize,QHostAddress *address=0,quint16 *port=0)
		*/

		int nSize = datagram.size();
		double dLon, dLat, dAngle;
		memcpy(&dLon, datagram.data() + 1, 8);
		memcpy(&dLat, datagram.data() + 9, 8);
		memcpy(&dAngle, datagram.data() + 17, 8);

		if (m_verticesPlanePath == nullptr)
		{
			m_verticesPlanePath = new osg::Vec3dArray();
		}

		if (m_verticesPlanePath->size() > 10000)
		{
			m_verticesPlanePath->clear();
		}

		osg::Vec3d startline(dLon, dLat, 10000.0);
		osg::Vec3d startWorld;
		pwgs84->transform(startline, mapSRS, startWorld);

		m_verticesPlanePath->push_back(startWorld);

		if (!g_bPlaneMove)
			return;

		if (m_nPort == 6665)
		{
			osgViewer::Viewer* pViewer = dynamic_cast<osgViewer::Viewer*>(m_pViewer);
			osgGA::CameraManipulator* pCameraManipulator = pViewer->getCameraManipulator();
			osgEarth::Util::EarthManipulator* pEarthManipulator = dynamic_cast<osgEarth::Util::EarthManipulator*>(pCameraManipulator);
			osgEarth::Viewpoint viewPoint = pEarthManipulator->getViewpoint();
			osgEarth::GeoPoint geoPoint = viewPoint.focalPoint().get();
			geoPoint.x() = -80.0/*dLon*/;
			geoPoint.y() = -179.0/*dLat*/;
			viewPoint.focalPoint() = geoPoint;

			double dHeading = viewPoint.getHeading();
			double dPitch = viewPoint.getPitch();
			double dRange = viewPoint.getRange();

			//pEarthManipulator->setViewpoint(viewPoint);
			pEarthManipulator->setViewpoint(osgEarth::Viewpoint("New Tork", dLon, dLat, geoPoint.z(), dHeading, dPitch, dRange));

			m_pPlaneNode->RotateHeading(dAngle);
		}

		m_pPlaneNode->setPosition(osgEarth::GeoPoint(pwgs84, osg::Vec3d(dLon, dLat, 2000.0)));

		int nCount = m_pGeodePath->getNumDrawables();
		if (nCount > 0)
		{
			m_pGeodePath->removeDrawables(0);
		}

		osg::ref_ptr<osg::Geometry> linesGeom = new osg::Geometry();
		// pass the created vertex array to the points geometry object.
		linesGeom->setVertexArray(m_verticesPlanePath);

		// set the colors as before, plus using the above
		osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
		colors->push_back(osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
		linesGeom->setColorArray(colors);
		linesGeom->setColorBinding(osg::Geometry::BIND_OVERALL);

		// set the normal in the same way color.
		osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
		normals->push_back(osg::Vec3(0.0f, -1.0f, 0.0f));
		linesGeom->setNormalArray(normals);
		linesGeom->setNormalBinding(osg::Geometry::BIND_OVERALL);

		// This time we simply use primitive, and hardwire the number of coords to use 
		// since we know up front,
		linesGeom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, m_verticesPlanePath->size()));
		linesGeom->getOrCreateStateSet()->setMode(GL_LINE_STIPPLE, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);

 		//linesGeom->getOrCreateStateSet()->setAttribute(new osg::LineStipple(2, 0x00FF));
 		linesGeom->getOrCreateStateSet()->setAttribute(new osg::LineWidth(4.0));

		m_pGeodePath->addDrawable(linesGeom);
	}
}