#include "TrackRecorder.h"

#include "../Config.h"
#include "../input/Input.h"
#include "../rendering/Interpolation.h"
#include "../network/Server.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>

namespace
{
	std::mutex g_mutex;
	TrackRecorder::Settings g_settings;
	TrackRecorder::State g_state;

	std::vector<glm::vec2> g_raw_points;
	std::vector<SplinePoint> g_smooth_points;

	glm::vec2 g_sf_p1(0.0f, 0.0f);
	glm::vec2 g_sf_p2(0.0f, 0.0f);

	static float distanceNorm(const glm::vec2& a, const glm::vec2& b)
	{
		return glm::distance(a, b);
	}

	static float distanceMetersFromNorm(float dNorm)
	{
		return dNorm * static_cast<float>(MapConstants::MAP_SIZE);
	}

	static void rebuildSmoothLocked()
	{
		g_smooth_points.clear();
		if (g_raw_points.size() < 2)
			return;

		// Keep raw points as-is. Tangent generation is done by spline interpolation.
		g_smooth_points = interpolatePointsWithTangents(g_raw_points, g_settings.pointsPerSegment);

		if (g_smooth_points.size() >= 2)
		{
			g_sf_p1 = g_smooth_points[0].position;
			g_sf_p2 = g_smooth_points[1].position;
		}
	}

	static bool tryCloseLoopLocked()
	{
		if (!g_state.isRecording || g_state.isFinalized)
			return false;

		if (g_raw_points.size() < g_settings.minPointsToClose)
			return false;

		if (g_state.approximateLengthMeters < g_settings.minLoopLengthMeters)
			return false;

		const glm::vec2 cur = g_raw_points.back();
		const float distToStart = distanceNorm(cur, g_state.startPoint);
		if (distToStart > g_settings.closeRadiusNorm)
			return false;

		// Close by snapping the last point to the exact start.
		g_raw_points.push_back(g_state.startPoint);
		g_state.rawPointCount = g_raw_points.size();
		rebuildSmoothLocked();
		g_state.isFinalized = true;
		g_state.isRecording = false;
		return true;
	}

	#pragma pack(push, 1)
	struct TrackFileHeader
	{
		TrackDataHeader net;
	};
	#pragma pack(pop)
}

namespace TrackRecorder
{
	void Start(int32_t vehicleId, const Settings& settings)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_settings = settings;
		g_state = State{};
		g_state.isRecording = true;
		g_state.isFinalized = false;
		g_state.sourceVehicleId = vehicleId;
		g_raw_points.clear();
		g_smooth_points.clear();
		g_sf_p1 = glm::vec2(0.0f, 0.0f);
		g_sf_p2 = glm::vec2(0.0f, 0.0f);
	}

	void Stop()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_state.isRecording = false;
		g_state.isFinalized = false;
		g_state.sourceVehicleId = -1;
		g_raw_points.clear();
		g_smooth_points.clear();
		g_state.rawPointCount = 0;
		g_state.approximateLengthMeters = 0.0f;
	}

	bool IsRecording()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_state.isRecording;
	}

	bool IsFinalized()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_state.isFinalized;
	}

	State GetState()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_state.rawPointCount = g_raw_points.size();
		return g_state;
	}

	void OnTelemetryPosition(int32_t vehicleId, const glm::vec2& pos)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_state.isRecording || g_state.isFinalized)
			return;
		if (g_state.sourceVehicleId != -1 && vehicleId != g_state.sourceVehicleId)
			return;

		if (g_raw_points.empty())
		{
			g_raw_points.push_back(pos);
			g_state.startPoint = pos;
			g_state.lastPoint = pos;
			g_state.approximateLengthMeters = 0.0f;
			g_state.rawPointCount = g_raw_points.size();
			rebuildSmoothLocked();
			return;
		}

		const float d = distanceNorm(pos, g_state.lastPoint);
		if (d < g_settings.minPointDistanceNorm)
			return;

		g_raw_points.push_back(pos);
		g_state.approximateLengthMeters += distanceMetersFromNorm(d);
		g_state.lastPoint = pos;
		g_state.rawPointCount = g_raw_points.size();

		// Rebuild spline periodically to keep UI responsive.
		if ((g_raw_points.size() % 20) == 0)
			rebuildSmoothLocked();

		tryCloseLoopLocked();
	}

	std::vector<SplinePoint> GetSmoothTrackSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_smooth_points;
	}

	bool FinalizeNow()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return tryCloseLoopLocked();
	}

	bool SaveToFile(const std::string& filename, const MapOrigin& origin)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_smooth_points.size() < 2)
			return false;

		std::ofstream f(filename, std::ios::binary);
		if (!f)
			return false;

		TrackDataHeader header{};
		header.magic_marker = PacketMagic::TRCK;
		header.point_count = static_cast<uint32_t>(g_smooth_points.size());
		header.origin_lat = origin.m_origin_lat_dd;
		header.origin_lon = origin.m_origin_lon_dd;
		header.origin_easting = origin.m_origin_meters_easting;
		header.origin_northing = origin.m_origin_meters_northing;
		header.origin_zone = origin.m_origin_zone_int;
		header.origin_zone_char = origin.m_origin_zone_char;
		header.start_finish_p1_x = g_sf_p1.x;
		header.start_finish_p1_y = g_sf_p1.y;
		header.start_finish_p2_x = g_sf_p2.x;
		header.start_finish_p2_y = g_sf_p2.y;

		f.write(reinterpret_cast<const char*>(&header), sizeof(header));

		for (const SplinePoint& sp : g_smooth_points)
		{
			TrackPointPacket p{};
			p.x = sp.position.x;
			p.y = sp.position.y;
			p.tangent_x = sp.tangent.x;
			p.tangent_y = sp.tangent.y;
			f.write(reinterpret_cast<const char*>(&p), sizeof(p));
		}

		return static_cast<bool>(f);
	}

	bool LoadFromFile(const std::string& filename, MapOrigin& out_origin, std::vector<SplinePoint>& out_track,
		glm::vec2& out_sf_p1, glm::vec2& out_sf_p2)
	{
		std::ifstream f(filename, std::ios::binary);
		if (!f)
			return false;

		TrackDataHeader header{};
		f.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!f)
			return false;
		if (header.magic_marker != PacketMagic::TRCK)
			return false;

		out_origin.m_origin_lat_dd = header.origin_lat;
		out_origin.m_origin_lon_dd = header.origin_lon;
		out_origin.m_origin_meters_easting = header.origin_easting;
		out_origin.m_origin_meters_northing = header.origin_northing;
		out_origin.m_origin_zone_int = header.origin_zone;
		out_origin.m_origin_zone_char = header.origin_zone_char;
		out_origin.m_map_size = MapConstants::MAP_SIZE;

		out_sf_p1 = glm::vec2(header.start_finish_p1_x, header.start_finish_p1_y);
		out_sf_p2 = glm::vec2(header.start_finish_p2_x, header.start_finish_p2_y);

		out_track.clear();
		out_track.reserve(header.point_count);
		for (uint32_t i = 0; i < header.point_count; ++i)
		{
			TrackPointPacket p{};
			f.read(reinterpret_cast<char*>(&p), sizeof(p));
			if (!f)
				return false;
			SplinePoint sp;
			sp.position = glm::vec2(p.x, p.y);
			sp.tangent = glm::vec2(p.tangent_x, p.tangent_y);
			out_track.push_back(sp);
		}

		return true;
	}
}
