$ErrorActionPreference = "Stop"

Write-Host "Updating vehicle creation in main.cpp..." -ForegroundColor Green

$file = "OpenGL\src\core\main.cpp"
$content = Get-Content $file -Raw -Encoding UTF8

# Find and replace the T key handler
$old_pattern = '(?s)\tstatic bool wasTPressed = false;.*?wasTPressed = false;\s+\}'
$new_code = @'
	static bool wasTPressed = false;
	if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !wasTPressed)
	{
		wasTPressed = true;
		
		// Guard clauses
		if (!g_is_map_loaded) {
			std::cout << "Cannot create vehicle - map not loaded!" << std::endl;
		} else if (g_smooth_track_points.empty()) {
			std::cout << "Cannot create vehicle - track not interpolated!" << std::endl;
		} else {
			// Create vehicle at origin
			Vehicle new_vehicle;
			int vehicle_id = new_vehicle.m_id;
			
			{
				std::lock_guard<std::mutex> lock(g_vehicles_mutex);
				g_vehicles[vehicle_id] = new_vehicle;
			}
			
			std::cout << "Vehicle #" << vehicle_id << " created - starting simulation" << std::endl;
			
			// Start automatic simulation along track
			simulateVehicleMovement(vehicle_id, g_smooth_track_points);
		}
	}
	if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE)
	{
		wasTPressed = false;
	}
'@

$content = $content -replace $old_pattern, $new_code

$content | Out-File -FilePath $file -Encoding UTF8 -NoNewline
Write-Host "Updated successfully!" -ForegroundColor Green
