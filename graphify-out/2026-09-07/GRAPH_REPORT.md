# Graph Report - OpenGL-BONI-MVP-  (2026-09-07)

## Corpus Check
- Large corpus: 607 files · ~1,042,290 words. Semantic extraction will be expensive (many Claude tokens). Consider running on a subfolder.

## Summary
- 1996 nodes · 3527 edges · 118 communities (96 shown, 6 thin omitted)
- Extraction: 90% EXTRACTED · 10% INFERRED · 0% AMBIGUOUS · INFERRED: 358 edges (avg confidence: 0.84)
- Token cost: 116,033 input · 0 output

## Community Hubs (Navigation)
- UI Facade State
- Track Spline Interpolation
- CSV Telemetry Import
- Track Review Editor
- Vehicle Telemetry State
- Synthetic Telemetry Simulator
- Telemetry Export MoTeC VBO
- Architecture Rules and Review Findings
- Device Registry Settings
- App Bootstrap and DPI Scaling
- Track Recorder File Format
- Console Log Capture
- Dual-Edge Track Builder
- PRO Track Map Panel
- World Snapshot Vehicle View
- UI Modals and Fonts
- Vehicle Interpolation Buffers
- Lap and Reference Trace Types
- PRO Graphs Channel Traces
- PRO Panel Window Renderers
- Race Manager Session State
- PRO View Lap Pinning
- HUD Overlay Elements
- Simulation Server Telemetry
- PRO Sidebar Panel Menu
- Race Mode and Phase Display
- Replay Player Transport
- Track Projection Geometry
- PRO Track Report Heatmap
- Telemetry Log Reader
- Server Auth Packets
- ESP32 Serial COM Discovery
- PRO G-Force Bars
- PRO Graph Channel Definitions
- Input Coordinate Conversion
- Race Status Bar Layout
- Telemetry Log Writer
- Vehicle State Packet
- Module Header Surface
- Race Manager Results
- Vehicle Rendering Geometry
- PRO Events Timeline
- Server Broadcast and Distances
- Vehicle Standings Struct
- PRO Relative Gaps
- Sector Timing Findings
- Replay Seek and Journal Build
- Snapshot and Locking Rules
- Track Server Client Status
- UI Frame Lifecycle
- Race Flag Colors
- Replay Status Struct
- Recent Files and Replay Open
- Embedded Track Trailer
- Map Origin Packet
- Race Display Wrapper
- Vehicle Render State
- Journal Memory Findings
- Trk2 File Header
- Telemetry Log Header
- Replay Open and Track Lookup
- Vehicle Journal Store
- Lap and Leader Time Deltas
- PRO Sectors Panel
- Race Status Bar Drawing
- Telemetry Log File Naming
- Map Points Packet
- Track Chunk Packet
- Session Start and Reset
- Status Bar Time Formatting
- Track Apply and Recent Files
- World Snapshot Publish
- Input Map Origin
- Track Server JSON Protocol
- Telemetry Track Builder Settings
- Track Recorder Settings
- Telemetry Export UI Actions
- Journal Build Progress
- Lap Clock UTC Timing
- Code Style Conventions
- AppPaths Data Directories
- Track Trailer Header
- Pending Origin Struct
- Track Map Sector Snapshot
- Start-Finish Line Crossing
- Graphify Knowledge Graph Tooling
- Telemetry Log Stats
- Replay Keyframes
- Vehicle Name Rendering
- Car Lap Session Samples
- Frame Arrival Stats
- Track Server Client Loop
- Include Root and Module Layout
- Race Data Packet
- Lap Time Smoother
- Vehicle Time Sync
- Log Header Write Helpers
- Telemetry Ingest Start
- Settings Panel Header
- Dead Constants Finding
- Spdlog Level Policy
- Track Recording From Telemetry

## God Nodes (most connected - your core abstractions)
1. `UI` - 124 edges
2. `Vehicle` - 75 edges
3. `RaceManager` - 64 edges
4. `VehicleView` - 44 edges
5. `RaceStatusBar` - 40 edges
6. `ProContext` - 32 edges
7. `LapInfo` - 32 edges
8. `impl_` - 31 edges
9. `Row` - 26 edges
10. `PanelState` - 26 edges

## Surprising Connections (you probably didn't know these)
- `A4: CSV import time origin can stay zero` --references--> `convert_csv_to_replay()`  [EXTRACTED]
  docs/CODE_REVIEW.md → OpenGL/src/logging/CsvImport.cpp
- `Race modes (Circuit / Time Attack / Rally)` --references--> `ModeManager`  [INFERRED]
  README.md → OpenGL/src/racing/ModeManager.h
- `Race phases (Practice -> Race -> Finishing -> Finished)` --references--> `ModeManager`  [INFERRED]
  README.md → OpenGL/src/racing/ModeManager.h
- `C5: sample rate declared in two places` --references--> `RaceManager`  [EXTRACTED]
  docs/CODE_REVIEW.md → OpenGL/src/racing/RaceManager.h
- `Invariant: timing anchored to source timestamps, not frames` --rationale_for--> `RaceManager`  [EXTRACTED]
  docs/CODE_REVIEW.md → OpenGL/src/racing/RaceManager.h

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Three data sources converge on a single telemetry ingest path** — opengl_claude_com_serial_source, opengl_claude_track_server_source, opengl_claude_simulation_source, opengl_claude_telemetry_pipeline, docs_code_review_single_ingest_path [EXTRACTED 1.00]
- **PRO analysis panels are path-dependent across a replay session** — docs_code_review_b1_events_from_observation, docs_code_review_b2_analysis_lap_divergence, docs_code_review_b3_sector_rollback_asymmetry, docs_code_review_b5_panel_state_survives_replay_change, docs_code_review_vehicle_journal [INFERRED 0.85]
- **Race status bar: display wrapper, bar, flags and phase source** — docs_notes_race_display_implementation_racedisplay, docs_notes_race_display_implementation_racestatusbar, docs_notes_race_display_implementation_raceflags, docs_notes_race_display_implementation_responsive_scaling, docs_notes_race_display_implementation_modemanager_integration [EXTRACTED 1.00]

## Communities (118 total, 6 thin omitted)

### Community 0 - "UI Facade State"
Cohesion: 0.03
Nodes (73): NetworkingModalMode, mutex, time_point, vec2, vector, UI, m_allowPrototypeToast, m_appliedMonitorScale (+65 more)

### Community 1 - "Track Spline Interpolation"
Cohesion: 0.07
Nodes (62): GLenum, alignPolylineDirection(), calculateTrackCenter(), SplinePoint, vec2, vector, douglasPeuckerRecursive(), filterPointsByDistance() (+54 more)

### Community 2 - "CSV Telemetry Import"
Cohesion: 0.06
Nodes (57): CsvSpeedUnit, D6: CSV import holds the whole file as strings, Fn, convert_csv_to_replay(), CsvChannel, CsvTimeFormat, path, string (+49 more)

### Community 3 - "Track Review Editor"
Cohesion: 0.07
Nodes (60): ApplyProportionalMove(), CancelDrag(), ImDrawList, ImFont, ImU32, ImVec2, vec2, vector (+52 more)

### Community 4 - "Vehicle Telemetry State"
Cohesion: 0.03
Nodes (58): array, map, SECTOR_COUNT, time_point, vector, Vehicle, bestlapID, laps (+50 more)

### Community 5 - "Synthetic Telemetry Simulator"
Cohesion: 0.06
Nodes (51): isRealDataCaptureRunning(), replay_is_active(), append_packet(), build_path(), build_synthetic_stream(), CornerAhead, curvature, distance_m (+43 more)

### Community 6 - "Telemetry Export MoTeC VBO"
Cohesion: 0.06
Nodes (53): A3: telemetry export does not survive UTC midnight, A4: CSV import time origin can stay zero, ExportSamples, build_row(), build_rows(), Channel, csv_name, format (+45 more)

### Community 7 - "Architecture Rules and Review Findings"
Cohesion: 0.04
Nodes (48): A2: GPS fix type labelled against the protocol, C1: two locking idioms on one mutex, C3: backward seek holds the vehicles mutex, forward seek does not, D5: settings written to disk on every wheel click, Recommended fix order (A2, A1, A3/A4, B2, C1, B1/B5), Review severity scale A/B/C/D, Invariant: one ingest path for all sources, VehiclesLock (reentrant bulk-section lock) (+40 more)

### Community 8 - "Device Registry Settings"
Cohesion: 0.07
Nodes (43): Device registry (SQLite devices.db), sqlite3 required for both vcpkg triplets, string, vector, DeviceInfo, device_id, group, last_seen_unix (+35 more)

### Community 9 - "App Bootstrap and DPI Scaling"
Cohesion: 0.07
Nodes (36): GLFWmonitor, AppContext, points, points_mutex, ui, zoom, GLuint, mutex (+28 more)

### Community 10 - "Track Recorder File Format"
Cohesion: 0.06
Nodes (41): TrackDataHeader, magic_marker, origin_easting, origin_lat, origin_lon, origin_northing, origin_zone, origin_zone_char (+33 more)

### Community 11 - "Console Log Capture"
Cohesion: 0.08
Nodes (32): int_type, ConsoleLogSession, impl_, err_buffer, file, original_err, original_out, out_buffer (+24 more)

### Community 12 - "Dual-Edge Track Builder"
Cohesion: 0.09
Nodes (32): EdgePhase, ofstream, Settings, SplinePoint, string, TelemetryPacket, vec2, vector (+24 more)

### Community 13 - "PRO Track Map Panel"
Cohesion: 0.07
Nodes (38): CardHeight(), colorFor(), CompareRow, active, delta, hasDelta, hasLapDelta, lap (+30 more)

### Community 14 - "World Snapshot Vehicle View"
Cohesion: 0.06
Nodes (35): SECTOR_COUNT, shared_ptr, string, vec3, VehicleView, acceleration, apply_track_render_offset, best_lap_id (+27 more)

### Community 15 - "UI Modals and Fonts"
Cohesion: 0.12
Nodes (32): AddDashedRect(), CsvChannel, ImDrawList, ImFont, ImU32, ImVec2, csv_column_combo(), _legacy_pro_view() (+24 more)

### Community 16 - "Vehicle Interpolation Buffers"
Cohesion: 0.09
Nodes (28): deque, map, mutex, VehicleBuffer, Cleanup, GetBracketingSnapshots, mutex, snapshots (+20 more)

### Community 17 - "Lap and Reference Trace Types"
Cohesion: 0.07
Nodes (32): array, SECTOR_COUNT, string, vector, RefTrace, at, at_time, lap_time (+24 more)

### Community 18 - "PRO Graphs Channel Traces"
Cohesion: 0.11
Nodes (30): apply_reference(), channel_value(), copy_samples(), function, vector, LapTrace, at_playhead, found (+22 more)

### Community 19 - "PRO Panel Window Renderers"
Cohesion: 0.11
Nodes (25): ImGuiWindowFlags, ImVec2, RenderChannelsWindow(), ImVec2, RenderEventsWindow(), ImVec2, RenderGForceWindow(), ImVec2 (+17 more)

### Community 20 - "Race Manager Session State"
Cohesion: 0.07
Nodes (30): milliseconds, map, mutex, SessionState, shared_ptr, time_point, vector, RaceManager (+22 more)

### Community 21 - "PRO View Lap Pinning"
Cohesion: 0.14
Nodes (25): ImVec2, RenderLapListWindow(), AnalysisLap(), AnalysisVehicle(), ComparePin(), shared_ptr, string, FlushPanelScales() (+17 more)

### Community 22 - "HUD Overlay Elements"
Cohesion: 0.11
Nodes (19): MapOrigin, vec2, ImFont, MapOrigin, UIElements, drawCompass, drawLapTimer, drawLeaderboard (+11 more)

### Community 23 - "Simulation Server Telemetry"
Cohesion: 0.13
Nodes (25): packets_belong_to_loaded_track(), allocateRaceIdLocked(), calculateTrackProgressFromPosition(), convertNormalizedToGPS(), TelemetryPacket, vec2, createTelemetryPacket(), createVehicleStatePacket() (+17 more)

### Community 24 - "PRO Sidebar Panel Menu"
Cohesion: 0.14
Nodes (25): anyVisible(), ImVec2, string, defaultVisible(), drawFlyout(), FlushPanelSettings(), flushVis(), flyoutSize() (+17 more)

### Community 25 - "Race Mode and Phase Display"
Cohesion: 0.10
Nodes (15): Automatic phase detection from ModeManager, RaceDisplay wrapper, RaceFlags / FlagColor, RaceStatusBar, Responsive scaling from a 1600x900 reference, SessionState, string, ModeManager (+7 more)

### Community 26 - "Replay Player Transport"
Cohesion: 0.09
Nodes (10): capture_keyframe_if_due(), function, vector, FeedingScope, index_for_offset_ms(), player_loop(), replay_apply_pending_seek(), replay_journal_vehicles() (+2 more)

### Community 27 - "Track Projection Geometry"
Cohesion: 0.16
Nodes (22): SplinePoint, vec2, vector, Geometry, build, cumulative_distances, points, segment_count (+14 more)

### Community 28 - "PRO Track Report Heatmap"
Cohesion: 0.12
Nodes (22): ColorMode, ImDrawList, ImU32, ImVec2, vec2, vector, heatColor(), ModeItem (+14 more)

### Community 29 - "Telemetry Log Reader"
Cohesion: 0.19
Nodes (20): condition_variable, atomic, Impl, unique_ptr, read_utc_ms(), records_end_offset(), TelemetryLogReader, duration_ms (+12 more)

### Community 30 - "Server Auth Packets"
Cohesion: 0.11
Nodes (14): mutex, map, AuthPacket, magic_marker, password, AuthResponsePacket, attempts_remaining, is_authenticated (+6 more)

### Community 31 - "ESP32 Serial COM Discovery"
Cohesion: 0.15
Nodes (22): closeSerialNoThrow(), comDiscoveryThreadWorker(), ComPortInfo, description, port, string, vector, enumerateComPortsWindows() (+14 more)

### Community 32 - "PRO G-Force Bars"
Cohesion: 0.13
Nodes (22): bar_color(), ImDrawList, ImU32, ImVec2, draw_track(), read_g(), Reading, has_vehicle (+14 more)

### Community 33 - "PRO Graph Channel Definitions"
Cohesion: 0.10
Nodes (19): ChannelId, Channel, bipolar, color, format, id, key, label (+11 more)

### Community 34 - "Input Coordinate Conversion"
Cohesion: 0.26
Nodes (21): chooseInputMode(), coordinatesToDecimalFormat(), coordinatesToMeters(), atomic, mutex, string, vec2, vector (+13 more)

### Community 35 - "Race Status Bar Layout"
Cohesion: 0.10
Nodes (21): time_point, RaceStatusBar, BASE_BAR_HEIGHT, BASE_BAR_WIDTH, BASE_FLAG_SIZE, BASE_TEXT_SCALE, m_barHeight, m_barWidth (+13 more)

### Community 36 - "Telemetry Log Writer"
Cohesion: 0.10
Nodes (20): mutex, ofstream, impl_, dropped, first_utc_ms, flushing, last_utc_ms, mutex (+12 more)

### Community 37 - "Vehicle State Packet"
Cohesion: 0.11
Nodes (20): VehicleStatePacket, best_lap_time, completed_laps, current_lap_number, current_lap_time, has_started_first_lap, heading, is_leader (+12 more)

### Community 38 - "Module Header Surface"
Cohesion: 0.16
Nodes (6): string, array, vector, ImFont, Render(), RenderRaceMenu()

### Community 39 - "Race Manager Results"
Cohesion: 0.12
Nodes (17): string, vec2, BuildResultsText, CalculateDistanceFromStart, GetAutoStopLaps, GetAutoStopSeconds, GetLeaderLapCount, GetStartFinishLine (+9 more)

### Community 40 - "Vehicle Rendering Geometry"
Cohesion: 0.19
Nodes (17): getTrackRenderOffset(), GLuint, mat4, TelemetryPacket, vec2, vec3, vector, generateCircle() (+9 more)

### Community 41 - "PRO Events Timeline"
Cohesion: 0.19
Nodes (18): buildFromJournal(), ImU32, string, vector, detectEvents(), elapsedSinceStart(), EvtState, bestLap (+10 more)

### Community 42 - "Server Broadcast and Distances"
Cohesion: 0.15
Nodes (16): mt19937, BroadcastTelemetryToClients(), BroadcastVehicleStateToClients(), TelemetryPacket, calculateCumulativeDistances(), pair, SplinePoint, vector (+8 more)

### Community 43 - "Vehicle Standings Struct"
Cohesion: 0.12
Nodes (16): VehicleStanding, bestLapTime, completedLaps, currentLapNumber, currentLapTime, deltaTimeToBest, deltaTimeToLeader, distanceFromStart (+8 more)

### Community 44 - "PRO Relative Gaps"
Cohesion: 0.14
Nodes (16): carColor(), ImU32, ImVec2, string, vec3, fmtGap(), RelCar, color (+8 more)

### Community 45 - "Sector Timing Findings"
Cohesion: 0.13
Nodes (15): A1: SECTORS panel still computes sectors from the sample log, Pro::AnalysisLap (operator-selected lap), B2: TRACK REPORT and SECTORS ignore the operator-selected lap, D3: SECTORS scans all vehicles and all samples every frame, Invariant: timing anchored to source timestamps, not frames, map, GetVehicleLaps, GetVehicleLapsCopy (+7 more)

### Community 46 - "Replay Seek and Journal Build"
Cohesion: 0.19
Nodes (16): B1: event log is built by observation, not from the recording, B3: laps roll back on seek, sectors do not, B5: panel state survives a change of recording, apply_seek(), feed_range(), journal_build_begin(), journal_build_finish(), journal_build_step() (+8 more)

### Community 47 - "Snapshot and Locking Rules"
Cohesion: 0.12
Nodes (16): C4: LapData::telemetryPoints is a dead field copied everywhere, D1: snapshot copies every lap of every vehicle each frame, Invariant: panels read the published snapshot, Prefer a queue to a mutex, Shared data must be protected, pending-consume hand-off pattern, adminResponses(), consumePendingTrack() (+8 more)

### Community 48 - "Track Server Client Status"
Cohesion: 0.13
Nodes (6): currentFlag(), monotonicMs(), recordFrameStat(), role(), sendCommand(), setConnectParams()

### Community 49 - "UI Frame Lifecycle"
Cohesion: 0.14
Nodes (9): B4: one frame after a seek, position and timing disagree, main(), vec2, ImFont, ImGuiContext, MapOrigin, ModeManager, EndFrame (+1 more)

### Community 50 - "Race Flag Colors"
Cohesion: 0.20
Nodes (5): FlagColor, RaceFlags, m_leftFlag, m_rightFlag, ModeManager

### Community 51 - "Replay Status Struct"
Cohesion: 0.15
Nodes (13): replay_status(), ReplayStatus, active, date_yyyymmdd, duration_ms, file_name, first_utc_ms, paused (+5 more)

### Community 52 - "Recent Files and Replay Open"
Cohesion: 0.38
Nodes (13): string, isSamePath(), normalizeTrackPath(), readRecentList(), BeginFrame, HandleDroppedFile, LoadRecentFiles, NoteRecentFile (+5 more)

### Community 53 - "Embedded Track Trailer"
Cohesion: 0.17
Nodes (12): vector, embedded_track_format, impl_, elapsed_ms, header, records, track_bytes, track_file_name (+4 more)

### Community 54 - "Map Origin Packet"
Cohesion: 0.17
Nodes (12): MapDataPacket, magic_marker, map_size, origin_lat_dd, origin_lon_dd, origin_meters_easting, origin_meters_northing, origin_zone_char (+4 more)

### Community 55 - "Race Display Wrapper"
Cohesion: 0.24
Nodes (9): ModeManager, RaceDisplay, Initialize, m_screenHeight, m_screenWidth, m_statusBar, OnScreenResized, Render (+1 more)

### Community 56 - "Vehicle Render State"
Cohesion: 0.17
Nodes (12): string, vec3, VehicleRenderState, apply_track_render_offset, color, heading, id, is_leader (+4 more)

### Community 57 - "Journal Memory Findings"
Cohesion: 0.20
Nodes (11): C2: replay_journal returns a raw pointer into a mutating map, C6: journal warm-up blocks the frame with no sign of life, C7: journal duplicates vehicle history in memory, D2: LAP LIST copies the lap list and immediately discards it, Invariant: keyframes without sample history, VehicleJournal (replay recording journal), Explicit ownership in design, OpenGL calls only from the context thread (+3 more)

### Community 58 - "Trk2 File Header"
Cohesion: 0.18
Nodes (11): Trk2FileHeader, left_count, magic, map_size, origin_easting, origin_northing, origin_zone, origin_zone_char (+3 more)

### Community 59 - "Telemetry Log Header"
Cohesion: 0.18
Nodes (11): TelemetryLogHeader, first_utc_ms, last_utc_ms, magic, record_count, record_size, source, track_embedded (+3 more)

### Community 60 - "Replay Open and Track Lookup"
Cohesion: 0.35
Nodes (11): clear_error(), path, string, fail(), find_track_by_name(), materialize_embedded_track(), replay_last_error(), replay_open() (+3 more)

### Community 61 - "Vehicle Journal Store"
Cohesion: 0.18
Nodes (11): shared_ptr, map, string, vector, replay_journal(), VehicleJournal, best_lap_id, best_lap_time (+3 more)

### Community 62 - "Lap and Leader Time Deltas"
Cohesion: 0.27
Nodes (10): vector, GetStandings, GetStandingsInternal, GetVehicleLapDelta, GetVehicleLeaderDelta, CalculateLapTimeDiff(), CalculateLapTimeDiffInternal(), CalculateLeaderTimeDiff() (+2 more)

### Community 63 - "PRO Sectors Panel"
Cohesion: 0.24
Nodes (10): array, ImVec2, SECTOR_COUNT, lapZoneTimes(), RenderSectorsWindow(), SessionBestZones, time, valid (+2 more)

### Community 64 - "Race Status Bar Drawing"
Cohesion: 0.27
Nodes (10): ModeManager, DrawBorderedRectangle, DrawRectangle, DrawText, Initialize, OnScreenResized, RaceStatusBar::RaceStatusBar(), RecalculateDimensions (+2 more)

### Community 65 - "Telemetry Log File Naming"
Cohesion: 0.31
Nodes (9): path, string, TelemetryLogSource, make_log_file_name(), sanitize_track_name(), writer_loop, track_format_of(), verify_telemetry_log() (+1 more)

### Community 66 - "Map Points Packet"
Cohesion: 0.20
Nodes (10): MapPointsPacket, magic_marker, num_points, points, sequence_number, server_timestamp, total_packets, TrackPoint (+2 more)

### Community 67 - "Track Chunk Packet"
Cohesion: 0.20
Nodes (10): TrackChunkPacket, chunk_index, magic_marker, points, points_in_chunk, TrackPointPacket, tangent_x, tangent_y (+2 more)

### Community 68 - "Session Start and Reset"
Cohesion: 0.27
Nodes (8): InvalidateStandingsCache, ResetSession, SessionState, RaceManager::GetSessionState(), RaceManager::ResetMap(), RaceManager::ResetSession(), RaceManager::StartSession(), RaceManager::StopSession()

### Community 69 - "Status Bar Time Formatting"
Cohesion: 0.24
Nodes (6): string, FormatTime, GetSessionTimeString, GetSystemTimeString, ModeManager, RenderRaceStatusBar

### Community 70 - "Track Apply and Recent Files"
Cohesion: 0.31
Nodes (10): applyTrackData(), applyTrackFile(), mutex, vec2, vector, string, RecentFile, name (+2 more)

### Community 71 - "World Snapshot Publish"
Cohesion: 0.31
Nodes (8): shared_ptr, current(), find(), publish(), Snapshot, revision, standings, vehicles

### Community 72 - "Input Map Origin"
Cohesion: 0.22
Nodes (8): MapOrigin, m_map_size, m_origin_lat_dd, m_origin_lon_dd, m_origin_meters_easting, m_origin_meters_northing, m_origin_zone_char, m_origin_zone_int

### Community 73 - "Track Server JSON Protocol"
Cohesion: 0.42
Nodes (9): telemetryGetRaceIdForPrototype(), string, handleMessage(), handleState(), jsonNumber(), jsonPointArray(), jsonString(), pushAdminResponse() (+1 more)

### Community 74 - "Telemetry Track Builder Settings"
Cohesion: 0.22
Nodes (8): MapOrigin, Settings, closeRadiusNorm, minLoopLengthMeters, minPointDistanceNorm, minPointsToClose, splinePointsPerSegment, SplinePoint

### Community 75 - "Track Recorder Settings"
Cohesion: 0.22
Nodes (8): MapOrigin, Settings, closeRadiusNorm, minLoopLengthMeters, minPointDistanceNorm, minPointsToClose, pointsPerSegment, SplinePoint

### Community 76 - "Telemetry Export UI Actions"
Cohesion: 0.25
Nodes (8): ExportFormat, HWND, collect_export_samples(), map, export_labels(), export_vehicle_id(), open_csv_import(), run_telemetry_export()

### Community 77 - "Journal Build Progress"
Cohesion: 0.25
Nodes (8): time_point, JournalBuild, from, index, next_update_ms, running, started, total

### Community 78 - "Lap Clock UTC Timing"
Cohesion: 0.36
Nodes (7): utc_at_fraction(), utc_elapsed_ms(), GetRaceElapsedTime, PublishPositionsOnly, PublishSnapshot, StopSession, Update

### Community 79 - "Code Style Conventions"
Cohesion: 0.29
Nodes (7): C5: sample rate declared in two places, Comments explain why, not what, const by default, careful auto, named constants, Naming convention (snake_case / PascalCase / UPPER_CASE / trailing _), Code is read ten times more often than written, TODO / FIXME / HACK markers, English-only identifiers and console output

### Community 80 - "AppPaths Data Directories"
Cohesion: 0.57
Nodes (6): AppPaths single source of data directories, path, migrate_directory(), move_file(), open_in_explorer(), prepare()

### Community 81 - "Track Trailer Header"
Cohesion: 0.29
Nodes (6): TrackTrailerHeader, file_name, format, magic, payload_size, version

### Community 82 - "Pending Origin Struct"
Cohesion: 0.29
Nodes (7): PendingOrigin, easting, map_size, northing, valid, zone, zone_char

### Community 83 - "Track Map Sector Snapshot"
Cohesion: 0.29
Nodes (6): GetSectorSnapshot(), SectorSnapshot, delta, hasDelta, live, t

### Community 84 - "Start-Finish Line Crossing"
Cohesion: 0.29
Nodes (7): LineCrossing, armed, fraction, from_geometry, has_source_time, point_index, utc_ms

### Community 85 - "Graphify Knowledge Graph Tooling"
Cohesion: 0.40
Nodes (6): graphify skill registration, GRAPH_REPORT.md, Project knowledge graph (graphify-out), graphify query / path / explain, graphify update (AST-only refresh), graphify wiki index

### Community 86 - "Telemetry Log Stats"
Cohesion: 0.40
Nodes (5): TelemetryLogStats, header_valid, records_total, records_valid, trailing_bytes

### Community 87 - "Replay Keyframes"
Cohesion: 0.40
Nodes (5): map, find_keyframe_before(), Keyframe, index, vehicles

### Community 88 - "Vehicle Name Rendering"
Cohesion: 0.40
Nodes (4): ImFont, mat4, string, DrawName()

### Community 89 - "Car Lap Session Samples"
Cohesion: 0.40
Nodes (5): CarLapSessions, globalLapnumber, lapnumber, samples, vector

### Community 90 - "Frame Arrival Stats"
Cohesion: 0.50
Nodes (4): FrameStat, arrival_ms, clock_diff, lost_before

### Community 91 - "Track Server Client Loop"
Cohesion: 0.50
Nodes (4): runLoop(), runOnce(), widen(), wstring

### Community 92 - "Include Root and Module Layout"
Cohesion: 0.67
Nodes (3): src as single #include root, Legacy UI.cpp de-duplication, OpenGL/src module layout

### Community 93 - "Race Data Packet"
Cohesion: 0.67
Nodes (3): RaceDataPacket, has_start_finish_line, magic_marker

### Community 94 - "Lap Time Smoother"
Cohesion: 0.67
Nodes (3): VehicleLapTimeSmoother, initialized, lastLocalTime

### Community 95 - "Vehicle Time Sync"
Cohesion: 0.67
Nodes (3): VehicleTimeSync, initialized, offsetSeconds

## Knowledge Gaps
- **746 isolated node(s):** `magic`, `version`, `origin_easting`, `origin_northing`, `origin_zone` (+741 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 974 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **6 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `UI` connect `UI Facade State` to `Status Bar Time Formatting`, `Track Apply and Recent Files`, `Vehicle Rendering Geometry`, `App Bootstrap and DPI Scaling`, `UI Modals and Fonts`, `UI Frame Lifecycle`, `Simulation Server Telemetry`, `Recent Files and Replay Open`, `Race Display Wrapper`, `ESP32 Serial COM Discovery`?**
  _High betweenness centrality (0.103) - this node is a cross-community bridge._
- **Why does `Vehicle` connect `Vehicle Telemetry State` to `Vehicle State Packet`, `Race Manager Results`, `Vehicle Rendering Geometry`, `Sector Timing Findings`, `Start-Finish Line Crossing`, `Replay Keyframes`, `Simulation Server Telemetry`, `Vehicle Render State`, `Car Lap Session Samples`, `Replay Player Transport`, `Server Auth Packets`, `PRO Sectors Panel`?**
  _High betweenness centrality (0.078) - this node is a cross-community bridge._
- **Why does `VehicleView` connect `World Snapshot Vehicle View` to `Module Header Surface`, `Race Manager Results`, `World Snapshot Publish`, `Sector Timing Findings`, `PRO Track Map Panel`, `Track Map Sector Snapshot`, `Server Auth Packets`?**
  _High betweenness centrality (0.059) - this node is a cross-community bridge._
- **Are the 4 inferred relationships involving `VehicleView` (e.g. with `GetVehicleLapsCopy` and `GetVehiclePreviousLapTime`) actually correct?**
  _`VehicleView` has 4 INFERRED edges - model-reasoned connections that need verification._
- **What connects `magic`, `version`, `origin_easting` to the rest of the system?**
  _746 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `UI Facade State` be split into smaller, more focused modules?**
  _Cohesion score 0.02774774774774775 - nodes in this community are weakly interconnected._
- **Should `Track Spline Interpolation` be split into smaller, more focused modules?**
  _Cohesion score 0.07289002557544758 - nodes in this community are weakly interconnected._