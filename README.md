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


