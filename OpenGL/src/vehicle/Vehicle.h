#pragma once
#include "../input/Input.h"
#include "../network/Server.h"

class Venchile
{
public:
	Venchile(struct TelemetryPacket &packet);
	
	double v_latDD;
	double v_lonDD;
	double v_metr_easting = 0;
	double v_metr_north = 0;
	double v_norX = 0;
	double v_norY = 0;
	double v_speedKPH;
	double v_acceleration;
	double v_gForceX;
	double v_gForceY;
	int16_t v_fixtype;
	int32_t v_ID;
	std::string name = "Unknown";


private:
	
};





//struct TelemetryPacket
//{
//	uint32_t MagicMarker;	// 'DATA' 0x44415441
//	int32_t lat;			// latitude degree * 1e7
//	int32_t lon;			// longitude degree * 1e7
//	uint32_t time;			// time in milliseconds
//	uint32_t speed;			// speed in km/h * 100
//	uint32_t acceleration;	// acceleration * 100
//	uint16_t gForceX;		// g-force X * 100
//	uint16_t gForceY;		// g-force Y * 100
//	int16_t fixtype;		// 0=none, 4=RTK_FIXED, etc.
//	int32_t ID;			    // vehicle ID
//};