// ============================================================================
// ?????????? ???????: Vehicle Focus Selection ? ?????????? ???????????? ID
// ???????? ?????? ??? ? processInput() ??????? ? main.cpp
// ============================================================================

	// ============================================================================
	// SYSTEM: Vehicle Focus Selection (P key + numbers)
	// ???????????? ???????????? ID (???????? 12, 345)
	// ============================================================================
	static bool wasPPressed = false;
	static bool isWaitingForVehicleId = false;
	static double focusInputStartTime = 0.0;
	static std::string vehicleIdInput = "";
	static const double FOCUS_INPUT_TIMEOUT = 10.0; // 10 seconds
	
	// P key: Toggle focus mode or reset focus
	if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !wasPPressed)
	{
		wasPPressed = true;
		
		if (isWaitingForVehicleId) {
			// Cancel input mode
			isWaitingForVehicleId = false;
			vehicleIdInput = "";
			std::cout << "[FOCUS] Vehicle selection cancelled" << std::endl;
		}
		else if (g_focused_vehicle_id != -1) {
			// Reset to leader
			g_focused_vehicle_id = -1;
			std::cout << "[FOCUS] Reset to leader tracking" << std::endl;
		}
		else {
			// Start input mode
			isWaitingForVehicleId = true;
			focusInputStartTime = glfwGetTime();
			vehicleIdInput = "";
			std::cout << "[FOCUS] Enter vehicle ID within 10 seconds (digits 0-9, then ENTER)..." << std::endl;
		}
	}
	if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE)
	{
		wasPPressed = false;
	}
	
	// Handle vehicle ID input (multi-digit support)
	if (isWaitingForVehicleId)
	{
		double currentTime = glfwGetTime();
		if (currentTime - focusInputStartTime > FOCUS_INPUT_TIMEOUT) {
			isWaitingForVehicleId = false;
			vehicleIdInput = "";
			std::cout << "[FOCUS] Input timeout - cancelled" << std::endl;
		}
		else {
			// Check for digit keys 0-9
			static bool digitPressed[10] = {false};
			for (int digit = 0; digit <= 9; digit++) {
				int key = GLFW_KEY_0 + digit;
				if (glfwGetKey(window, key) == GLFW_PRESS && !digitPressed[digit]) {
					digitPressed[digit] = true;
					vehicleIdInput += ('0' + digit);
					std::cout << "[FOCUS] Entered: " << vehicleIdInput << std::endl;
				}
				if (glfwGetKey(window, key) == GLFW_RELEASE) {
					digitPressed[digit] = false;
				}
			}
			
			// Check for ENTER key to confirm selection
			static bool enterPressed = false;
			if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterPressed) {
				enterPressed = true;
				
				if (!vehicleIdInput.empty()) {
					int vehicleId = std::stoi(vehicleIdInput);
					
					// Check if vehicle exists
					std::lock_guard<std::mutex> lock(g_vehicles_mutex);
					if (g_vehicles.find(vehicleId) != g_vehicles.end()) {
						g_focused_vehicle_id = vehicleId;
						isWaitingForVehicleId = false;
						std::cout << "[FOCUS] Now tracking Vehicle #" << vehicleId << std::endl;
					}
					else {
						isWaitingForVehicleId = false;
						std::cout << "[FOCUS] Vehicle #" << vehicleId << " does not exist" << std::endl;
					}
					vehicleIdInput = "";
				}
			}
			if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_RELEASE) {
				enterPressed = false;
			}
		}
	}
