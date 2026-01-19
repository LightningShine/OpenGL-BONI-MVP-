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
#include <UTMUPS.hpp>



void InputOrigin();

void InputDataOpenGL(
    std::vector<glm::vec2>& points,
    std::mutex& pointsMutex,
    std::atomic<bool>& running
);

void CordinatesToDecimalFormat(std::string line, double& dec_lat_deg, double& dec_lon_deg);

void CreateOriginDD(double lat_deg, double lon_deg, double& easting, double& northing);

void DDToMetr(double lat_deg, double lon_deg, double& easting, double& northing);

void CordinateDifirenceFromOrigin(double CordiateX, double CordinateY, double& normalized_x, double& normalized_y);

void InputDatainCode(std::vector<glm::vec2>& points, std::mutex& pointsMutex, std::atomic<bool>& running, double& normalized_x, double& normalized_y);

void ChoseInputMode(std::vector<glm::vec2>& points, std::mutex& pointsMutex, std::atomic<bool>& running);

void LoadTrackFromData(const std::string& data, std::vector<glm::vec2>& points, std::mutex& pointsMutex);

class MapDate
{
public:
	double originDD_lat;
	double originDD_lon;
	double originMetr_est;
	double originMetr_nort;
	int originZone_int;
	double origin_mapsize = 100;
	char originZone_char;

};



