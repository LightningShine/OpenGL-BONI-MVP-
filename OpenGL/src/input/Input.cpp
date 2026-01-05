#pragma once
#include "Input.h"
#include "../rendering/Interpolation.h"

JsonInput jsonClass;

void ChoseInputMode(std::vector<glm::vec2>& points, std::mutex& pointsMutex, std::atomic<bool>& running)
{
	start:
	std::cout << "1. Create New Map" << "\n";
	std::cout << "2. Load Existing Map" << "\n";
	std::cout << "3. Input cordinate manual ( Normalizated cordinat )" << "\n";
	int input_mode;
	std::cout << "Select input mode: ";
	std::cin.clear();
	std::cin >> input_mode;
	std::string line;
	double dec_lat_deg, dec_lon_deg, easting, northing, normalized_x, normalized_y;
	std::vector<glm::vec2> smoothedData;
	switch (input_mode)
	{
		case 1:
			std::cout << "Create New Map Started ( Input your cordibate in DMS format )\n";
			InputOrigin();
			while (true)
			{
				std::getline(std::cin, line);
				if (line == "exit")
				{
					break;
				}
				CordinatesToDecimalFormat(line, dec_lat_deg, dec_lon_deg);
				CordinateToMetersUTM(dec_lat_deg, dec_lon_deg, easting, northing);
				CordinateDifirenceFromOrigin(easting, northing, 100.0, normalized_x, normalized_y);
				InputDatainCode(points, pointsMutex, running, normalized_x, normalized_y);

				{
					std::lock_guard<std::mutex> lock(pointsMutex); // Neded to explain
					if (points.back() == points.front() && points.size() != 1)
					{
						std::cout << "Map closed.\n";
						break;
					}
				}
			}

			break;
		case 2:
			std::cout << "Load Existing Map Selected\n";
			break;
		case 3:
			InputDataOpenGL(points, pointsMutex, running);
			break;
		default:
			std::cout << "Invalid input mode selected\n";
			goto start;
	}

}


void InputDataOpenGL(std::vector<glm::vec2>& points, std::mutex& pointsMutex, std::atomic<bool>& running)
{
	std::cout << "Input date is started\n";

	std::string line;
	while (running)
	{
		std::getline(std::cin, line);
		
		if(line == "exit")
		{
			running = false;
			break;
		}

		std::stringstream ss(line);
		float x, y;
		
		if (ss >> x >> y)
		{
			{
				std::lock_guard<std::mutex> lock(pointsMutex);
				points.push_back(glm::vec2{x, y});
				std::cout << "Received point: (" << x << ", " << y << ")\n";
			}
		}
		else
		{
			std::cout << "Invalid input. Please enter two float values separated by space.\n";
		}
	
	}

}

void InputOrigin()
{
		std::string cordinate;
		std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // Очищаем буфер
		std::getline(std::cin, cordinate); // Ingonoring it
		double dec_lat_deg, dec_lon_deg;
		CordinatesToDecimalFormat(cordinate, dec_lat_deg, dec_lon_deg);
		double easting, northing;
		CordinatesToUTM_GeographicLib(dec_lat_deg, dec_lon_deg, easting, northing);
	
};

void CordinatesToDecimalFormat(std::string line, double &dec_lat_deg, double& dec_lon_deg)
{
	int lat_deg, lat_min, lon_deg, lon_min;
	double lat_sec, lon_sec;

	// Replace non-numeric characters with space (except dot and minus)
	std::string tempLine = line;
	for (char& c : tempLine) {
		if (c != '.' && c != '-' && (c < '0' || c > '9')) {
			c = ' ';
		}
	}

	std::stringstream ss(tempLine);
	if (ss >> lat_deg >> lat_min >> lat_sec >> lon_deg >> lon_min >> lon_sec)
	{
		dec_lat_deg = lat_deg + (lat_min / 60.0f) + (lat_sec / 3600.0f);
		dec_lon_deg = lon_deg + (lon_min / 60.0f) + (lon_sec / 3600.0f);
	}
	else
	{
		// Fallback or error handling
		dec_lat_deg = 0;
		dec_lon_deg = 0;
	}
}

void LoadTrackFromData(const std::string& data, std::vector<glm::vec2>& points, std::mutex& pointsMutex)
{
	std::stringstream ss(data);
	std::string line;
	bool firstPoint = true;

	{
		std::lock_guard<std::mutex> lock(pointsMutex);
		points.clear();
	}

	while (std::getline(ss, line))
	{
		if (line.empty()) continue;
		
		// Simple check if line contains digits
		bool hasDigit = false;
		for (char c : line) { if (isdigit(c)) { hasDigit = true; break; } }
		if (!hasDigit) continue;

		double dec_lat_deg, dec_lon_deg;
		CordinatesToDecimalFormat(line, dec_lat_deg, dec_lon_deg);

		double easting, northing;
		if (firstPoint)
		{
			CordinatesToUTM_GeographicLib(dec_lat_deg, dec_lon_deg, easting, northing);
			firstPoint = false;
		}
		else
		{
			CordinateToMetersUTM(dec_lat_deg, dec_lon_deg, easting, northing);
		}

		double normalized_x, normalized_y;
		CordinateDifirenceFromOrigin(easting, northing, 100.0, normalized_x, normalized_y);

		{
			std::lock_guard<std::mutex> lock(pointsMutex);
			points.push_back(glm::vec2(normalized_x, normalized_y));
		}
	}
	std::cout << "Track loaded with " << points.size() << " points." << std::endl;
}

void CordinatesToUTM_GeographicLib(double lat_deg, double lon_deg, double& easting, double& northing)
{
	using namespace GeographicLib;

	int zone;
	bool northp;

	// Cordinate converter
	UTMUPS::Forward(lat_deg, lon_deg, zone, northp, easting, northing);

	// 2Getting the char of zone
	char zone_letter;
	if (lat_deg >= 84 || lat_deg < -80) {
		zone_letter = (northp) ? 'Z' : 'A';
	}
	else {
		// Стандартные UTM зоны
		if (lat_deg >= 72) zone_letter = 'X';
		else if (lat_deg >= 64) zone_letter = 'W';
		else if (lat_deg >= 56) zone_letter = 'V';
		else if (lat_deg >= 48) zone_letter = 'U';
		else if (lat_deg >= 40) zone_letter = 'T';
		else if (lat_deg >= 32) zone_letter = 'S';
		else if (lat_deg >= 24) zone_letter = 'R';
		else if (lat_deg >= 16) zone_letter = 'Q';
		else if (lat_deg >= 8) zone_letter = 'P';
		else if (lat_deg >= 0) zone_letter = 'N';
		else if (lat_deg >= -8) zone_letter = 'M';
		else if (lat_deg >= -16) zone_letter = 'L';
		else if (lat_deg >= -24) zone_letter = 'K';
		else if (lat_deg >= -32) zone_letter = 'J';
		else if (lat_deg >= -40) zone_letter = 'H';
		else if (lat_deg >= -48) zone_letter = 'G';
		else if (lat_deg >= -56) zone_letter = 'F';
		else if (lat_deg >= -64) zone_letter = 'E';
		else zone_letter = 'D'; // -80 to -64
	}

	std::cout << "UTM Coordinates: \n";
	std::cout << "  Easting (X): " << easting << " m\n";
	std::cout << "  Northing (Y): " << northing << " m\n";
	std::cout << "  Zone: " << zone << zone_letter << "\n"; 

	
	jsonClass.input_latitude = lat_deg;
	jsonClass.input_longitude = lon_deg;
	jsonClass.easting = easting;
	jsonClass.northing = northing;
	jsonClass.zone = zone;
	jsonClass.zone_letter = zone_letter;

}

void CordinateToMetersUTM(double lat_deg, double lon_deg, double &easting, double &northing)
{
	using namespace GeographicLib;


	int zone;
	bool northp;

	// Cordinate converter
	UTMUPS::Forward(lat_deg, lon_deg, zone, northp, easting, northing);

}

void CordinateDifirenceFromOrigin(double CordiateX, double CordinateY, double MAP_SIZE, double &normalized_x, double &normalized_y)
{

	double diff_easting = CordiateX - jsonClass.easting;
	double diff_northing = CordinateY - jsonClass.northing;

	normalized_x = (diff_easting / MAP_SIZE);
	normalized_y = (diff_northing / MAP_SIZE);

}

void InputDatainCode(std::vector<glm::vec2>& points, std::mutex& pointsMutex, std::atomic<bool>& running, double &normalized_x, double &normalized_y)
{
	{
		std::lock_guard<std::mutex> lock(pointsMutex);
		glm::vec2 newPoint(normalized_x, normalized_y);

		if (!points.empty()) {
			float dist = glm::distance(points.back(), newPoint);
			// 0.001f - it is neear to 10 cm with MAP_SIZE 100;
			// If car stay or move slowly, do not 
			if (dist < 0.001f) return;
		}

		points.push_back(glm::vec2{ normalized_x, normalized_y });
		std::cout << "Received point: (" << normalized_x << ", " << normalized_y << ")\n";
	}
}
