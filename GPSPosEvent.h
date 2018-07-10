#pragma once

#include "osg/Referenced"

struct GPSPosEvent : public osg::Referenced
{
	GPSPosEvent(double d1, double d2)
	{
		dLon = d1;
		dLat = d2;
	}

	double dLon;
	double dLat;
};

