#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <sstream>
#include <string>
#include <GeographicLib/UTMUPS.hpp>



void inputOrigin();

void inputDataOpenGL(
    std::vector<glm::vec2>& points,
    std::mutex& points_mutex,
    std::atomic<bool>& running
);

void coordinatesToDecimalFormat(std::string line, double& decimal_lat_deg, double& decimal_lon_deg);

void createOriginDD(double lat_deg, double lon_deg, double& easting_meters, double& northing_meters);

void coordinatesToMeters(double lat_deg, double lon_deg, double& easting_meters, double& northing_meters);

void getCoordinateDifferenceFromOrigin(double CordiateX, double CordinateY, double& normalized_x, double& normalized_y);

void inputDataInCode(std::vector<glm::vec2>& points, std::mutex& points_mutex, std::atomic<bool>& running, double& normalized_x, double& normalized_y);

void chooseInputMode(std::vector<glm::vec2>& points, std::mutex& points_mutex, std::atomic<bool>& running);

void loadTrackFromData(const std::string& data, std::vector<glm::vec2>& points, std::mutex& points_mutex);

class MapOrigin
{
public:
	double m_origin_lat_dd;
	double m_origin_lon_dd;
	double m_origin_meters_easting;
	double m_origin_meters_northing;
	int m_origin_zone_int;
	double m_map_size = 100;
	char m_origin_zone_char;

};

extern MapOrigin g_map_origin;

extern std::atomic<bool> g_is_map_loaded;





