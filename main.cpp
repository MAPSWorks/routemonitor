/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2015 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osg/Notify>
#include <osg/Version>
#include <osgEarth/ImageUtils>
#include <osgEarth/MapNode>
#include <osgEarthAnnotation/AnnotationData>
#include <osgEarthAnnotation/AnnotationNode>
#include <osgEarthAnnotation/PlaceNode>
#include <osgEarthAnnotation/ScaleDecoration>
#include <osgEarthAnnotation/TrackNode>
#include <osgEarthQt/ViewerWidget>
#include <osgEarthQt/LayerManagerWidget>
#include <osgEarthQt/MapCatalogWidget>
#include <osgEarthQt/DataManager>
#include <osgEarthQt/AnnotationListWidget>
#include <osgEarthQt/LOSControlWidget>
#include <osgEarthQt/TerrainProfileWidget>
#include <osgEarthUtil/AnnotationEvents>
#include <osgEarthUtil/AutoClipPlaneHandler>
#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/Sky>
#include <osgEarthUtil/Ocean>
#include <osgEarthAnnotation/LocalGeometryNode>

#include <osgEarthDrivers/bing/bingoptions>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QToolBar>
#include <QApplication>
#include "MainWindow.h"
#include "UDPServer.h"
#include <osg/LineWidth>

#include <osg/PointSprite>
#include <osg/BlendFunc>
#include <osg/StateAttribute>
#include <osg/Point>
#include <osg/Geometry>
#include <osg/Texture2D>
#include <osg/TexEnv>
#include <osg/GLExtensions>
#include <osg/TexEnv>

#include <osgDB/ReadFile>
#include <osgEarthDrivers/gdal/GDALOptions>
#include <osgEarthDrivers/feature_ogr/OGRFeatureOptions>
#include <osgEarthDrivers/model_feature_geom/FeatureGeomModelOptions>
#include "MyPlaceNode.h"
#include "MyManipulator.h"

#ifdef Q_WS_X11
#include <X11/Xlib.h>
#endif

using namespace osgEarth;
using namespace osgEarth::Features;
using namespace osgEarth::Drivers;
using namespace osgEarth::Symbology;
using namespace osgEarth::Util;

#define TRACK_ICON_URL    "../data/m2525_air.png"
#define TRACK_ICON_SIZE   24
#define TRACK_FIELD_NAME  "name"

static osg::ref_ptr<osg::Group> s_annoGroup;
static osgEarth::Util::SkyNode* s_sky = 0L;
static osgEarth::Util::OceanNode* s_ocean = 0L;

extern double g_dPlanePosLon;
extern double g_dPlanePosLat;
extern double g_dPlanePosAngle;

extern double g_dTargetPosLon;
extern double g_dTargetPosLat;


extern osg::ref_ptr<osg::Geode> g_geode;
extern osg::ref_ptr<osg::Geode> g_geodeTarget;

osg::Camera* g_hudCamera = nullptr;
osgEarth::MapNode* g_MapNode = nullptr;
osg::Geometry* g_GeoScaleLine = nullptr;

//------------------------------------------------------------------

int
usage(const std::string& msg)
{
	OE_NOTICE << msg << std::endl;
	OE_NOTICE << std::endl;
	OE_NOTICE << "USAGE: osgearth_qt [options] file.earth" << std::endl;
	OE_NOTICE << "   --multi n               : use a multi-pane viewer with n initial views" << std::endl;
	OE_NOTICE << "   --stylesheet filename   : optional Qt stylesheet" << std::endl;
	OE_NOTICE << "   --run-on-demand         : use the OSG ON_DEMAND frame scheme" << std::endl;
	OE_NOTICE << "   --tracks                : create some moving track data" << std::endl;

	return -1;
}

osg::StateSet* makeStateSet(float size)
{
	osg::StateSet *set = new osg::StateSet();

	/// Setup cool blending
	set->setMode(GL_BLEND, osg::StateAttribute::ON);
	osg::BlendFunc *fn = new osg::BlendFunc();
	fn->setFunction(osg::BlendFunc::SRC_ALPHA, osg::BlendFunc::DST_ALPHA);
	set->setAttributeAndModes(fn, osg::StateAttribute::ON);

	/// Setup the point sprites
	osg::PointSprite *sprite = new osg::PointSprite();
	set->setTextureAttributeAndModes(0, sprite, osg::StateAttribute::ON);

	/// Give some size to the points to be able to see the sprite
	osg::Point *point = new osg::Point();
	point->setSize(size);
	set->setAttribute(point);

	/// Disable depth test to avoid sort problems and Lighting
	set->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	set->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

	/// The texture for the sprites
	osg::Texture2D *tex = new osg::Texture2D();
	tex->setImage(osgDB::readImageFile("C:/OSG_OSGEarth/OpenSceneGraph-Data/Images/particle.rgb"));
	set->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);

	return set;
}

//------------------------------------------------------------------

osgEarth::Annotation::LocalGeometryNode* CreatePlaneTag(MapNode* pMapNode)
{
 	osg::ref_ptr<osg::Vec4Array> shared_colors = new osg::Vec4Array;
 	shared_colors->push_back(osg::Vec4(0.5, 0.5, 0.5, 1.0));

	osg::ref_ptr<osg::Vec3Array> shared_normals = new osg::Vec3Array;
	shared_normals->push_back(osg::Vec3(0.0f, -1.0f, 0.0f));

	osg::Geometry* polyGeom = new osg::Geometry();
	osg::Vec3Array* vertices = new osg::Vec3Array;
 	vertices->push_back(osg::Vec3d(-10000.0, 0.0, 0.0));
 	vertices->push_back(osg::Vec3d(10000.0, 0.0, 0.0));
 	vertices->push_back(osg::Vec3d(0.0, 10000.0, 0.0));
 	vertices->push_back(osg::Vec3d(0.0, -10000.0, 0.0));

//	vertices->push_back(osg::Vec3d(0.0, 0.0, 0.0));

	polyGeom->setVertexArray(vertices);
 	polyGeom->setColorArray(shared_colors.get(), osg::Array::BIND_OVERALL);
 	polyGeom->setNormalArray(shared_normals.get(), osg::Array::BIND_OVERALL);
	polyGeom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES/*POINTS*/, 0, 4));

	osg::Geode* geode = new osg::Geode;
	geode->addDrawable(polyGeom);

	//geode->setStateSet(makeStateSet(100.0f));

	osgEarth::Annotation::LocalGeometryNode* pLocalGeoNode = new osgEarth::Annotation::LocalGeometryNode(pMapNode, geode);
	pLocalGeoNode->setPosition(osgEarth::GeoPoint(osgEarth::SpatialReference::get("wgs84"), osg::Vec3d(0.0, 0.0, 2000.0)));
	return pLocalGeoNode;
}

osg::ref_ptr<osg::Geode> g_geode = nullptr;
osg::ref_ptr<osg::Geode> g_geodeTarget = nullptr;

osg::Group* g_root = nullptr;
osg::Node* g_earthNode = nullptr;
osgViewer::Viewer* g_viewerMain = nullptr;
osgText::Text* g_pText = nullptr;

double g_dOriginHeight = 5000000.0;

void LoadPosFromFile()
{
	QString strIniFile = QApplication::applicationFilePath();
	strIniFile = QFileInfo(strIniFile).absolutePath();
	strIniFile += "/data/pos.ini";

	QSettings settings(strIniFile, QSettings::IniFormat);
	g_dPlanePosLon = settings.value("lon").toDouble();
	g_dPlanePosLat = settings.value("lat").toDouble();
	g_dPlanePosAngle = settings.value("angle").toDouble();

	g_dTargetPosLon = settings.value("targetlon").toDouble();
	g_dTargetPosLat = settings.value("targetlat").toDouble();

	g_dOriginHeight = settings.value("height", 500000.0).toDouble();
}

void SavePosFromFile()
{
	QString strIniFile = QApplication::applicationFilePath();
	strIniFile = QFileInfo(strIniFile).absolutePath();
	strIniFile += "/data/pos.ini";

	QSettings settings(strIniFile, QSettings::IniFormat);
	settings.setValue("lon", g_dPlanePosLon);
	settings.setValue("lat", g_dPlanePosLat);
	settings.setValue("angle", g_dPlanePosAngle);

	settings.setValue("targetlon", g_dTargetPosLon);
	settings.setValue("targetlat", g_dTargetPosLat);
	settings.setValue("height", g_dOriginHeight);
}

osg::Node* createScaleBarHUD(osgText::Text* updateText)
{
	// create the hud. derived from osgHud.cpp
	// adds a set of quads, each in a separate Geode - which can be picked individually
	// eg to be used as a menuing/help system!
	// Can pick texts too!

	osg::Camera* hudCamera = new osg::Camera;
	g_hudCamera = hudCamera;
	hudCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	hudCamera->setProjectionMatrixAsOrtho2D(0, 1024, 0, 1024);
	hudCamera->setViewport(0, 0, 1024, 1024);
	hudCamera->setViewMatrix(osg::Matrix::identity());
	hudCamera->setRenderOrder(osg::Camera::POST_RENDER);
	hudCamera->setClearMask(GL_DEPTH_BUFFER_BIT);

	std::string timesFont("fonts/times.ttf");

	double dx = 100.0;
	double dy = 50.0;

	double dHeight = 15.0;
	double dWidth = 100.0;

	//绘制比例尺的线
	{
		osg::ref_ptr<osg::Geometry> linesGeom = new osg::Geometry();
		g_GeoScaleLine = linesGeom.get();
		// pass the created vertex array to the points geometry object.

		osg::Vec3dArray* vertices = new osg::Vec3dArray();

		vertices->push_back(osg::Vec3d(dx, dy + dHeight, 0.0));
		vertices->push_back(osg::Vec3d(dx, dy, 0.0));
		vertices->push_back(osg::Vec3d(dx + dWidth, dy, 0.0));
		vertices->push_back(osg::Vec3d(dx + dWidth, dy + dHeight, 0.0));
		vertices->push_back(osg::Vec3d(dx + dWidth, dy, 0.0));
		vertices->push_back(osg::Vec3d(dx + dWidth * 2.0, dy, 0.0));
		vertices->push_back(osg::Vec3d(dx + dWidth * 2.0, dy + dHeight, 0.0));

		linesGeom->setVertexArray(vertices);
		linesGeom->setDataVariance(osg::Object::DYNAMIC);

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

		int nn = vertices->size();

		// This time we simply use primitive, and hardwire the number of coords to use 
		// since we know up front,
		linesGeom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, vertices->size()));
		linesGeom->getOrCreateStateSet()->setMode(GL_LINE_STIPPLE, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);

		//linesGeom->getOrCreateStateSet()->setAttribute(new osg::LineStipple(2, 0x00FF));
		linesGeom->getOrCreateStateSet()->setAttribute(new osg::LineWidth(2.0));

		osg::Geode* geode = new osg::Geode();
		osg::StateSet* stateset = geode->getOrCreateStateSet();
		stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
		stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
		geode->setName("simple");
		geode->addDrawable(linesGeom);

		hudCamera->addChild(geode);
	}

	{
		osg::Geode* geode = new osg::Geode();
		osg::StateSet* stateset = geode->getOrCreateStateSet();
		stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
		stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
		geode->setName("simple");

		osgText::Text* text = new  osgText::Text;
		geode->addDrawable(text);

		text->setCharacterSize(40.0f);
		text->setFont(timesFont);
		text->setColor(osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
		text->setText("0");
		text->setPosition(osg::Vec3d(dx - 8, dy + dHeight + 8, 0.0));

		hudCamera->addChild(geode);
	}

	{ // this displays what has been selected
		osg::Geode* geode = new osg::Geode();
		osg::StateSet* stateset = geode->getOrCreateStateSet();
		stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
		stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
		geode->setName("The text label");
		geode->addDrawable(updateText);
		hudCamera->addChild(geode);

		updateText->setCharacterSize(40.0f);
		updateText->setFont(timesFont);
		updateText->setColor(osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
		updateText->setText("km");
		updateText->setPosition(osg::Vec3d(dx + dWidth * 2.0, dy + dHeight + 4, 0.0));
		updateText->setDataVariance(osg::Object::DYNAMIC);

	}

	return hudCamera;
}

bool DelDir(const QString &path)
{
	if (path.isEmpty()){
		return false;
	}
	QDir dir(path);
	if (!dir.exists()){
		return true;
	}
	dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot); //设置过滤  
	QFileInfoList fileList = dir.entryInfoList(); // 获取所有的文件信息  
	foreach(QFileInfo file, fileList){ //遍历文件信息  
		if (file.isFile()){ // 是文件，删除  
			file.dir().remove(file.fileName());
		}
		else{ // 递归删除  
			DelDir(file.absoluteFilePath());
		}
	}
	return dir.rmpath(dir.absolutePath()); // 删除文件夹  
	}

int
main(int argc, char** argv)
{
#ifdef Q_WS_X11
	XInitThreads();
#endif

	QApplication app(argc, argv);

	QString strResourcePath = QApplication::applicationFilePath();
	strResourcePath = QFileInfo(strResourcePath).absolutePath();
	strResourcePath += "/data/";

	QString strResPath = strResourcePath + "base.earth";
	QByteArray arrayTemp = strResPath.toLocal8Bit();
	char* pPath = arrayTemp.data();
	int nSize = strlen(pPath);

	LoadPosFromFile();

	QString strResourcePath1 = QApplication::applicationFilePath();
	strResourcePath1 = QFileInfo(strResourcePath1).absolutePath();
	strResourcePath1 += "/temp";

	DelDir(strResourcePath1);

	int nArgC = 2;
	char** pArg = new char*[nArgC];
	pArg[0] = "abc.exe";
	pArg[1] = new char[nSize + 1];
	memcpy(pArg[1], pPath, nSize);
	pArg[1][nSize] = 0;

	osg::ArgumentParser arguments(&nArgC, pArg);
	osg::DisplaySettings::instance()->setMinimumNumStencilBits(8);

	// load the .earth file from the command line.
	osg::Node* earthNode = osgDB::readNodeFiles(arguments);
	if (!earthNode)
		return usage("Unable to load earth model.");

	g_earthNode = earthNode;

	osg::Group* root = new osg::Group();
	g_root = root;
	root->addChild(earthNode);

	s_annoGroup = new osg::Group();
	root->addChild(s_annoGroup);

	osg::ref_ptr<osgText::Text> updateText = new osgText::Text;
	g_pText = updateText.get();
	root->addChild(createScaleBarHUD(updateText.get()));

	osg::ref_ptr<osgEarth::MapNode> mapNode = osgEarth::MapNode::findMapNode(earthNode);

	//test code
	if (0)
	{
		OGRFeatureOptions featureOptions;
		featureOptions.url() = "D:/OSG_OSGEarth_RCS/gwaldron-osgearth-25ce0e1/data/world.shp";

		Style style;

		LineSymbol* ls = style.getOrCreateSymbol<LineSymbol>();
		ls->stroke()->color() = Color::Yellow;
		ls->stroke()->width() = 2.0f;

		FeatureGeomModelOptions geomOptions;
		geomOptions.featureOptions() = featureOptions;
		geomOptions.styles() = new StyleSheet();
		geomOptions.styles()->addStyle(style);
		geomOptions.enableLighting() = false;

		ModelLayerOptions layerOptions("my features", geomOptions);
		mapNode->getMap()->addModelLayer(new ModelLayer(layerOptions));
	}

	g_MapNode = mapNode.get();
	QString strFilePath = QApplication::applicationFilePath();
	strFilePath = QFileInfo(strFilePath).absolutePath();

	osg::ref_ptr<osgEarth::QtGui::DataManager> dataManager = new osgEarth::QtGui::DataManager(mapNode.get());
	osgEarth::Drivers::BingOptions bing;
	QByteArray arrayTemp1 = QString(strFilePath + "/data/1").toLocal8Bit();
	bing.key() = arrayTemp1.data();
	dataManager->map()->addImageLayer(new osgEarth::ImageLayer("TileImage", bing));

	DemoMainWindow appWin(dataManager.get(), mapNode.get(), s_annoGroup);

	osgEarth::QtGui::ViewVector views;
	osg::ref_ptr<osgViewer::ViewerBase> viewer;

	osgEarth::QtGui::ViewerWidget* viewerWidget = 0L;

	// tests: implicity creating a viewer.
	viewerWidget = new osgEarth::QtGui::ViewerWidget(root);
	osgViewer::ViewerBase* pViewBase = viewerWidget->getViewer();

	osgViewer::Viewer* pViewer = dynamic_cast<osgViewer::Viewer*>(pViewBase);
	MyManipulator* pCameraManipulator = new MyManipulator;
	pViewer->setCameraManipulator(pCameraManipulator);

	{
		osgEarth::Viewpoint viewPoint = pCameraManipulator->getViewpoint();
		double dRange = viewPoint.getRange();
		viewPoint.setRange(g_dOriginHeight);

		pCameraManipulator->setViewpoint(viewPoint);
	}

	//osgEarth::Annotation::LocalGeometryNode* pPlaneTag = CreatePlaneTag(mapNode.get());

	//添加飞行轨迹线
	if (0)
	{
		OGRFeatureOptions featureOptions;

		LineString* line = new LineString();
		line->push_back(osg::Vec3d(-60, 20, 1000.0));
		line->push_back(osg::Vec3d(-120, 20, 1000.0));
		line->push_back(osg::Vec3d(-120, 60, 1000.0));
		line->push_back(osg::Vec3d(-60, 60, 1000.0));
		featureOptions.geometry() = line;

		Style style;

		LineSymbol* ls = style.getOrCreateSymbol<LineSymbol>();
		ls->stroke()->color() = Color::Yellow;
		ls->stroke()->width() = 2.0f;

		FeatureGeomModelOptions geomOptions;
		geomOptions.featureOptions() = featureOptions;
		geomOptions.styles() = new StyleSheet();
		geomOptions.styles()->addStyle(style);
		geomOptions.enableLighting() = false;

		ModelLayerOptions layerOptions("my features", geomOptions);
		mapNode->getMap()->addModelLayer(new ModelLayer(layerOptions));
	}
	else
	{
		/*osg::ref_ptr<osg::Geode> geode*/g_geode = new osg::Geode();
		g_geodeTarget = new osg::Geode();
		//g_geode->addDrawable(linesGeom);
		g_geode->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
		g_geodeTarget->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);

		root->addChild(g_geode);
		root->addChild(g_geodeTarget);
	}

	//创建并加载飞机
	QString strPlanePath = strResourcePath + "plane.png";
	QByteArray arrayPlane = strPlanePath.toLocal8Bit();
	char* pPathPlanePNG = arrayPlane.data();

	Style pin;
	pin.getOrCreate<IconSymbol>()->url()->setLiteral(pPathPlanePNG);
	pin.getOrCreate<IconSymbol>()->alignment() = osgEarth::Symbology::IconSymbol::ALIGN_CENTER_CENTER;
	//PlaceNode* pPlaneTag = new PlaceNode(mapNode, GeoPoint(osgEarth::SpatialReference::get("wgs84"), 0.0, 0.0, 10000.0), "", pin);
	MyPlaceNode* pPlaneTag = new MyPlaceNode(mapNode, GeoPoint(osgEarth::SpatialReference::get("wgs84"), g_dPlanePosLon, g_dPlanePosLat, 10000.0), "", pin);
	pPlaneTag->RotateHeading(g_dPlanePosAngle);

	dataManager->addAnnotation(pPlaneTag, s_annoGroup);
	UDPServer udpServer(g_geode.get(), g_dPlanePosLon, g_dPlanePosLat, g_dPlanePosAngle, 6665, pViewBase, pPlaneTag);

	//创建并加载目标
	QString strTargetPath = strResourcePath + "target.png";
	QByteArray arrayTarget = strTargetPath.toLocal8Bit();
	char* pPathTargetPNG = arrayTarget.data();

	Style pin2;
	pin2.getOrCreate<IconSymbol>()->url()->setLiteral(pPathTargetPNG);
	pin2.getOrCreate<IconSymbol>()->alignment() = osgEarth::Symbology::IconSymbol::ALIGN_CENTER_CENTER;
	MyPlaceNode* pTargetTag = new MyPlaceNode(mapNode, GeoPoint(osgEarth::SpatialReference::get("wgs84"), g_dTargetPosLon, g_dTargetPosLat, 10000.0), "", pin2);

	double nTemp;
	dataManager->addAnnotation(pTargetTag, s_annoGroup);
	UDPServer udpServer2(g_geodeTarget.get(), g_dTargetPosLon, g_dTargetPosLat, nTemp, 6666, pViewBase, pTargetTag);

#if OSG_MIN_VERSION_REQUIRED(3,3,2)
	// Enable touch events on the viewer
	viewerWidget->getGraphicsWindow()->setTouchEventsEnabled(true);
#endif

	//osgEarth::QtGui::ViewerWidget* viewerWidget = new osgEarth::QtGui::ViewerWidget(root);
	//viewerWidget->setGeometry(50, 50, 1024, 768);

	viewerWidget->getViews(views);

	for (osgEarth::QtGui::ViewVector::iterator i = views.begin(); i != views.end(); ++i)
	{
		i->get()->getCamera()->addCullCallback(new osgEarth::Util::AutoClipPlaneCullCallback(mapNode));
	}
	appWin.setViewerWidget(viewerWidget);

	if (mapNode.valid())
	{
		const Config& externals = mapNode->externalConfig();

		if (mapNode->getMap()->isGeocentric())
		{
			// Sky model.
			Config skyConf = externals.child("sky");

			double hours = skyConf.value("hours", 12.0);
			s_sky = osgEarth::Util::SkyNode::create(mapNode);
			s_sky->setDateTime(DateTime(2011, 3, 6, hours));
			for (osgEarth::QtGui::ViewVector::iterator i = views.begin(); i != views.end(); ++i)
				s_sky->attach(*i, 0);
			root->addChild(s_sky);

			// Ocean surface.
			if (externals.hasChild("ocean"))
			{
				s_ocean = osgEarth::Util::OceanNode::create(
					osgEarth::Util::OceanOptions(externals.child("ocean")),
					mapNode.get());

				if (s_ocean)
					root->addChild(s_ocean);
			}
		}
	}

	viewer = viewerWidget->getViewer();
	g_viewerMain = dynamic_cast<osgViewer::Viewer*>(viewer.get());

	if (viewer.valid())
		viewer->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);


	// create catalog widget and add as a docked widget to the main window
	//     QDockWidget *catalogDock = new QDockWidget(QWidget::tr("Layers"));
	//     catalogDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	//     osgEarth::QtGui::MapCatalogWidget* layerCatalog = new osgEarth::QtGui::MapCatalogWidget(dataManager.get(), osgEarth::QtGui::MapCatalogWidget::ALL_LAYERS);
	//     layerCatalog->setActiveViews(views);
	//     layerCatalog->setHideEmptyGroups(true);
	//     catalogDock->setWidget(layerCatalog);
	//     appWin.addDockWidget(Qt::LeftDockWidgetArea, catalogDock);

	// create layer manager widget and add as a docked widget on the right
	//     QDockWidget *layersDock = new QDockWidget(QWidget::tr("Image Layers"));
	//     layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	//     osgEarth::QtGui::LayerManagerWidget* layerManager = new osgEarth::QtGui::LayerManagerWidget(dataManager.get(), osgEarth::QtGui::LayerManagerWidget::IMAGE_LAYERS);
	//     layerManager->setActiveViews(views);
	//     layersDock->setWidget(layerManager);
	//     appWin.addDockWidget(Qt::RightDockWidgetArea, layersDock);

	appWin.setGeometry(100, 100, 1280, 800);
	appWin.show();

	int nRes = app.exec();

	SavePosFromFile();
	return nRes;
}
