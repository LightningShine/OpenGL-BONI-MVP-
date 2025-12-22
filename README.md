# OpenGL

🏎️ RaceMap Tracker: High-Precision Geospatial Visualization

Project Overview

RaceMap Tracker is a C++ and OpenGL application designed for the visualization and analysis of race tracks and geographical paths using high-accuracy geospatial data (e.g., from RTK receivers).

The core function of this project is its ability to seamlessly translate absolute geographic coordinates (Latitude/Longitude) into a localized, meter-based 2D coordinate system. This conversion allows for accurate real-time plotting, precise distance calculation, and rendering the map within a normalized OpenGL viewport.

🚀 Key Features

    Real-Time Coordinate Input: The application accepts GPS/GNSS coordinates, supporting both Degrees-Minutes-Seconds (D°M'S") and Decimal Degrees input formats.

    High-Fidelity Geospatial Conversion: Utilizes the GeographicLib C++ library to perform accurate WGS84 to UTM (Universal Transverse Mercator) coordinate transformation.

    Localization & Normalization:

        Localization: Automatically determines the Track Center to offset huge UTM numbers, creating a local origin (0, 0).

        Normalization: Calculates the maximum extent (Max\_Span) to scale all local coordinates into the standard OpenGL range of [-1.0, 1.0].

    OpenGL Visualization: Renders the 2D path using primitives like GL_LINE_STRIP and GL_POINTS.

    Data Persistence (JSON): Stores all necessary track metadata (UTM zone, center coordinates, scaling factor) and absolute UTM track points in a structured JSON file for easy sharing and cross-platform use.

⚙️ Technical Stack and Dependencies

Component	Description
Language Standard	C++17
Graphics	OpenGL (for 2D rendering)
Geodesy	GeographicLib (WGS84 to UTM conversion)
Data Serialization	nlohmann/json (for reading/writing track data)
Linear Algebra	GLM (likely used for vector math and point handling)

Data Transformation Pipeline

The project follows a critical four-step conversion pipeline to ensure accuracy and visual stability:

https://github.com/user-attachments/assets/4bd82d52-cb2b-4506-8cf0-179601198cdf

    Input: D°M'S".

    Geodetic Conversion: WGS84 → Absolute UTM Meters (Easting/Northing) + Zone (38V).

    Localization: PUTM​→PLocal​=PUTM​−CenterUTM​. (Translating to a local 0,0 origin).

    Normalization: PLocal​→POpenGL​=PLocal​×Scale. (Scaling to fit the screen boundaries [-1.0, 1.0]).

💾 Track Data Format (JSON)

The project's JSON file is fully self-describing, containing all the parameters needed to load and render the track correctly on any system without external calibration.
JSON

```
{
  "track_name": "Track Name",
  "projection_zone": "38V",
  "center_easting": 271780.88, // Absolute UTM X of the track center
  "center_northing": 6279715.39, // Absolute UTM Y of the track center
  "max_span_meters": 500.0,    // Largest dimension of the track's bounding box
  "track_points": [
    [0, 0], // Normalizated OpenGL [X, Y] points
    // 
  ]
}
```

---

### 🚨 Problems and Drawbacks (Technical Debt)

* **Raw Pointer Management** — Using raw pointers creates the risk of memory leaks and undefined behavior.
* **Hardcoded Assets Paths** — Hardcoded shader/texture paths make the application non-portable.
* **Lack of Error Handling** — The lack of OpenGL state checks and shader compilation validation makes debugging difficult.
* **Mixed Code Style** — Inconsistent tabulation and mixed camelCase/snake_case make code less readable.
* **Global State Dependency** — Excessive use of global variables or macros instead of encapsulation.
* **Redundant Redraws** — Lack of visibility optimization; everything is drawn every frame, even if it's outside the camera's view. * **Manual Buffer Sync** — lack of automation when updating data in VBO/VAO, leading to errors when changing data structures.

---

### 🛠 Roadmap

* **Smart Pointers Migration** — replace resource management with `std::unique_ptr` and `std::shared_ptr`.
* **Resource Manager** — create a centralized class for loading textures and shaders with caching and relative paths.
* **Modern OpenGL (DSA)** — implement Direct State Access for changing object parameters without constant Bind/Unbind.
* **Logging System** — integrate `spdlog` or similar for outputting errors and render status to the console/file.
* **UBO/SSBO Implementation** — move the transfer of matrices and material data to buffers to reduce the number of draw calls. * **Camera Frustum Culling** — Add a check to see if an object is within the camera frustum to avoid unnecessary GPU load.
* **Coordinate Transformation (GIS)** — Implement integration with GeographicLib to convert coordinates from LLA to the local metric system.
* **Async Data Loading** — Move loading of heavy meshes and textures to a separate thread to avoid UI freezes.
* **GUI Integration** — Enable Dear ImGui for performance monitoring and live editing of scene parameters.
* **Clang-Format Setup** — Add a config for automatic code alignment to a single standard (e.g., LLVM).

---


