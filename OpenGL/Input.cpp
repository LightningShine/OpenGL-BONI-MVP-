#pragma once
#include "Input.h"


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

void Input(std::string cordinate, std::mutex& pointsMutex, std::atomic<bool>& running)
{
	while (running)
	{
		std::getline(std::cin, cordinate);

		if (cordinate == "exit")
		{
			running = false;
			break;
		}
		

	}
};

void CordinatesToDecimalFormat(std::string line)
{
	int lat_deg, lat_min, lon_deg, lon_min;
	double lat_sec, lon_sec;

		std::getline(std::cin, line);

		int result = sscanf_s(line.c_str(),
			"%dø%d'%lf\" %dø%d'%lf\"",
			&lat_deg, &lat_min, &lat_sec,
			&lon_deg, &lon_min, &lon_sec);

		//std::cout << lat_deg << " " << lat_min << " " << lat_sec << "      " << lon_deg << " " << lon_min << " " << lon_sec << "\n";
		float X =lat_deg + ((float)lat_min / 60) + ((float)lat_sec / 3600);
		float Y = lon_deg + ((float)lon_min / 60) + ((float)lon_sec / 3600);

		std::cout << "FlatX " << X << "\n" << "FlatY " << Y << "\n";
}

void CordinatesToUTM_GeographicLib(double lat_deg, double lon_deg, double& easting, double& northing)
{
	using namespace GeographicLib;

	int zone;
	bool northp;

	// 1. Выполняем преобразование
	UTMUPS::Forward(lat_deg, lon_deg, zone, northp, easting, northing);

	// 2. Получаем букву зоны на основе широты
	char zone_letter;
	if (lat_deg >= 84 || lat_deg < -80) {
		// Полярные регионы (UPS)
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
		else zone_letter = 'D'; // -80 до -64
	}

	std::cout << "UTM Coordinates: \n";
	std::cout << "  Easting (X): " << easting << " m\n";
	std::cout << "  Northing (Y): " << northing << " m\n";
	std::cout << "  Zone: " << zone << zone_letter << "\n"; // Например, 38N
}
