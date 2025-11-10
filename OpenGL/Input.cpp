#include "Input.h"

void InputData(std::vector<glm::vec2>& points, std::mutex& pointsMutex, std::atomic<bool>& running)
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

void CordinatesToOpenGLFormat(std::atomic<bool>& running)
{
	std::cout << "Input Earth cordinate is started\n";
	std::string line;

	int lat_deg, lat_min, lon_deg, lon_min;
	double lat_sec, lon_sec;
	while (running)
	{
		std::getline(std::cin, line);

		int result = sscanf_s(line.c_str(),
			"%dø%d'%lf\" %dø%d'%lf\"",
			&lat_deg, &lat_min, &lat_sec,
			&lon_deg, &lon_min, &lon_sec);

		//std::cout << lat_deg << " " << lat_min << " " << lat_sec << "      " << lon_deg << " " << lon_min << " " << lon_sec << "\n";
		lat_degX = lat_deg + ((float)lat_min / 60) + ((float)lat_sec / 3600);
		lon_degY = lon_deg + ((float)lon_min / 60) + ((float)lon_sec / 3600);

		std::cout << "FlatX " << lat_deg + ((float)lat_min / 60) + ((float)lat_sec / 3600) << "\n" << "FlatY " << lon_deg + ((float)lon_min / 60) + ((float)lon_sec / 3600) << "\n";


	}
}

void CordinatesToUTM(std::atomic<bool>& running)
{
	PJ_COORD input_cord; 
	float PI = 3.1415926535897932384626433832795;
	input_cord.lp.lam = (lon_degY * PI)/180;

	PJ_CONTEXT* C = proj_context_create();

	PJ* UTM_def = proj_create_crs_to_crs(C, "EPSG:4326", "+proj=utm +zone=auto +elips=WGS84", nullptr);

	PJ_COORD output_coord = proj_trans(UTM_def, PJ_FWD, input_cord);

	double easting = output_coord.xy.x;
	double northing = output_coord.xy.y;
}
