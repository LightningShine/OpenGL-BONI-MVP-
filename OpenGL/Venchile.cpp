#pragma once 

#include "VENCHILEH.h"
#include "TelemetryServer.h"
#include "Input.h"
#include <cmath>
#include <UTMUPS.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern JsonInput jsonClass;

void VenchileManager::UpdateFromTelemetry(const ParsedTelemetry& telemetry)
{
	std::lock_guard<std::mutex> lock(VenchileMutex);

	// Find existing vehicle
	for (auto& vehicle : venchiles)
	{
		if (vehicle.deviceId == telemetry.deviceId)
		{
			// Update existing vehicle
			vehicle.latitude = telemetry.latitude;
			vehicle.longitude = telemetry.longitude;
			vehicle.timestamp = telemetry.timestamp;
			vehicle.direction = static_cast<float>(telemetry.course);
			vehicle.speed_kmh = telemetry.speed * 3.6; // m/s to km/h
			vehicle.acceleration = telemetry.acceleration;
			vehicle.accelerationValid = telemetry.accelerationValid;
			vehicle.fixType = static_cast<uint8_t>(telemetry.fixType);
			vehicle.satellites = telemetry.satellites;

			ConvertToNormalizedCoordinates(vehicle, jsonClass);

			// Update path history
			vehicle.path.push_back(glm::vec2{ vehicle.normalized_x, vehicle.normalized_y });
			if (vehicle.path.size() > MAX_PATH_POINTS)
			{
				vehicle.path.pop_front();
			}

			std::cout << "[Vehicle] Updated: " << telemetry.deviceId 
				<< " pos(" << vehicle.normalized_x << ", " << vehicle.normalized_y << ")\n";
			return;
		}
	}

	// Create new vehicle
	Venchile newVehicle;
	newVehicle.deviceId = telemetry.deviceId;
	newVehicle.displayName = telemetry.deviceId;
	newVehicle.latitude = telemetry.latitude;
	newVehicle.longitude = telemetry.longitude;
	newVehicle.timestamp = telemetry.timestamp;
	newVehicle.direction = static_cast<float>(telemetry.course);
	newVehicle.speed_kmh = telemetry.speed * 3.6;
	newVehicle.acceleration = telemetry.acceleration;
	newVehicle.accelerationValid = telemetry.accelerationValid;
	newVehicle.fixType = static_cast<uint8_t>(telemetry.fixType);
	newVehicle.satellites = telemetry.satellites;
	newVehicle.color = GenerateColorFromID(telemetry.deviceId);

	ConvertToNormalizedCoordinates(newVehicle, jsonClass);
	venchiles.push_back(newVehicle);

	std::cout << "[Vehicle] New added: " << telemetry.deviceId << "\n";
}

void VenchileManager::UpdateVenchile(const std::string& id, double lat, double lon, unsigned timestamp, float direction, float speed)
{
	std::lock_guard<std::mutex> lock(VenchileMutex);

	for (auto& vehicle : venchiles)
	{
		if (vehicle.deviceId == id)
		{
			vehicle.latitude = lat;
			vehicle.longitude = lon;
			vehicle.timestamp = timestamp;
			vehicle.direction = direction;
			vehicle.speed_kmh = speed;
			ConvertToNormalizedCoordinates(vehicle, jsonClass);

			vehicle.path.push_back(glm::vec2{ vehicle.normalized_x, vehicle.normalized_y });
			if (vehicle.path.size() > MAX_PATH_POINTS)
			{
				vehicle.path.pop_front();
			}
			return;
		}
	}

	// Vehicle not found - add new
	Venchile newVehicle;
	newVehicle.deviceId = id;
	newVehicle.latitude = lat;
	newVehicle.longitude = lon;
	newVehicle.timestamp = timestamp;
	newVehicle.direction = direction;
	newVehicle.speed_kmh = speed;
	newVehicle.color = GenerateColorFromID(id);
	newVehicle.fixType = 0;
	newVehicle.accelerationValid = false;

	ConvertToNormalizedCoordinates(newVehicle, jsonClass);
	venchiles.push_back(newVehicle);

	std::cout << "[Vehicle] New added: " << id << "\n";
}

void VenchileManager::ConvertToNormalizedCoordinates(Venchile& vehicle, const JsonInput& origin)
{
	double easting, northing;

	CordinateToMetersUTM(vehicle.latitude, vehicle.longitude, easting, northing);

	double diff_easting = easting - origin.easting;
	double diff_northing = northing - origin.northing;

	// Normalize based on map size (100 meters default)
	vehicle.normalized_x = diff_easting / 100.0;
	vehicle.normalized_y = diff_northing / 100.0;
}

bool VenchileManager::HasValidFix(const Venchile& vehicle)
{
	return vehicle.fixType > 0 && 
	       vehicle.latitude != 0.0 && 
	       vehicle.longitude != 0.0;
}

void VenchileManager::RenderVenchiles(bool trackLoaded)
{
	// Don't render if track is not loaded
	if (!trackLoaded)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(VenchileMutex);
	
	for (const auto& vehicle : venchiles)
	{
		// Only render vehicles with valid GPS fix
		if (HasValidFix(vehicle))
		{
			DrawVenchileShape(vehicle);
		}
	}
}

void VenchileManager::DrawVenchileShape(const Venchile& vehicle)
{
	glColor3f(vehicle.color.r, vehicle.color.g, vehicle.color.b);
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(static_cast<float>(vehicle.normalized_x), static_cast<float>(vehicle.normalized_y));
	
	for (int i = 0; i <= VEHICLE_SEGMENTS; ++i)
	{
		float angle = i * 2.0f * static_cast<float>(M_PI) / VEHICLE_SEGMENTS;
		float x = static_cast<float>(vehicle.normalized_x) + cos(angle) * VEHICLE_RADIUS;
		float y = static_cast<float>(vehicle.normalized_y) + sin(angle) * VEHICLE_RADIUS;
		glVertex2f(x, y);
	}
	glEnd();

	// White outline
	glColor3f(1.0f, 1.0f, 1.0f);
	glLineWidth(2.0f);
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i <= VEHICLE_SEGMENTS; ++i)
	{
		float angle = i * 2.0f * static_cast<float>(M_PI) / VEHICLE_SEGMENTS;
		float x = static_cast<float>(vehicle.normalized_x) + cos(angle) * VEHICLE_RADIUS;
		float y = static_cast<float>(vehicle.normalized_y) + sin(angle) * VEHICLE_RADIUS;
		glVertex2f(x, y);
	}
	glEnd();
}

void VenchileManager::RemoveOldVenchiles(unsigned currentTime, unsigned maxAge)
{
	std::lock_guard<std::mutex> lock(VenchileMutex);
	venchiles.erase(
		std::remove_if(venchiles.begin(), venchiles.end(),
			[currentTime, maxAge](const Venchile& vehicle)
			{
				return (currentTime - vehicle.timestamp) > maxAge;
			}),
		venchiles.end());
}

std::vector<Venchile> VenchileManager::getVenchiles() const
{
	std::lock_guard<std::mutex> lock(VenchileMutex);
	return venchiles;
}

size_t VenchileManager::GetVenchileCount() const
{
	std::lock_guard<std::mutex> lock(VenchileMutex);
	return venchiles.size();
}

glm::vec3 VenchileManager::GenerateColorFromID(const std::string& id)
{
	std::hash<std::string> hasher;
	size_t hash = hasher(id);
	
	float r = ((hash & 0xFF0000) >> 16) / 255.0f;
	float g = ((hash & 0x00FF00) >> 8) / 255.0f;
	float b = (hash & 0x0000FF) / 255.0f;
	
	// Ensure minimum brightness
	float brightness = (r + g + b) / 3.0f;
	if (brightness < 0.4f)
	{
		float boost = 0.4f - brightness;
		r = std::min(1.0f, r + boost);
		g = std::min(1.0f, g + boost);
		b = std::min(1.0f, b + boost);
	}
	
	return glm::vec3(r, g, b);
}

void VenchileManager::CalculateSpeedAndAcceleration(Venchile& vehicle, double new_lat, double new_lon, unsigned new_timestamp)
{
	// TODO: Implement if needed
}
