#include "TelemetryTrackBuilder.h"

#include "../Config.h"
#include "../input/Input.h"
#include "../network/Server.h"
#include "../rendering/Interpolation.h"

#include <GeographicLib/UTMUPS.hpp>

#include <algorithm>
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

	bool g_active    = false;
	bool g_finalized = false;
	bool g_origin_initialized  = false;
	bool g_auto_save_requested = false;

	EdgePhase g_phase = EdgePhase::Idle;

	std::vector<glm::vec2> g_points;       // active recording buffer
	std::vector<glm::vec2> g_left_points;  // stored after FinishLeftEdge
	std::vector<SplinePoint> g_smooth;
	glm::vec2 g_sf_p1(0.0f), g_sf_p2(0.0f);
	glm::vec2 g_start(0.0f), g_last(0.0f);
	float g_len_m = 0.0f;
	int g_last_fix_type = 0;               // fix type of the latest GPS packet

	// ── helpers ─────────────────────────────────────────────────────────────

	static float distNorm(const glm::vec2& a, const glm::vec2& b)
	{
		return glm::distance(a, b);
	}

	static void ensureOriginLocked(double lat, double lon)
	{
		if (g_origin_initialized) return;
		double e = 0.0, n = 0.0;
		createOriginDD(lat, lon, e, n);
		g_map_origin.m_map_size = MapConstants::MAP_SIZE;
		g_is_map_loaded = true;
		g_origin_initialized = true;
	}

	static void rebuildSmoothLocked()
	{
		g_smooth.clear();
		if (g_points.size() < 2) return;
		g_smooth = interpolatePointsWithTangents(g_points, g_settings.splinePointsPerSegment);
		if (g_smooth.size() >= 2) { g_sf_p1 = g_smooth[0].position; g_sf_p2 = g_smooth[1].position; }
	}

	static bool isClosedReadyLocked()
	{
		return g_points.size() >= g_settings.minPointsToClose
			&& g_len_m >= g_settings.minLoopLengthMeters
			&& distNorm(g_points.back(), g_start) <= g_settings.closeRadiusNorm;
	}

	static std::string sanitizeName(std::string s)
	{
		for (char& c : s)
			if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ' '))
				c = '_';
		while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
		while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
		return s.empty() ? "track" : s;
	}

	// Convert normalized point to GPS DMS and write one line
	static void writePointDMS(std::ofstream& f, const glm::vec2& p)
	{
		double utmE = g_map_origin.m_origin_meters_easting  + (double)p.x * MapConstants::MAP_SIZE;
		double utmN = g_map_origin.m_origin_meters_northing + (double)p.y * MapConstants::MAP_SIZE;
		double lat = 0.0, lon = 0.0;
		try {
			bool northp = g_map_origin.m_origin_zone_char >= 'N';
			GeographicLib::UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp, utmE, utmN, lat, lon);
		} catch (...) { return; }

		auto dms = [](double dd, int& deg, int& min, double& sec) {
			double sign = dd < 0 ? -1.0 : 1.0;
			double a = std::abs(dd);
			deg = (int)std::floor(a);
			double m = (a - deg) * 60.0;
			min = (int)std::floor(m);
			sec = (m - min) * 60.0;
			deg = (int)(deg * sign);
		};
		int latD, latM, lonD, lonM; double latS, lonS;
		dms(lat, latD, latM, latS);
		dms(lon, lonD, lonM, lonS);
		f << latD << ' ' << latM << ' ' << latS << ' ' << lonD << ' ' << lonM << ' ' << lonS << '\n';
	}

	static std::ofstream openOutFile(const std::string& userPath, std::string& outAbsPath)
	{
		namespace fs = std::filesystem;
		fs::path p = userPath.empty() ? fs::path("src/saves") / "track_recorded.txt"
		                              : (fs::path(userPath).has_parent_path()
		                                     ? fs::path(userPath)
		                                     : fs::path("src/saves") / sanitizeName(userPath));
		if (p.extension().empty()) p.replace_extension(".txt");
		try { fs::create_directories(p.parent_path()); } catch (...) {}
		std::error_code ec;
		outAbsPath = fs::absolute(p, ec).string();
		return std::ofstream(p, std::ios::out | std::ios::binary);
	}

	// Save single-pass (centre-line) track
	static bool saveCentreLineLocked(const std::string& userPath)
	{
		if (g_points.size() < 2) return false;
		std::string abs;
		std::ofstream f = openOutFile(userPath, abs);
		if (!f) return false;

		f << std::setprecision(7) << std::fixed;
		if (!g_points.empty())
			f << "#start_norm " << g_points.front().x << ' ' << g_points.front().y << '\n';
		for (const auto& p : g_points) writePointDMS(f, p);

		std::cout << "[TRACK-BUILD] Saved centre-line track to: " << abs << '\n';
		return true;
	}

	// Save dual-edge track as binary .trk2
	static bool saveDualEdgeLocked(const std::string& userPath)
	{
		if (g_left_points.size() < 2 || g_points.size() < 2) return false;

		namespace fs = std::filesystem;
		fs::path p = userPath.empty()
		    ? fs::path("src/saves") / "track_recorded.trk2"
		    : (fs::path(userPath).has_parent_path()
		           ? fs::path(userPath)
		           : fs::path("src/saves") / sanitizeName(userPath));
		p.replace_extension(".trk2");
		try { fs::create_directories(p.parent_path()); } catch (...) {}

		std::ofstream f(p, std::ios::binary);
		if (!f) return false;

		Trk2FileHeader h;
		memcpy(h.magic, "TRK2", 4);
		h.version           = 2;
		h.origin_easting    = g_map_origin.m_origin_meters_easting;
		h.origin_northing   = g_map_origin.m_origin_meters_northing;
		h.origin_zone       = g_map_origin.m_origin_zone_int;
		h.origin_zone_char  = g_map_origin.m_origin_zone_char;
		memset(h.pad, 0, 3);
		h.map_size          = g_map_origin.m_map_size;
		h.left_count        = (uint32_t)g_left_points.size();
		h.right_count       = (uint32_t)g_points.size();

		f.write(reinterpret_cast<const char*>(&h), sizeof(h));
		f.write(reinterpret_cast<const char*>(g_left_points.data()), h.left_count  * sizeof(glm::vec2));
		f.write(reinterpret_cast<const char*>(g_points.data()),      h.right_count * sizeof(glm::vec2));

		std::string absStr; std::error_code ec;
		absStr = fs::absolute(p, ec).string();
		std::cout << "[TRACK-BUILD] Saved .trk2 to: " << absStr << '\n';
		return true;
	}

	// Shared finalize logic (called with lock held)
	static bool finalizeLocked(bool ensureClosed)
	{
		if (!g_active || g_finalized || g_points.size() < 2) return false;
		if (ensureClosed && !isClosedReadyLocked()) return false;
		if (ensureClosed) g_points.push_back(g_start);

		TrackCenterInfo info = calculateTrackCenter(g_points);
		if (info.is_closed)
		{
			recenterTrack(g_points, info);
			g_map_origin.m_origin_meters_easting  -= info.offset.x * MapConstants::MAP_SIZE;
			g_map_origin.m_origin_meters_northing -= info.offset.y * MapConstants::MAP_SIZE;
			try {
				bool northp = g_map_origin.m_origin_zone_char >= 'N';
				GeographicLib::UTMUPS::Reverse(g_map_origin.m_origin_zone_int, northp,
					g_map_origin.m_origin_meters_easting, g_map_origin.m_origin_meters_northing,
					g_map_origin.m_origin_lat_dd, g_map_origin.m_origin_lon_dd);
			} catch (...) {}
		}
		rebuildSmoothLocked();
		g_finalized = true;
		g_active    = false;
		return true;
	}

	// Dual-edge finalize (called with lock held)
	static bool finalizeEdgesLocked()
	{
		if (g_left_points.size() < 2 || g_points.size() < 2) return false;

		size_t nl = g_left_points.size(), nr = g_points.size();
		int n = (int)(nl < nr ? nl : nr);
		if (n > 500) n = 500;
		if (n < 50)  n = 50;

		auto left  = resamplePolyline(g_left_points, n);
		auto right = resamplePolyline(g_points, n);
		alignPolylineDirection(right, left);

		g_left_points = left;
		g_points      = right;

		std::vector<glm::vec2> centres(n);
		for (int i = 0; i < n; ++i)
			centres[i] = (left[i] + right[i]) * 0.5f;

		g_smooth = interpolatePointsWithTangents(centres, g_settings.splinePointsPerSegment);
		if (g_smooth.size() >= 2) { g_sf_p1 = g_smooth[0].position; g_sf_p2 = g_smooth[1].position; }

		g_active    = false;
		g_finalized = true;
		g_phase     = EdgePhase::Done;
		g_auto_save_requested = true;
		return true;
	}
}

namespace TelemetryTrackBuilder
{
	// ── Single-pass API ─────────────────────────────────────────────────────

	void Start(const Settings& settings)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_settings = settings;
		g_active = true; g_finalized = false; g_origin_initialized = false;
		g_auto_save_requested = false;
		g_phase = EdgePhase::Idle;
		g_points.clear(); g_left_points.clear(); g_smooth.clear();
		g_sf_p1 = g_sf_p2 = glm::vec2(0.0f);
		g_len_m = 0.0f;
	}

	void Stop() { Stop(false); }

	void Stop(bool keepPoints)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_active = false; g_finalized = false; g_origin_initialized = false;
		g_auto_save_requested = false;
		g_phase = EdgePhase::Idle;
		if (!keepPoints) { g_points.clear(); g_left_points.clear(); g_smooth.clear(); g_len_m = 0.0f; }
	}

	bool IsActive()    { std::lock_guard<std::mutex> l(g_mutex); return g_active; }
	bool IsFinalized() { std::lock_guard<std::mutex> l(g_mutex); return g_finalized; }

	bool ConsumeAutoSaveRequest()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		bool v = g_auto_save_requested;
		g_auto_save_requested = false;
		return v;
	}

	void OnTelemetryPacket(const TelemetryPacket& packet)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_active || g_finalized) return;
		if (packet.MagicMarker != PACKET_MAGIC_DATA && packet.MagicMarker != PacketMagic::DATA) return;

		g_last_fix_type = packet.fixtype;

		// During Transit the rider is only driving over to the other edge —
		// those positions are not part of the track.
		if (g_phase == EdgePhase::Transit || g_phase == EdgePhase::Review) return;

		const double lat = (double)packet.lat / 1e7;
		const double lon = (double)packet.lon / 1e7;
		ensureOriginLocked(lat, lon);

		double e, n, nx, ny;
		coordinatesToMeters(lat, lon, e, n);
		getCoordinateDifferenceFromOrigin(e, n, nx, ny);
		glm::vec2 pos((float)nx, (float)ny);

		if (g_points.empty())
		{
			g_points.push_back(pos);
			g_start = g_last = pos;
			rebuildSmoothLocked();
			return;
		}

		const float d = distNorm(pos, g_last);
		if (d < g_settings.minPointDistanceNorm) return;

		g_points.push_back(pos);
		g_len_m += d * (float)MapConstants::MAP_SIZE;
		g_last = pos;

		if (g_points.size() % 20 == 0) rebuildSmoothLocked();

		// Centre-line mode: auto-close
		if (g_phase == EdgePhase::Idle && finalizeLocked(true))
			g_auto_save_requested = true;
	}

	bool FinalizeOpenAndSaveTxt(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!finalizeLocked(false)) return false;
		return saveCentreLineLocked(path);
	}

	bool SaveFinalizedAsTxt(const std::string& path)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (!g_finalized) return false;
		return (g_phase == EdgePhase::Done) ? saveDualEdgeLocked(path) : saveCentreLineLocked(path);
	}

	// ── Dual-edge API ───────────────────────────────────────────────────────

	void StartLeftEdge(const Settings& settings)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_settings = settings;
		g_active = true; g_finalized = false; g_origin_initialized = false;
		g_auto_save_requested = false;
		g_phase = EdgePhase::Left;
		g_points.clear(); g_left_points.clear(); g_smooth.clear();
		g_sf_p1 = g_sf_p2 = glm::vec2(0.0f);
		g_start = g_last = glm::vec2(0.0f);
		g_len_m = 0.0f;
		g_last_fix_type = 0;
	}

	bool FinishLeftEdge()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_phase != EdgePhase::Left || g_points.size() < g_settings.minPointsToClose) return false;
		// Store the left edge and pause: points are ignored until the rider
		// reaches the right edge start and presses "Start Right Edge".
		g_left_points = std::move(g_points);
		g_points.clear();
		g_phase = EdgePhase::Transit;
		return true;
	}

	bool StartRightEdge()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_phase != EdgePhase::Transit) return false;
		g_start = g_last = glm::vec2(0.0f);
		g_len_m = 0.0f;
		g_phase = EdgePhase::Right;
		return true;
	}

	bool FinishRightEdge()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_phase != EdgePhase::Right || g_points.size() < g_settings.minPointsToClose) return false;
		// Recording is over; both raw edges stay in the buffers for the
		// review screen. Nothing is finalized or saved yet.
		g_active = false;
		g_phase = EdgePhase::Review;
		return true;
	}

	bool SubmitReviewedEdges(std::vector<glm::vec2> left, std::vector<glm::vec2> right)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_phase != EdgePhase::Review) return false;
		if (left.size() < 2 || right.size() < 2) return false;
		g_left_points = std::move(left);
		g_points      = std::move(right);
		return finalizeEdgesLocked();
	}

	size_t PointCount()   { std::lock_guard<std::mutex> l(g_mutex); return g_points.size(); }
	float  LengthMeters() { std::lock_guard<std::mutex> l(g_mutex); return g_len_m; }
	int    LastFixType()  { std::lock_guard<std::mutex> l(g_mutex); return g_last_fix_type; }

	bool InjectSyntheticEdges(std::vector<glm::vec2> left, std::vector<glm::vec2> right)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (left.size() < 2 || right.size() < 2) return false;
		g_left_points = std::move(left);
		g_points      = std::move(right);
		g_active      = true;
		g_finalized   = false;
		g_phase       = EdgePhase::Right;
		g_origin_initialized = true;
		return finalizeEdgesLocked();
	}

	EdgePhase GetPhase() { std::lock_guard<std::mutex> l(g_mutex); return g_phase; }

	std::vector<glm::vec2> GetLeftEdgeSnapshot()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_left_points;
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

	glm::vec2 GetStartFinishP1() { std::lock_guard<std::mutex> l(g_mutex); return g_sf_p1; }
	glm::vec2 GetStartFinishP2() { std::lock_guard<std::mutex> l(g_mutex); return g_sf_p2; }
}
