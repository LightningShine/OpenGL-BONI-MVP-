// ???? ??? ??????????? ROTATION - ????????? ? main.cpp

// ============================================================================
// ????????? 1: ?????? 700-706
// ============================================================================
// ????:
		// Creating view matrix with camera position and rotation
		glm::mat4 view = glm::mat4(1.0f);
		view = glm::translate(view, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));
		view = glm::rotate(view, glm::radians(camera_rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		
		// Combine projection and view
		glm::mat4 viewProjection = projection * view;

// ???????? ??:
		// VIEW 1: World objects (track, vehicles) - NO rotation
		glm::mat4 view_world = glm::mat4(1.0f);
		view_world = glm::translate(view_world, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));
		glm::mat4 viewProjection_world = projection * view_world;
		
		// VIEW 2: Grid - WITH rotation (visual effect)
		glm::mat4 view_grid = glm::mat4(1.0f);
		view_grid = glm::translate(view_grid, glm::vec3(-camera_position.x, -camera_position.y, 0.0f));
		view_grid = glm::rotate(view_grid, glm::radians(camera_rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		glm::mat4 viewProjection_grid = projection * view_grid;

// ============================================================================
// ????????? 2: ?????? ~733 (vehicles)
// ============================================================================
// ????:
		renderAllVehicles(shader_program, vao, vbo, viewProjection, camera_position, camera_zoom);

// ???????? ??:
		renderAllVehicles(shader_program, vao, vbo, viewProjection_world, camera_position, camera_zoom);
