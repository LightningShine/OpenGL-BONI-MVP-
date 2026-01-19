#include "../vehicle/Vehicle.h"

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

Venchile::Venchile(struct TelemetryPacket& packet)
{
	v_latDD = packet.lat / 1e7;
	v_lonDD = packet.lon / 1e7;
	v_speedKPH = packet.speed / 100.0;
	v_acceleration = packet.acceleration / 100.0;
	v_gForceX = packet.gForceX / 100.0;
	v_gForceY = packet.gForceY / 100.0;
	v_fixtype = packet.fixtype;
	v_ID = packet.ID;
	DDToMetr(v_latDD, v_lonDD, v_metr_easting, v_metr_north);
	CordinateDifirenceFromOrigin(v_metr_easting, v_metr_north, v_norX, v_norY);
}

void CreateVenchile(Venchile &car)
{
	DDToMetr(car.v_latDD, car.v_lonDD, car.v_metr_easting, car.v_metr_north);
	CordinateDifirenceFromOrigin(car.v_metr_easting, car.v_metr_north, car.v_norX, car.v_norY);

}

