#include "TelemetryTrackBuilder.h"

#include "../Config.h"
#include "../input/Input.h"
#include "../network/Server.h"
#include "../rendering/Interpolation.h"

#include <GeographicLib/UTMUPS.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

extern MapOrigin g_map_origin;
extern std::atomic<bool> g_is_map_loaded;

namespace
{
	std::mutex g_mutex;
	TelemetryTrackBuilder::Settings g_settings;
	bool g_active = false;
	bool g_finalized = false;
	bool g_origin_initialized = false;
	bool g_auto_save_requested = false;

	std::vector<glm::vec2> g_points;
	std::vector<SplinePoint> g_smooth;
	glm::vec2 g_sf_p1(0.0f, 0.0f);
	glm::vec2 g_sf_p2(0.0f, 0.0f);

	glm::vec2 g_start(0.0f, 0.0f);
	glm::vec2 g_last(0.0f, 0.0f);
	float g_len_m = 0.0f;

	static float distNorm(const glm::vec2& a, const glm::vec2& b)
	{
		return glm::distance(a, b);
	}

	static float normToMeters(float d)
	{
		return d * static_cast<float>(MapConstants::MAP_SIZE);
	}

	static void rebuildSmoothLocked()
	{
		g_smooth.clear();
		if (g_points.size() < 2)
			return;
		g_smooth = interpolatePointsWithTangents(g_points, g_settings.splinePointsPerSegment);
		if (g_smooth.size() >= 2)
		{
			g_sf_p1 = g_smooth[0].position;
			g_sf_p2 = g_smooth[1].position;
		}
	}

	static void ensureOriginLocked(double lat_deg, double lon_deg)
	{
		if (g_origin_initialized)
			return;

		double e = 0.0;
		double n = 0.0;
		createOriginDD(lat_deg, lon_deg, e, n);
		g_map_origin.m_map_size = MapConstants::MAP_SIZE;
		g_is_map_loaded = true;
		g_origin_initialized = true;
	}

	static bool isClosedReadyLocked()
	{
		if (g_points.size() < g_settings.minPointsToClose)
			return false;
		if (g_len_m < g_settings.minLoopLengthMeters)
			return false;
		const float d = distNorm(g_points.back(), g_start);
		return d <= g_settings.closeRadiusNorm;
	}

	static std::string sanitizeNameToFile(std::string s)
	{
		for (char& c : s)
		{
			if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ' '))
				c = '_';
		}
		// trim
		while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
		while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
		if (s.empty()) s = "track";
		return s;
	}

	static bool savePointsAsTxtLocked(const std::string& userNameOrPath)
	{
		namespace fs = std::filesystem;
		fs::path outPath;
		if (userNameOrPath.empty())
		{
			outPath = fs::path("src/saves") / "track_recorded.txt";
		}
		else
		{
			fs::path p(userNameOrPath);
			if (p.has_parent_path())
			{
				outPath = p;
			}
			else
			{
				outPath = fs::path("src/saves") / sanitizeNameToFile(userNameOrPath);
			}
			if (outPath.extension().empty())
				outPath.replace_extension(".txt");
		}

		try { fs::create_directories(outPath.parent_path()); }
		catch (...) {}

		std::ofstream f(outPath.string());
		if (!f)
			return false;

		// Save as decimal degrees (lat lon). Parser accepts digits/dots/minus and extracts 6 numbers; 
		// but it also works with two decimals because it will read first two numbers and set others to 0.
		// To make parsing robust, write as: lat_deg 0 0 lon_deg 0 0
		f.setf(std::ios::fixed);
		f << std::setprecision(7);
		for (const glm::vec2& p : g_points)
		{
			double utmE = g_map_origin.m_origin_meters_easting + (static_cast<double>(p.x) * MapConstants::MAP_SIZE);
			double utmN = g_map_origin.m_origin_meters_northing + (static_cast<double>(p.y) * MapConstants::MAP_SIZE);
			double lat = 0.0;
			double lon = 0.0;
			try {
				using namespace GeographicLib;
				bool northp = (g_map_origin.m_origin_zone_char >= 'N');
				UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp, utmE, utmN, lat, lon);
			}
			catch (...) {
				continue;
			}
			f << lat << " 0 0 " << lon << " 0 0\n";
		}

		std::cout << "[TRACK-BUILD] Saved track to: " << outPath.string() << std::endl;
		return true;
	}

	static bool finalizeLocked(bool ensureClosed)
	{
		if (!g_active || g_finalized)
			return false;
		if (g_points.size() < 2)
			return false;

		if (ensureClosed)
		{
			if (!isClosedReadyLocked())
				return false;
			g_points.push_back(g_start);
		}

		// Apply recenter only if closed logic says closed.
		TrackCenterInfo info = calculateTrackCenter(g_points);
		if (info.is_closed)
		{
			recenterTrack(g_points, info);

			// Update origin UTM to match shifting (same idea as existing UI recent-file loader)
			g_map_origin.m_origin_meters_easting -= info.offset.x * MapConstants::MAP_SIZE;
			g_map_origin.m_origin_meters_northing -= info.offset.y * MapConstants::MAP_SIZE;
			try {
				using namespace GeographicLib;
				bool northp = (g_map_origin.m_origin_zone_char >= 'N');
				UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp,
					g_map_origin.m_origin_meters_easting, g_map_origin.m_origin_meters_northing,
					g_map_origin.m_origin_lat_dd, g_map_origin.m_origin_lon_dd);
			}
			catch (...) {}
		}

		rebuildSmoothLocked();
		g_finalized = true;
		g_active = false;
		return true;
	}
}

namespace TelemetryTrackBuilder
{
	void Start(const Settings& settings)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_settings = settings;
		g_active = true;
		g_finalized = false;
		g_origin_initialized = false;
       g_auto_save_requested = false;
		g_points.clear();
		g_smooth.clear();
		g_sf_p1 = glm::vec2(0.0f, 0.0f);
		g_sf_p2 = glm::vec2(0.0f, 0.0f);
		g_len_m = 0.0f;
	}

	void Stop()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_active = false;
        g_finalized = true; // Reset finalized flag to OFF after loop closure
		g_origin_initialized = false;
       g_auto_save_requested = false;
		g_points.clear();
		g_smooth.clear();
		g_len_m = 0.0f;
	}

	bool IsActive()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_active;
	}

	bool IsFinalized()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_finalized;
	}

	void OnTelemetryPacket(const TelemetryPacket& packet)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || g_finalized)
			return;
		if (packet.MagicMarker != PACKET_MAGIC_DATA && packet.MagicMarker != PacketMagic::DATA)
			return;

		const double lat = static_cast<double>(packet.lat) / 1e7;
		const double lon = static_cast<double>(packet.lon) / 1e7;
		ensureOriginLocked(lat, lon);

		double e = 0.0;
		double n = 0.0;
		coordinatesToMeters(lat, lon, e, n);
		double nx = 0.0;
		double ny = 0.0;
		getCoordinateDifferenceFromOrigin(e, n, nx, ny);

		glm::vec2 pos(static_cast<float>(nx), static_cast<float>(ny));

		if (g_points.empty())
		{
			g_points.push_back(pos);
			g_start = pos;
			g_last = pos;
			rebuildSmoothLocked();
			return;
		}

		const float d = distNorm(pos, g_last);
		if (d < g_settings.minPointDistanceNorm)
			return;

		g_points.push_back(pos);
		g_len_m += normToMeters(d);
		g_last = pos;

		if ((g_points.size() % 20) == 0)
			rebuildSmoothLocked();

		// Auto finalize if loop closed.
		if (finalizeLocked(true))
		{
          // UI thread should open Save As dialog.
			if (!g_auto_save_requested)
				g_auto_save_requested = true;
		}
	}

   bool ConsumeAutoSaveRequest() // Ensure Save As prompt triggers once per auto-close
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		const bool v = g_auto_save_requested;
		g_auto_save_requested = false;
		return v;
	}


	bool FinalizeOpenAndSaveTxt(const std::string& userNameOrPath)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!finalizeLocked(false))
			return false;
		return savePointsAsTxtLocked(userNameOrPath);
	}

	bool SaveFinalizedAsTxt(const std::string& userNameOrPath)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_finalized)
			return false;
		return savePointsAsTxtLocked(userNameOrPath);
	}

	std::vector<glm::vec2> GetRawPointsSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_points;
	}

	std::vector<SplinePoint> GetSmoothPointsSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_smooth;
	}

	glm::vec2 GetStartFinishP1()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_sf_p1;
	}

	glm::vec2 GetStartFinishP2()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_sf_p2;
	}
}
