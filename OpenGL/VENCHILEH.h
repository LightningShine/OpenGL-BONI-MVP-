#pragma once
#include "Input.h"
#include <ctime>
#include <algorithm>
#include <deque>

class JsonInput;

// Forward declaration
struct ParsedTelemetry;

struct Venchile
{
	std::string deviceId;
	std::string displayName;
	double latitude;
	double longitude;
	unsigned timestamp;
	double normalized_x;
	double normalized_y;
	glm::vec3 color;
	double speed_kmh;
	double acceleration;
	bool accelerationValid;
	float direction;
	uint8_t fixType;
	uint8_t satellites;
	std::deque<glm::vec2> path;
};

class VenchileManager
{
public:
	VenchileManager() = default;

	// Update from telemetry data
	void UpdateFromTelemetry(const ParsedTelemetry& telemetry);

	// Legacy: Updating or adding a venchile
	void UpdateVenchile(const std::string& id, double lat, double lon, unsigned timestamp, float direction, float speed);

	// Render all venchiles (only if track is loaded)
	void RenderVenchiles(bool trackLoaded);

	// Remove stale venchiles
	void RemoveOldVenchiles(unsigned currentTime, unsigned maxAge);

	// Access to venchiles for other operations
	std::vector<Venchile> getVenchiles() const;

	size_t GetVenchileCount() const;

	// Check if vehicle has valid GPS fix
	static bool HasValidFix(const Venchile& vehicle);

private:
	std::vector<Venchile> venchiles;
	mutable std::mutex VenchileMutex;

	static constexpr size_t MAX_PATH_POINTS = 50;
	static constexpr float VEHICLE_RADIUS = 0.01f;
	static constexpr int VEHICLE_SEGMENTS = 24;

	void ConvertToNormalizedCoordinates(Venchile& vehicle, const JsonInput& origin);
	glm::vec3 GenerateColorFromID(const std::string& id);
	void CalculateSpeedAndAcceleration(Venchile& vehicle, double new_lat, double new_lon, unsigned new_timestamp);
	void DrawVenchileShape(const Venchile& vehicle);
};