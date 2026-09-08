# Graph Report - OpenGL-BONI-MVP-  (2026-09-08)

## Corpus Check
- 556 files · ~1,045,310 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 8317 nodes · 18352 edges · 328 communities (277 shown, 17 thin omitted)
- Extraction: 91% EXTRACTED · 9% INFERRED · 0% AMBIGUOUS · INFERRED: 1695 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `2ca7192b`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- UI
- Render.cpp
- CsvImport.cpp
- TrackReviewPanel.cpp
- Vehicle
- SyntheticTelemetry.cpp
- Row
- Vehicle focus selection (P key)
- mutex
- main
- TrackRecorder.cpp
- TeeStreambuf
- TelemetryTrackBuilder.cpp
- SecCmp
- VehicleView
- UI.cpp
- VehicleInterpolator
- LapInfo
- RenderGraphsWindow
- ProContext
- RaceManager
- ProView.cpp
- UIElements
- SimulationServer.cpp
- ProSidebar.cpp
- ModeManager
- ReplayPlayer.cpp
- Geometry
- RenderTrackReportWindow
- TelemetryLog.cpp
- Server.h
- RenderTopMenu
- render_bar_panel
- T
- Input.cpp
- RaceStatusBar
- impl_
- VehicleStatePacket
- ImGuiContext
- Vehicle.cpp
- ProEvents.cpp
- glm.cpp
- VehicleStanding
- RelCar
- LapData
- imgui.cpp
- ImVec2
- TrackServerClient.cpp
- imgui_widgets.cpp
- RaceFlags
- ReplayStatus
- ImGuiIO
- impl_
- ImFontConfig
- RaceDisplay
- VehicleRenderState
- VehicleJournal (replay recording journal)
- Trk2FileHeader
- TelemetryLogHeader
- json.hpp
- vector
- TimeDiff.cpp
- apply_seek
- qualifier.hpp
- ImGuiResizeBorderDef
- ImGuiTextBuffer
- common.h
- StartStop.cpp
- imgui.h
- ImMax
- Snapshot
- MapOrigin
- glad.c
- ImDrawList
- TrackRecorder.h
- imstb_textedit.h
- ImFont
- stb_image.h
- Naming convention (snake_case / PascalCase / UPPER_CASE / trailing _)
- AppPaths.cpp
- ImGui::ShowMetricsWindow
- PendingOrigin
- ImGui_ImplOpenGL3_Data
- LineCrossing
- Project knowledge graph (graphify-out)
- TelemetryLogStats
- ImFontAtlasCustomRect
- ImGuiNextItemData
- type_ptr.hpp
- ImGuiViewportP
- ImGuiBoxSelectState
- src as single #include root
- ImGuiID
- compute_vector_decl.hpp
- ImGuiWindowSettings
- SyntheticScenario
- SettingsPanel.h
- D4: dead constants in ProGraphs
- spdlog level policy
- Track recording from telemetry
- imgui_internal.h
- _vectorize.hpp
- ImFontAtlas
- ImGuiTableColumn
- ImGuiViewport
- gtc/quaternion.hpp
- ~ImVector
- stbi__context
- AngleT
- imgui_demo.cpp
- imgui_draw.cpp
- ImGuiID
- imgui_tables.cpp
- ImGuiListClipper
- ImFontGlyph
- imstb_truetype.h
- format_punct
- ImGuiNavItemData
- STBTT_DEF
- ImGuiTableColumnSettings
- binary_reader
- SphericalHarmonic2
- real
- stbtt_fontinfo
- ImGuiLastItemData
- stbi__err
- serialib.cpp
- Constants.hpp
- _matrix_vectorize.hpp
- ExampleAssetsBrowser
- ImVec4
- type_precision.hpp
- gtx/hash.hpp
- imgui_impl_glfw.cpp
- PolygonAreaT
- type_trait.hpp
- ImGuiOldColumns
- stbi__load_main
- ImTriangulator
- ImGuiSelectionBasicStorage
- ImFontGlyphRangesBuilder
- stbtt_pack_context
- ttUSHORT
- What You Must Do When Invoked
- ImGui_ImplGlfw_Data
- ImGuiNextWindowData
- Geodesic.hpp
- ImDrawData
- ImGuiPayload
- ImGuiMetricsConfig
- glfw3.h
- ExampleAsset
- json_pointer.hpp
- structured_bindings.hpp
- ImGuiDemoWindowData
- MyDocument
- ImGuiInputTextCallbackData
- ImGuiIDStackTool
- ordered_map.hpp
- Interpolation.cpp
- AddLine
- ui_scale.cpp
- json_sax_dom_callback_parser
- stbi_uc
- ExampleTreeNode
- get
- Telemetry ingest pipeline (processIncomingTelemetry -> g_vehicles)
- ImDrawCmd
- ImGuiTextFilter
- stbtt__buf
- ImPool
- sax_parse
- ImGuiShrinkWidthItem
- stbi__zbuf
- SettingsPanel.cpp
- vec<3, T, Q>
- handle_value
- json_sax_acceptor
- A1: SECTORS panel still computes sectors from the sample log
- _noise.hpp
- .operator -=
- vec<2, T, Q>
- compatibility.hpp
- stbtt__run_charstring
- CsvMapping
- ImGuiKeyRoutingData
- mat<2, 2, T, Q>
- mat<2, 3, T, Q>
- mat<2, 4, T, Q>
- mat<3, 2, T, Q>
- mat<3, 3, T, Q>
- mat<3, 4, T, Q>
- mat<4, 3, T, Q>
- mat<4, 4, T, Q>
- create
- Math.hpp
- imgui/imgui_impl_opengl3_loader.h
- ImSpanAllocator
- imstb_rectpack.h
- stbi__parse_png_file
- CsvTable
- OSGB.hpp
- Ellipsoid3.hpp
- Utility.hpp
- mat<4, 2, T, Q>
- qua
- tdualquat
- ImChunkStream
- ingest_wire_packet
- DST
- _swizzle_base2
- protocol.h
- vec<1, T, Q>
- ImGuiDebugAllocInfo
- ImGuiInputEventMouseButton
- json_sax_dom_parser
- matrix.hpp
- vec<4, T, Q>
- VehicleJournal
- ImGuiContextHook
- ImTextCharFromUtf8
- Sample
- TrackEditor.cpp
- UserInfo
- RaceStatusBar.cpp
- graphify reference: extra exports and benchmark
- vec
- ImGuiMultiSelectIO
- TrackCenterInfo
- string
- ImGuiListClipperData
- .parse_error
- byte_container_with_subtype.hpp
- operator->
- ImGui_ImplGlfw_WndProc
- init_gentype<genType, GENTYPE_MAT>
- ImGuiTableInstanceData
- ImGuiTextIndex
- uint8_t
- ImGui_ImplGlfw_OnCanvasSizeChange
- GLFWvidmode
- _swizzle.hpp
- ImGuiStyleVarInfo
- RenderRaceStatusBar
- hex_bytes
- graphify reference: query, path, explain
- Karpathy Guidelines
- readarray
- ExampleMemberInfo
- TelemetryTrackBuilder.h
- dump_integer
- glm_i128_interleave
- ImGuiKeyData
- DemoWindowTables
- graphify reference: add a URL and watch a folder
- graphify reference: commit hook and native CLAUDE.md integration
- graphify reference: incremental update and cluster-only
- dump_float
- ImGui::GetAllocatorFunctions
- CsvChannel
- GeographicErr
- C1: two locking idioms on one mutex
- ImGuiColorMod
- ImGuiPlotArrayGetterData
- from_json
- number_unsigned_t
- JournalBuild
- graphify reference: GitHub clone and cross-repo merge
- graphify reference: transcribe video and audio
- from_json
- ImGuiTableColumnSortSpecs
- ImGuiInputEvent
- SectorSnapshot
- backends/imgui_impl_glfw.h
- position_t.hpp
- namespace
- extraction-spec.md
- OpenGL.sln
- GLFWwindow
- ImGui_ImplGlfw_MonitorCallback
- TrackTrailerHeader
- SyntheticCar
- type
- FindLatestInputEvent
- internal_iterator.hpp
- CarLapSessions
- ImColor
- ImGuiResizeGripDef
- stbtt_GetKerningTable
- FrameStat
- ImGuiStyleMod
- binary_t
- number_integer_t
- openDevice

## God Nodes (most connected - your core abstractions)
1. `T()` - 536 edges
2. `ImGuiContext` - 379 edges
3. `ImGuiWindow` - 160 edges
4. `ImGuiIO` - 129 edges
5. `ImMax()` - 126 edges
6. `UI` - 124 edges
7. `ImRect` - 106 edges
8. `ImDrawList` - 98 edges
9. `Vehicle` - 75 edges
10. `stbtt_fontinfo` - 73 edges

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
- **Race status bar: display wrapper, bar, flags and phase source** — docs_notes_race_display_implementation_racedisplay, docs_notes_race_display_implementation_racestatusbar, docs_notes_race_display_implementation_raceflags, docs_notes_race_display_implementation_responsive_scaling, docs_notes_race_display_implementation_modemanager_integration [EXTRACTED 1.00]
- **Three data sources converge on a single telemetry ingest path** — opengl_claude_com_serial_source, opengl_claude_track_server_source, opengl_claude_simulation_source, opengl_claude_telemetry_pipeline, docs_code_review_single_ingest_path [EXTRACTED 1.00]
- **PRO analysis panels are path-dependent across a replay session** — docs_code_review_b1_events_from_observation, docs_code_review_b2_analysis_lap_divergence, docs_code_review_b3_sector_rollback_asymmetry, docs_code_review_b5_panel_state_survives_replay_change, docs_code_review_vehicle_journal [INFERRED 0.85]

## Communities (328 total, 17 thin omitted)

### Community 0 - "UI"
Cohesion: 0.02
Nodes (84): NetworkingModalMode, vec2, GLFWwindow, mutex, string, time_point, vec2, vector (+76 more)

### Community 1 - "Render.cpp"
Cohesion: 0.17
Nodes (24): GLenum, publish_smooth_track_points(), clearStartFinishLine(), clearTrackCache(), compileStartLineShader(), GLuint, mat4, mutex (+16 more)

### Community 2 - "CsvImport.cpp"
Cohesion: 0.29
Nodes (18): Fn, convert_csv_to_replay(), path, string, vector, detect_delimiter(), fail(), for_each_data_row() (+10 more)

### Community 3 - "TrackReviewPanel.cpp"
Cohesion: 0.07
Nodes (61): ApplyProportionalMove(), CancelDrag(), ImDrawList, ImFont, ImU32, ImVec2, vec2, vector (+53 more)

### Community 4 - "Vehicle"
Cohesion: 0.04
Nodes (57): map, SECTOR_COUNT, time_point, vector, Vehicle, bestlapID, laps, m_acceleration (+49 more)

### Community 5 - "SyntheticTelemetry.cpp"
Cohesion: 0.19
Nodes (21): append_packet(), build_path(), build_synthetic_stream(), CornerAhead, curvature, distance_m, MapOrigin, SplinePoint (+13 more)

### Community 6 - "Row"
Cohesion: 0.06
Nodes (53): A3: telemetry export does not survive UTC midnight, A4: CSV import time origin can stay zero, ExportSamples, build_row(), build_rows(), Channel, csv_name, format (+45 more)

### Community 7 - "Vehicle focus selection (P key)"
Cohesion: 0.15
Nodes (13): D5: settings written to disk on every wheel click, Focus target existence check under g_vehicles_mutex, Vehicle focus selection (P key), 10-second focus input timeout, Multi-digit vehicle ID entry, Coordinate frame conventions (txt / trk2 / sim, render offset), Dual-edge track creation, Local UI state files excluded from git (+5 more)

### Community 8 - "mutex"
Cohesion: 0.11
Nodes (31): Device registry (SQLite devices.db), sqlite3 required for both vcpkg triplets, string, vector, DeviceInfo, device_id, group, last_seen_unix (+23 more)

### Community 9 - "main"
Cohesion: 0.08
Nodes (26): B4: one frame after a seek, position and timing disagree, AppContext, points, points_mutex, ui, zoom, GLFWwindow, GLuint (+18 more)

### Community 10 - "TrackRecorder.cpp"
Cohesion: 0.06
Nodes (41): TrackDataHeader, magic_marker, origin_easting, origin_lat, origin_lon, origin_northing, origin_zone, origin_zone_char (+33 more)

### Community 11 - "TeeStreambuf"
Cohesion: 0.07
Nodes (37): int_type, ConsoleLogSession, impl_, err_buffer, file, original_err, original_out, out_buffer (+29 more)

### Community 12 - "TelemetryTrackBuilder.cpp"
Cohesion: 0.10
Nodes (24): EdgePhase, Settings, SplinePoint, TelemetryPacket, vec2, vector, distNorm(), ensureOriginLocked() (+16 more)

### Community 13 - "SecCmp"
Cohesion: 0.10
Nodes (20): colorFor(), ImU32, SecCmp, bestS, bestV, sessS, sessV, sessW (+12 more)

### Community 14 - "VehicleView"
Cohesion: 0.06
Nodes (35): SECTOR_COUNT, shared_ptr, string, vec3, VehicleView, acceleration, apply_track_render_offset, best_lap_id (+27 more)

### Community 15 - "UI.cpp"
Cohesion: 0.09
Nodes (59): ExportFormat, AddDashedRect(), applyTrackData(), applyTrackFile(), collect_export_samples(), CsvChannel, GLFWwindow, HWND (+51 more)

### Community 16 - "VehicleInterpolator"
Cohesion: 0.09
Nodes (27): map, mutex, VehicleBuffer, Cleanup, GetBracketingSnapshots, mutex, snapshots, VehicleInterpolator (+19 more)

### Community 17 - "LapInfo"
Cohesion: 0.06
Nodes (38): SECTOR_COUNT, lapZoneTimes(), SessionBestZones, time, valid, zoneShape(), zoneTimes(), SECTOR_COUNT (+30 more)

### Community 18 - "RenderGraphsWindow"
Cohesion: 0.06
Nodes (50): ChannelId, apply_reference(), Channel, bipolar, color, format, id, key (+42 more)

### Community 19 - "ProContext"
Cohesion: 0.09
Nodes (40): ImVec2, RenderChannelsWindow(), ImVec2, detectEvents(), EvtState, bestLap, bestSec, RenderEventsWindow() (+32 more)

### Community 20 - "RaceManager"
Cohesion: 0.05
Nodes (57): milliseconds, utc_at_fraction(), utc_elapsed_ms(), string, vec2, vector, map, mutex (+49 more)

### Community 21 - "ProView.cpp"
Cohesion: 0.10
Nodes (36): ImVec2, RenderLapListWindow(), ImVec2, RenderLaptimeWindow(), AnalysisLap(), AnalysisVehicle(), ComparePin(), ComparisonActive() (+28 more)

### Community 22 - "UIElements"
Cohesion: 0.12
Nodes (16): vec2, ImFont, MapOrigin, UIElements, drawLapTimer, drawSeparatorLine, initialize, m_compass_texture (+8 more)

### Community 23 - "SimulationServer.cpp"
Cohesion: 0.07
Nodes (46): mt19937, BroadcastTelemetryToClients(), BroadcastVehicleStateToClients(), TelemetryPacket, allocateRaceIdLocked(), applyRaceStateFromPacket(), calculateCumulativeDistances(), calculateTrackProgressFromPosition() (+38 more)

### Community 24 - "ProSidebar.cpp"
Cohesion: 0.14
Nodes (25): anyVisible(), ImVec2, string, defaultVisible(), drawFlyout(), FlushPanelSettings(), flushVis(), flyoutSize() (+17 more)

### Community 25 - "ModeManager"
Cohesion: 0.10
Nodes (15): Automatic phase detection from ModeManager, RaceDisplay wrapper, RaceFlags / FlagColor, RaceStatusBar, Responsive scaling from a 1600x900 reference, SessionState, string, ModeManager (+7 more)

### Community 26 - "ReplayPlayer.cpp"
Cohesion: 0.09
Nodes (20): clear_error(), function, path, string, vector, fail(), FeedingScope, find_track_by_name() (+12 more)

### Community 27 - "Geometry"
Cohesion: 0.16
Nodes (22): SplinePoint, vec2, vector, Geometry, build, cumulative_distances, points, segment_count (+14 more)

### Community 28 - "RenderTrackReportWindow"
Cohesion: 0.13
Nodes (21): ColorMode, ImDrawList, ImU32, ImVec2, vec2, vector, heatColor(), ModeItem (+13 more)

### Community 29 - "TelemetryLog.cpp"
Cohesion: 0.15
Nodes (27): path, string, Impl, unique_ptr, local_date_yyyymmdd(), make_log_file_name(), read_utc_ms(), records_end_offset() (+19 more)

### Community 30 - "Server.h"
Cohesion: 0.04
Nodes (46): AuthPacket, magic_marker, password, AuthResponsePacket, attempts_remaining, is_authenticated, magic_marker, message (+38 more)

### Community 31 - "RenderTopMenu"
Cohesion: 0.12
Nodes (26): closeSerialNoThrow(), comDiscoveryThreadWorker(), ComPortInfo, description, port, string, vector, enumerateComPortsWindows() (+18 more)

### Community 32 - "render_bar_panel"
Cohesion: 0.27
Nodes (12): bar_color(), ImDrawList, ImU32, ImVec2, draw_track(), read_g(), Reading, has_vehicle (+4 more)

### Community 33 - "T"
Cohesion: 0.03
Nodes (343): T(), glm::vec<2, T, Q> ww(), glm::vec<2, T, Q> wx(), glm::vec<2, T, Q> wy(), glm::vec<2, T, Q> wz(), glm::vec<2, T, Q> xw(), glm::vec<2, T, Q> xx(), glm::vec<2, T, Q> xy() (+335 more)

### Community 34 - "Input.cpp"
Cohesion: 0.25
Nodes (22): chooseInputMode(), coordinatesToDecimalFormat(), coordinatesToMeters(), atomic, mutex, string, vec2, vector (+14 more)

### Community 35 - "RaceStatusBar"
Cohesion: 0.10
Nodes (21): time_point, RaceStatusBar, BASE_BAR_HEIGHT, BASE_BAR_WIDTH, BASE_FLAG_SIZE, BASE_TEXT_SCALE, m_barHeight, m_barWidth (+13 more)

### Community 36 - "impl_"
Cohesion: 0.09
Nodes (23): condition_variable, atomic, mutex, ofstream, TelemetryLogSource, impl_, dropped, first_utc_ms (+15 more)

### Community 37 - "VehicleStatePacket"
Cohesion: 0.12
Nodes (16): VehicleStatePacket, best_lap_time, completed_laps, current_lap_number, current_lap_time, has_started_first_lap, heading, is_leader (+8 more)

### Community 39 - "ImGuiContext"
Cohesion: 0.01
Nodes (310): ImBitArrayForNamedKeys, ImGuiActivateFlags, ImGuiErrorCallback, ImGuiIO, ImGuiPlatformIO, ImGuiColorEditFlags, ImGuiDebugLogFlags, ImGuiDragDropFlags (+302 more)

### Community 40 - "Vehicle.cpp"
Cohesion: 0.15
Nodes (16): GLuint, mat4, TelemetryPacket, vec2, vec3, vector, generateCircle(), generateTriangle() (+8 more)

### Community 41 - "ProEvents.cpp"
Cohesion: 0.18
Nodes (15): deque, buildFromJournal(), ImU32, string, vector, elapsedSinceStart(), fmtSec(), LogEvent (+7 more)

### Community 42 - "glm.cpp"
Cohesion: 0.01
Nodes (186): mat<2, 2, float32, highp>, mat<2, 2, float32, lowp>, mat<2, 2, float32, mediump>, mat<2, 2, float64, highp>, mat<2, 2, float64, lowp>, mat<2, 2, float64, mediump>, mat<2, 3, float32, highp>, mat<2, 3, float32, lowp> (+178 more)

### Community 43 - "VehicleStanding"
Cohesion: 0.12
Nodes (16): VehicleStanding, bestLapTime, completedLaps, currentLapNumber, currentLapTime, deltaTimeToBest, deltaTimeToLeader, distanceFromStart (+8 more)

### Community 44 - "RelCar"
Cohesion: 0.15
Nodes (13): carColor(), ImU32, string, vec3, RelCar, color, dProg, gapTime (+5 more)

### Community 45 - "LapData"
Cohesion: 0.22
Nodes (9): map, GetVehicleLaps, GetVehicleLapsCopy, SECTOR_COUNT, LapData, lapTime, positionAtFinish, sectors (+1 more)

### Community 46 - "imgui.cpp"
Cohesion: 0.01
Nodes (273): ImGuiFocusedFlags, ImGuiFocusRequestFlags, ImGuiHoveredFlags, ImGuiNavRenderCursorFlags, ImGuiPopupFlags, AddRootWindowToDrawData(), AddWindowToDrawData(), AddWindowToSortBuffer() (+265 more)

### Community 47 - "ImVec2"
Cohesion: 0.07
Nodes (64): ImDrawFlags, ImDrawList, ImGuiDir, ImGuiMouseCursor, ImU32, ImVec2, FixRectCornerFlags(), ImDrawData::AddDrawList() (+56 more)

### Community 48 - "TrackServerClient.cpp"
Cohesion: 0.13
Nodes (18): telemetryGetRaceIdForPrototype(), string, currentFlag(), handleMessage(), handleState(), jsonNumber(), jsonString(), monotonicMs() (+10 more)

### Community 49 - "imgui_widgets.cpp"
Cohesion: 0.01
Nodes (200): ImGuiComboFlags, ImGuiInputTextCallback, ImGuiMultiSelectFlags, ImGuiSliderFlags, ImGuiTabBarFlags, ImS32, ImS64, ImFormatStringToTempBufferV() (+192 more)

### Community 50 - "RaceFlags"
Cohesion: 0.20
Nodes (5): FlagColor, RaceFlags, m_leftFlag, m_rightFlag, ModeManager

### Community 51 - "ReplayStatus"
Cohesion: 0.15
Nodes (13): replay_status(), ReplayStatus, active, date_yyyymmdd, duration_ms, file_name, first_utc_ms, paused (+5 more)

### Community 52 - "ImGuiIO"
Cohesion: 0.02
Nodes (115): ImGuiBackendFlags, ImGuiConfigFlags, ImWchar16, ImGuiKeyChord, ImGuiMouseSource, ImWchar16, ImGuiIO, AddInputCharacter (+107 more)

### Community 53 - "impl_"
Cohesion: 0.17
Nodes (12): vector, embedded_track_format, impl_, elapsed_ms, header, records, track_bytes, track_file_name (+4 more)

### Community 54 - "ImFontConfig"
Cohesion: 0.06
Nodes (30): ImWchar, ImFontConfig, DstFont, EllipsisChar, FontBuilderFlags, FontData, FontDataOwnedByAtlas, FontDataSize (+22 more)

### Community 55 - "RaceDisplay"
Cohesion: 0.24
Nodes (9): ModeManager, RaceDisplay, Initialize, m_screenHeight, m_screenWidth, m_statusBar, OnScreenResized, Render (+1 more)

### Community 56 - "VehicleRenderState"
Cohesion: 0.17
Nodes (12): string, vec3, VehicleRenderState, apply_track_render_offset, color, heading, id, is_leader (+4 more)

### Community 57 - "VehicleJournal (replay recording journal)"
Cohesion: 0.14
Nodes (15): C2: replay_journal returns a raw pointer into a mutating map, C4: LapData::telemetryPoints is a dead field copied everywhere, C6: journal warm-up blocks the frame with no sign of life, C7: journal duplicates vehicle history in memory, D1: snapshot copies every lap of every vehicle each frame, D2: LAP LIST copies the lap list and immediately discards it, Invariant: keyframes without sample history, Invariant: panels read the published snapshot (+7 more)

### Community 58 - "Trk2FileHeader"
Cohesion: 0.18
Nodes (11): Trk2FileHeader, left_count, magic, map_size, origin_easting, origin_northing, origin_zone, origin_zone_char (+3 more)

### Community 59 - "TelemetryLogHeader"
Cohesion: 0.18
Nodes (11): TelemetryLogHeader, first_utc_ms, last_utc_ms, magic, record_count, record_size, source, track_embedded (+3 more)

### Community 60 - "json.hpp"
Cohesion: 0.06
Nodes (107): array_t, boolean_t, CompatibleType, const_reference, const_reverse_iterator, else, initializer_list_t, iteration_proxy (+99 more)

### Community 61 - "vector"
Cohesion: 0.04
Nodes (63): input_format_t, string, map, vector, fmtGap(), CardHeight(), fmtSector(), NLOHMANN_JSON_NAMESPACE_BEGIN (+55 more)

### Community 62 - "TimeDiff.cpp"
Cohesion: 0.36
Nodes (7): GetVehicleLapDelta, GetVehicleLeaderDelta, CalculateLapTimeDiff(), CalculateLapTimeDiffInternal(), CalculateLeaderTimeDiff(), CalculateLeaderTimeDiffInternal(), GetCachedTrackLengthMeters()

### Community 63 - "apply_seek"
Cohesion: 0.13
Nodes (22): B1: event log is built by observation, not from the recording, B3: laps roll back on seek, sectors do not, B5: panel state survives a change of recording, apply_seek(), map, feed_range(), find_keyframe_before(), journal_build_begin() (+14 more)

### Community 64 - "qualifier.hpp"
Cohesion: 0.03
Nodes (53): genTypeEnum, compute_equal, GLM_CONSTEXPR, GLM_FUNC_QUALIFIER, genTypeTrait, genTypeTrait<mat<C, R, T> >, GENTYPE, init_gentype (+45 more)

### Community 65 - "ImGuiResizeBorderDef"
Cohesion: 0.40
Nodes (5): ImGuiResizeBorderDef, InnerDir, OuterAngle, SegmentN1, SegmentN2

### Community 66 - "ImGuiTextBuffer"
Cohesion: 0.09
Nodes (12): IMGUI_API, clear_delete(), clear_destruct(), IM_DELETE(), ImGui::DestroyContext(), ImGuiTextBuffer, append, Buf (+4 more)

### Community 67 - "common.h"
Cohesion: 0.07
Nodes (71): float32x2_t, float32x4_t, glm_ivec4, __m128, glm_ivec4_abs(), glm_vec1_add(), glm_vec1_div(), glm_vec1_fma() (+63 more)

### Community 68 - "StartStop.cpp"
Cohesion: 0.27
Nodes (8): InvalidateStandingsCache, ResetSession, SessionState, RaceManager::GetSessionState(), RaceManager::ResetMap(), RaceManager::ResetSession(), RaceManager::StartSession(), RaceManager::StopSession()

### Community 69 - "imgui.h"
Cohesion: 0.05
Nodes (28): begin(), contains(), end(), erase(), erase_unsorted(), find(), find_erase(), find_erase_unsorted() (+20 more)

### Community 70 - "ImMax"
Cohesion: 0.05
Nodes (135): ImGuiButtonFlags, ImGuiPlotType, ImGuiPopupPositionPolicy, ImGuiSelectableFlags, ImGuiSeparatorFlags, ImGuiTextFlags, CalcNextScrollFromScrollTargetAndClamp(), ClampWindowPos() (+127 more)

### Community 71 - "Snapshot"
Cohesion: 0.31
Nodes (8): shared_ptr, current(), find(), publish(), Snapshot, revision, standings, vehicles

### Community 72 - "MapOrigin"
Cohesion: 0.25
Nodes (8): MapOrigin, m_map_size, m_origin_lat_dd, m_origin_lon_dd, m_origin_meters_easting, m_origin_meters_northing, m_origin_zone_char, m_origin_zone_int

### Community 73 - "glad.c"
Cohesion: 0.06
Nodes (59): Archive, dist_t, distfun_t, GLADloadproc, item, dist, close_gl(), find_coreGL() (+51 more)

### Community 74 - "ImDrawList"
Cohesion: 0.04
Nodes (64): ImDrawIdx, ImDrawListFlags, ShowExampleAppCustomRendering(), ImDrawListSharedData, ImDrawList::AddRectFilledMultiColor(), ImDrawList::ImDrawList(), ImDrawList::PrimReserve(), ImDrawListSplitter::Merge() (+56 more)

### Community 75 - "TrackRecorder.h"
Cohesion: 0.22
Nodes (8): MapOrigin, Settings, closeRadiusNorm, minLoopLengthMeters, minPointDistanceNorm, minPointsToClose, pointsPerSegment, SplinePoint

### Community 76 - "imstb_textedit.h"
Cohesion: 0.10
Nodes (58): IMSTB_TEXTEDIT_STRING, ImTextFindPreviousUtf8Codepoint(), ImGuiInputTextState, STB_TexteditState, IMSTB_TEXTEDIT_CHARTYPE, ImCharIsSeparatorW(), ImGuiInputTextState::OnKeyPressed(), IMSTB_TEXTEDIT_GETPREVCHARINDEX_IMPL() (+50 more)

### Community 77 - "ImFont"
Cohesion: 0.05
Nodes (42): CalcWordWrapNextLineStartA(), FindFirstExistingGlyph(), ImFont::AddRemapChar(), ImFont::BuildLookupTable(), ImFont::CalcTextSizeA(), ImFont::ImFont(), ImFont::RenderText(), ImU16 (+34 more)

### Community 78 - "stb_image.h"
Cohesion: 0.08
Nodes (65): FILE, stbi__compute_y_16(), stbi__convert_16_to_8(), stbi__convert_8_to_16(), stbi_convert_iphone_png_to_rgb(), stbi_convert_iphone_png_to_rgb_thread(), stbi_convert_wchar_to_utf8(), stbi__do_zlib() (+57 more)

### Community 79 - "Naming convention (snake_case / PascalCase / UPPER_CASE / trailing _)"
Cohesion: 0.29
Nodes (7): C5: sample rate declared in two places, Comments explain why, not what, const by default, careful auto, named constants, Naming convention (snake_case / PascalCase / UPPER_CASE / trailing _), Code is read ten times more often than written, TODO / FIXME / HACK markers, English-only identifiers and console output

### Community 80 - "AppPaths.cpp"
Cohesion: 0.57
Nodes (6): AppPaths single source of data directories, path, migrate_directory(), move_file(), open_in_explorer(), prepare()

### Community 81 - "ImGui::ShowMetricsWindow"
Cohesion: 0.06
Nodes (58): CalcRoutingScore(), ImGuiInputFlags, ImGuiInputSource, ImGuiKey, ImGuiKeyChord, ImGuiMouseSource, DebugPrintInputEvent(), GetInputSourceName() (+50 more)

### Community 82 - "PendingOrigin"
Cohesion: 0.29
Nodes (7): PendingOrigin, easting, map_size, northing, valid, zone, zone_char

### Community 83 - "ImGui_ImplOpenGL3_Data"
Cohesion: 0.05
Nodes (57): GLint, GLsizeiptr, GLvoid, CheckProgram(), CheckShader(), GLuint, ImGui_ImplOpenGL3_CreateDeviceObjects(), ImGui_ImplOpenGL3_CreateFontsTexture() (+49 more)

### Community 84 - "LineCrossing"
Cohesion: 0.29
Nodes (7): LineCrossing, armed, fraction, from_geometry, has_source_time, point_index, utc_ms

### Community 85 - "Project knowledge graph (graphify-out)"
Cohesion: 0.40
Nodes (6): graphify skill registration, GRAPH_REPORT.md, Project knowledge graph (graphify-out), graphify query / path / explain, graphify update (AST-only refresh), graphify wiki index

### Community 86 - "TelemetryLogStats"
Cohesion: 0.40
Nodes (5): TelemetryLogStats, header_valid, records_total, records_valid, trailing_bytes

### Community 87 - "ImFontAtlasCustomRect"
Cohesion: 0.11
Nodes (19): ImFontAtlas, ImFontAtlas::CalcCustomRectUV(), ImFontAtlasBuildFinish(), ImFontAtlasBuildInit(), ImFontAtlasBuildPackCustomRects(), ImFontAtlasBuildRender32bppRectFromString(), ImFontAtlasBuildRender8bppRectFromString(), ImFontAtlasBuildRenderDefaultTexData() (+11 more)

### Community 88 - "ImGuiNextItemData"
Cohesion: 0.11
Nodes (17): ImGuiNextItemDataFlags, ImGuiInputFlags, ImU8, ImGuiDataTypeStorage, Data, ImGuiNextItemData, FocusScopeId, HasFlags (+9 more)

### Community 89 - "type_ptr.hpp"
Cohesion: 0.16
Nodes (7): begin(), components(), end(), genType, length_t, Q, vec

### Community 90 - "ImGuiViewportP"
Cohesion: 0.03
Nodes (92): ImGuiLocKey, ImGuiTooltipFlags, ImDrawList, ImFont, ImGuiChildFlags, ImGuiDataType, ImGuiWindowFlags, ImTextureID (+84 more)

### Community 91 - "ImGuiBoxSelectState"
Cohesion: 0.12
Nodes (16): ImGuiBoxSelectState, BoxSelectRectCurr, BoxSelectRectPrev, EndPosRel, ID, IsActive, IsStartedFromVoid, IsStartedSetNavIdOnce (+8 more)

### Community 92 - "src as single #include root"
Cohesion: 0.67
Nodes (3): src as single #include root, Legacy UI.cpp de-duplication, OpenGL/src module layout

### Community 93 - "ImGuiID"
Cohesion: 0.03
Nodes (98): json_value, IM_MSVC_RUNTIME_CHECKS_OFF, ImGuiDir, ImGuiDragDropFlags, ImGuiID, ImGuiItemFlags, ImGuiItemStatusFlags, ImGuiMouseButton (+90 more)

### Community 94 - "compute_vector_decl.hpp"
Cohesion: 0.06
Nodes (44): L, compute_splat, compute_vec_add, compute_vec_add<L, T, Q, false>, GLM_CONSTEXPR, compute_vec_and, compute_vec_and<L, T, Q, IsInt, Size, false>, GLM_CONSTEXPR (+36 more)

### Community 95 - "ImGuiWindowSettings"
Cohesion: 0.15
Nodes (13): ImGui::CreateNewWindowSettings(), ImGui::DebugNodeWindowSettings(), ImGui::FindWindowSettingsByID(), ImGui::FindWindowSettingsByWindow(), ImGuiWindowSettings, Collapsed, ID, IsChild (+5 more)

### Community 97 - "SyntheticScenario"
Cohesion: 0.10
Nodes (19): isRealDataCaptureRunning(), replay_is_active(), synthetic_start(), SyntheticScenario, base_speed_kph, car_count, degrade_fix_after_gap, duration_seconds (+11 more)

### Community 118 - "imgui_internal.h"
Cohesion: 0.03
Nodes (73): FindBestWheelingWindow(), ImGui::DebugTextUnformattedWithLocateItem(), ImGui::UpdateMouseInputs(), ImGui::UpdateMouseWheel(), AddMousePosEvent, ImStricmp(), ImStrnicmp(), begin() (+65 more)

### Community 119 - "_vectorize.hpp"
Cohesion: 0.09
Nodes (37): Fct, functor1, functor1<vec, 1, R, T, Q>, GLM_CONSTEXPR, functor1<vec, 2, R, T, Q>, GLM_CONSTEXPR, functor1<vec, 3, R, T, Q>, GLM_CONSTEXPR (+29 more)

### Community 120 - "ImFontAtlas"
Cohesion: 0.04
Nodes (51): ImFontAtlasFlags, ImFontBuilderIO, ImFontAtlas::Build(), ImFontAtlas::Clear(), ImFontAtlas::ClearFonts(), ImFontAtlas::GetTexDataAsRGBA32(), ImFontAtlasGetBuilderForStbTruetype(), ImFontAtlas (+43 more)

### Community 121 - "ImGuiTableColumn"
Cohesion: 0.04
Nodes (45): ImGuiTableDrawChannelIdx, ImGuiTableColumnFlags, ImGuiTableColumn, AutoFitQueue, CannotSkipItemsQueue, ClipRect, ContentMaxXFrozen, ContentMaxXHeadersIdeal (+37 more)

### Community 122 - "ImGuiViewport"
Cohesion: 0.06
Nodes (29): ImGuiViewportFlags, BeginChildFrame(), IM_MSVC_RUNTIME_CHECKS_OFF, IM_MSVC_RUNTIME_CHECKS_RESTORE, ImGuiID, ImVec2, ImGui::GetMainViewport(), ImGuiPlatformImeData (+21 more)

### Community 123 - "gtc/quaternion.hpp"
Cohesion: 0.12
Nodes (7): GLM_FUNC_QUALIFIER, mat, Q, qua, mat<3, 3, T, Q> toMat3(), mat<4, 4, T, Q> toMat4(), toQuat()

### Community 124 - "~ImVector"
Cohesion: 0.05
Nodes (43): ImTextureID, stbtt_pack_range, stbtt_packedchar, ImBezierCubicCalc(), ImBezierQuadraticCalc(), ImDrawList::PathArcTo(), ImDrawList::_PathArcToFastEx(), ImDrawList::_PathArcToN() (+35 more)

### Community 125 - "stbi__context"
Cohesion: 0.12
Nodes (50): stbi__bmp_info(), stbi__bmp_parse_header(), stbi__bmp_set_mask_defaults(), stbi__bmp_test(), stbi__bmp_test_raw(), stbi__check_png_header(), stbi__get16le(), stbi__get32le() (+42 more)

### Community 126 - "AngleT"
Cohesion: 0.06
Nodes (45): AngleT, AzimuthString, base, _c, cardinal, DecodeAzimuth, DecodeLatLon, degrees (+37 more)

### Community 127 - "imgui_demo.cpp"
Cohesion: 0.10
Nodes (46): ImDrawList, ImVec2, DemoWindowMenuBar(), DemoWindowPopups(), DemoWindowWidgets(), DemoWindowWidgetsBasic(), DemoWindowWidgetsBullets(), DemoWindowWidgetsCollapsingHeaders() (+38 more)

### Community 128 - "imgui_draw.cpp"
Cohesion: 0.05
Nodes (45): ImFontConfig, ImDrawCallback, ImFont, ImWchar, Decode85(), Decode85Byte(), GetDefaultCompressedFontDataTTF(), ImAcos01() (+37 more)

### Community 129 - "ImGuiID"
Cohesion: 0.06
Nodes (30): GetActiveID(), GetCurrentFocusScope(), GetFocusID(), GetInputTextState(), GetMultiSelectState(), ImGuiID, ImGuiDeactivatedItemData, ElapseFrame (+22 more)

### Community 130 - "imgui_tables.cpp"
Cohesion: 0.03
Nodes (92): ImGuiTableBgTarget, ImGuiTableRowFlags, ImGui::AddSettingsHandler(), ImGui::ClearIniSettings(), ImGui::FindSettingsHandler(), ImGui::SaveIniSettingsToMemory(), ImStrSkipBlank(), GetCurrentTable() (+84 more)

### Community 131 - "ImGuiListClipper"
Cohesion: 0.13
Nodes (14): ForceDisplayRangeByIndices(), ImGuiContext, ImGuiListClipper, Ctx, DisplayEnd, DisplayStart, ImGuiListClipper, IncludeItemsByIndex (+6 more)

### Community 132 - "ImFontGlyph"
Cohesion: 0.13
Nodes (15): ImFont::FindGlyph(), ImFont::FindGlyphNoFallback(), ImFontGlyph, AdvanceX, Codepoint, Colored, U0, U1 (+7 more)

### Community 133 - "imstb_truetype.h"
Cohesion: 0.08
Nodes (49): equal(), stbtt__active_edge, next, stbtt__add_point(), stbtt_CompareUTF8toUTF16_bigendian(), stbtt_CompareUTF8toUTF16_bigendian_internal(), stbtt__compute_crossings_x(), stbtt__cuberoot() (+41 more)

### Community 134 - "format_punct"
Cohesion: 0.05
Nodes (43): char_type, CTy, flags_type, id, locale::facet, locale_type, basic_format_saver, basic_format_saver (+35 more)

### Community 135 - "ImGuiNavItemData"
Cohesion: 0.09
Nodes (21): ImGui::NavMoveRequestResolveWithLastItem(), ImGui::NavMoveRequestResolveWithPastTreeNode(), GetItemFlags(), ImGuiItemFlags, ImGuiSelectionUserData, ImGuiTreeNodeFlags, ImGuiNavItemData, DistAxial (+13 more)

### Community 136 - "STBTT_DEF"
Cohesion: 0.12
Nodes (44): ImFontAtlasBuildMultiplyCalcLookupTable(), ImFontAtlasBuildMultiplyRectAlpha8(), ImFontAtlasBuildWithStbTruetype(), stbtt_pack_range, stbtt_packedchar, main(), my_stbtt_initfont(), my_stbtt_print() (+36 more)

### Community 137 - "ImGuiTableColumnSettings"
Cohesion: 0.11
Nodes (18): ImGuiTableColumnIdx, ImGuiTableCellData, BgColor, Column, ImGuiTableColumnSettings, DisplayOrder, Index, IsEnabled (+10 more)

### Community 138 - "binary_reader"
Cohesion: 0.15
Nodes (14): cbor_tag_handler_t, InputAdapterType, binary_reader, get_bson_binary(), get_bson_string(), binary_t, char_int_type, NumberType (+6 more)

### Community 139 - "SphericalHarmonic2"
Cohesion: 0.07
Nodes (16): coeff, GravityCircle(), GravityCircle, MagneticCircle(), MagneticCircle, CircularEngine, SphericalHarmonic1(), vector (+8 more)

### Community 140 - "real"
Cohesion: 0.09
Nodes (28): AlbersEqualArea(), AltConvergence(), AltEasting(), AltNorthing(), AltScale(), Convergence(), Easting(), EquatorialRadius() (+20 more)

### Community 141 - "stbtt_fontinfo"
Cohesion: 0.07
Nodes (42): stbtt__close_shape(), stbtt_fontinfo, cff, charstrings, data, fdselect, fontdicts, fontstart (+34 more)

### Community 143 - "ImGuiLastItemData"
Cohesion: 0.11
Nodes (18): GetItemStatusFlags(), ImGuiItemStatusFlags, ImGuiKeyChord, ImGuiLastItemData, ClipRect, DisplayRect, ID, ItemFlags (+10 more)

### Community 144 - "stbi__err"
Cohesion: 0.15
Nodes (35): stbi__addints_valid(), stbi__build_fast_ac(), stbi__build_huffman(), stbi__cleanup_jpeg(), stbi__cpuid3(), stbi__decode_jpeg_header(), stbi__decode_jpeg_image(), stbi__err() (+27 more)

### Community 145 - "serialib.cpp"
Cohesion: 0.14
Nodes (28): write, serialib, available, clearDTR, clearRTS, closeDevice, currentStateDTR, currentStateRTS (+20 more)

### Community 146 - "Constants.hpp"
Cohesion: 0.08
Nodes (32): acre(), arcminute(), arcsecond(), chain(), fathom(), foot(), furlong(), GRS80_a() (+24 more)

### Community 147 - "_matrix_vectorize.hpp"
Cohesion: 0.12
Nodes (23): GLM_FUNC_QUALIFIER, mat, Q, matrix_functor_1, matrix_functor_1<mat, 2, 2, Ret, T, Q>, GLM_CONSTEXPR, matrix_functor_1<mat, 2, 3, Ret, T, Q>, GLM_CONSTEXPR (+15 more)

### Community 148 - "ExampleAssetsBrowser"
Cohesion: 0.09
Nodes (22): ExampleAssetsBrowser, AllowBoxSelect, AllowDragUnselected, AllowSorting, IconHitSpacing, IconSize, IconSpacing, Items (+14 more)

### Community 149 - "ImVec4"
Cohesion: 0.13
Nodes (23): CalcScrollEdgeSnap(), ImGui::StyleColorsClassic(), ImGui::StyleColorsDark(), ImGui::StyleColorsLight(), ImGui::SetScrollHereX(), ImGui::SetScrollHereY(), ImGuiStyle, ImTriangleBarycentricCoords() (+15 more)

### Community 150 - "type_precision.hpp"
Cohesion: 0.10
Nodes (6): is_int<int16>, is_int<int64>, is_int<int8>, is_int<uint16>, is_int<uint64>, is_int<uint8>

### Community 151 - "gtx/hash.hpp"
Cohesion: 0.09
Nodes (31): hash<glm::mat<2, 2, T, Q> >, GLM_NOEXCEPT, hash<glm::mat<2, 3, T, Q> >, GLM_NOEXCEPT, hash<glm::mat<2, 4, T, Q> >, GLM_NOEXCEPT, hash<glm::mat<3, 2, T, Q> >, GLM_NOEXCEPT (+23 more)

### Community 152 - "imgui_impl_glfw.cpp"
Cohesion: 0.17
Nodes (29): GLFWwindow, ImGuiKey, ImVec2, ImGui_ImplGlfw_CharCallback(), ImGui_ImplGlfw_CursorEnterCallback(), ImGui_ImplGlfw_CursorPosCallback(), ImGui_ImplGlfw_EmscriptenOpenURL(), ImGui_ImplGlfw_GetBackendData() (+21 more)

### Community 153 - "PolygonAreaT"
Cohesion: 0.06
Nodes (24): GeodType, Accumulator(), _s, PolygonAreaT, AddEdge, AddPoint, _area0, _areasum (+16 more)

### Community 154 - "type_trait.hpp"
Cohesion: 0.07
Nodes (30): length_t, type, cols, components, is_mat, is_quat, is_vec, type<mat<C, R, T, Q> > (+22 more)

### Community 155 - "ImGuiOldColumns"
Cohesion: 0.07
Nodes (27): ImGui::DebugNodeColumns(), ImGuiOldColumnFlags, ImGuiOldColumnData, ClipRect, Flags, OffsetNorm, OffsetNormBeforeResize, ImGuiOldColumns (+19 more)

### Community 156 - "stbi__load_main"
Cohesion: 0.16
Nodes (31): stbi__addsizes_valid(), stbi__bitcount(), stbi__bmp_load(), stbi__convert_format(), stbi__convert_format16(), stbi__do_png(), stbi__get32be(), stbi__getn() (+23 more)

### Community 157 - "ImTriangulator"
Cohesion: 0.11
Nodes (25): ImTriangulatorNodeType, ImDrawList::AddConcavePolyFilled(), ImTriangulator, BuildEars, BuildNodes, BuildReflexes, _Ears, FlipNodeList (+17 more)

### Community 158 - "ImGuiSelectionBasicStorage"
Cohesion: 0.10
Nodes (22): ITEM_TYPE, ApplyDeletionPostLoop(), ImGuiSelectionBasicStorage, DemoWindowWidgetsSelectionAndMultiSelect(), ExampleDualListBox, IMGUI_CDECL, Items, OptKeepSorted (+14 more)

### Community 159 - "ImFontGlyphRangesBuilder"
Cohesion: 0.15
Nodes (8): ImFontGlyphRangesBuilder::AddRanges(), ImFontGlyphRangesBuilder::AddText(), Clear, ImFontGlyphRangesBuilder, AddRanges, AddText, BuildRanges, UsedChars

### Community 160 - "stbtt_pack_context"
Cohesion: 0.08
Nodes (28): stbrp_context, stbrp_node, stbrp_init_target(), stbrp_pack_rects(), stbrp_rect, h, id, w (+20 more)

### Community 161 - "ttUSHORT"
Cohesion: 0.20
Nodes (24): stbtt__CompareUTF8toUTF16_bigendian_prefix(), stbtt__find_table(), stbtt_FindMatchingFont(), stbtt_FindMatchingFont_internal(), stbtt__get_svg(), stbtt__GetCoverageIndex(), stbtt_GetFontNameString(), stbtt_GetFontOffsetForIndex_internal() (+16 more)

### Community 162 - "What You Must Do When Invoked"
Cohesion: 0.07
Nodes (26): For /graphify add and --watch, For /graphify query, For the commit hook and native CLAUDE.md integration, For --update and --cluster-only, /graphify, Honesty Rules, Interpreter guard for subcommands, Part A - Structural extraction for code files (+18 more)

### Community 163 - "ImGui_ImplGlfw_Data"
Cohesion: 0.07
Nodes (26): GLFWcharfun, GlfwClientApi, GLFWcursorenterfun, GLFWcursorposfun, GLFWkeyfun, GLFWmonitorfun, GLFWmousebuttonfun, GLFWscrollfun (+18 more)

### Community 164 - "ImGuiNextWindowData"
Cohesion: 0.07
Nodes (25): ImGuiNextWindowDataFlags, ImGuiChildFlags, ImGuiCond, ImGuiSizeCallback, ImGuiWindowFlags, ImGuiWindowRefreshFlags, ImGuiNextWindowData, BgAlphaVal (+17 more)

### Community 165 - "Geodesic.hpp"
Cohesion: 0.10
Nodes (4): EllipticFunction(), GeodesicLine, GeodesicLineExact, GEOGRAPHICLIB_EXPORT

### Community 166 - "ImDrawData"
Cohesion: 0.13
Nodes (15): ImDrawData, AddDrawList, Clear, CmdLists, CmdListsCount, DeIndexAllBuffers, DisplayPos, DisplaySize (+7 more)

### Community 167 - "ImGuiPayload"
Cohesion: 0.14
Nodes (11): ImGui::AcceptDragDropPayload(), ImGui::GetDragDropPayload(), ImGuiPayload, Data, DataFrameCount, DataSize, DataType, Delivery (+3 more)

### Community 168 - "ImGuiMetricsConfig"
Cohesion: 0.15
Nodes (13): ImGuiMetricsConfig, HighlightMonitorIdx, HighlightViewportID, ShowDebugLog, ShowDrawCmdBoundingBoxes, ShowDrawCmdMesh, ShowIDStackTool, ShowTablesRects (+5 more)

### Community 169 - "glfw3.h"
Cohesion: 0.08
Nodes (23): GLFWallocatefun, GLFWdeallocatefun, GLFWreallocatefun, GLFWallocator, allocate, deallocate, reallocate, user (+15 more)

### Community 171 - "ExampleAsset"
Cohesion: 0.10
Nodes (17): ImGuiID, ExampleAsset, ID, IMGUI_CDECL, s_current_sort_specs, Type, MyItem, ID (+9 more)

### Community 172 - "json_pointer.hpp"
Cohesion: 0.11
Nodes (25): contains(), convert(), empty(), BasicJsonType, friend, NLOHMANN_BASIC_JSON_TPL_DECLARATION, NLOHMANN_JSON_NAMESPACE_BEGIN, size_t (+17 more)

### Community 173 - "structured_bindings.hpp"
Cohesion: 0.11
Nodes (18): genFIType, GLM_STATIC_ASSERT, compute_abs, compute_abs<float, true>, compute_abs<genFIType, false>, GLM_CONSTEXPR, compute_abs<genFIType, true>, GLM_CONSTEXPR (+10 more)

### Community 174 - "ImGuiDemoWindowData"
Cohesion: 0.09
Nodes (22): ImGuiDemoWindowData, DemoTree, DisableSections, ShowAbout, ShowAppAssetsBrowser, ShowAppAutoResize, ShowAppConsole, ShowAppConstrainedResize (+14 more)

### Community 175 - "MyDocument"
Cohesion: 0.14
Nodes (13): ExampleAppDocuments, CloseQueue, Documents, RenamingDoc, RenamingStarted, MyDocument, Color, Dirty (+5 more)

### Community 176 - "ImGuiInputTextCallbackData"
Cohesion: 0.09
Nodes (19): ImGuiInputTextFlags, ImGuiKey, ImGuiInputTextCallbackData, Buf, BufDirty, BufSize, BufTextLen, Ctx (+11 more)

### Community 177 - "ImGuiIDStackTool"
Cohesion: 0.12
Nodes (14): ImGuiIDStackTool, CopyToClipboardLastTime, CopyToClipboardOnCtrlC, LastActiveFrame, QueryId, ResultPathBuf, Results, StackLevel (+6 more)

### Community 178 - "ordered_map.hpp"
Cohesion: 0.16
Nodes (19): Allocator, initializer_list, It, key_type, NLOHMANN_JSON_NAMESPACE_BEGIN, namespace(), count(), emplace() (+11 more)

### Community 179 - "Interpolation.cpp"
Cohesion: 0.26
Nodes (20): alignPolylineDirection(), calculateTrackCenter(), SplinePoint, vec2, vector, douglasPeuckerRecursive(), filterPointsByDistance(), generateTriangleStripFromEdges() (+12 more)

### Community 180 - "AddLine"
Cohesion: 0.10
Nodes (36): getTrackRenderOffset(), ImFont, mat4, string, DrawName(), ImFont, Render(), ImVec2 (+28 more)

### Community 181 - "ui_scale.cpp"
Cohesion: 0.19
Nodes (15): clamp_scale(), commit_pending_now(), GLFWmonitor, GLFWwindow, init(), load_user_scale(), monitor_under_window(), on_content_scale_changed() (+7 more)

### Community 182 - "json_sax_dom_callback_parser"
Cohesion: 0.14
Nodes (15): BasicJsonType* handle_value(Value&& v)(), handle_diagnostic_positions_for_json_value(), BasicJsonType, JSON_HEDLEY_RETURNS_NON_NULL, json_sax_dom_callback_parser, allow_exceptions, callback, discarded (+7 more)

### Community 183 - "stbi_uc"
Cohesion: 0.12
Nodes (21): load_jpeg_image(), resample_row_1(), stbi__at_eof(), stbi__blinn_8x8(), stbi__clamp(), stbi__compute_y(), stbi__copyval(), stbi__hdr_convert() (+13 more)

### Community 184 - "ExampleTreeNode"
Cohesion: 0.13
Nodes (15): ImGuiTextFilter, ExampleAppPropertyEditor, Filter, VisibleNode, ExampleTree_DestroyNode(), ExampleTreeNode, Childs, DataMyBool (+7 more)

### Community 186 - "get"
Cohesion: 0.22
Nodes (10): add(), get(), char_int_type, NLOHMANN_JSON_NAMESPACE_BEGIN, namespace(), scan(), skip_bom(), skip_whitespace() (+2 more)

### Community 187 - "Telemetry ingest pipeline (processIncomingTelemetry -> g_vehicles)"
Cohesion: 0.10
Nodes (20): Invariant: one ingest path for all sources, DataSource abstraction (ServerSource / LocalReceiverSource), Exceptions caught at module boundaries, Minimal header surface, One function, one job, Expected failures return optional / expected, Validate inputs at the public entry point, ARM64 dev build target (+12 more)

### Community 188 - "ImDrawCmd"
Cohesion: 0.11
Nodes (16): ImDrawCallback, ImTextureID, ImDrawCmd, ClipRect, ElemCount, IdxOffset, TextureId, UserCallback (+8 more)

### Community 189 - "ImGuiTextFilter"
Cohesion: 0.13
Nodes (12): Build, ImGuiTextFilter, Build, CountGrep, Draw, Filters, ImGuiTextFilter::ImGuiTextRange::split(), InputBuf (+4 more)

### Community 190 - "stbtt__buf"
Cohesion: 0.38
Nodes (19): stbtt__buf_get(), stbtt__buf_get8(), stbtt__buf_peek8(), stbtt__buf_range(), stbtt__buf_seek(), stbtt__buf_skip(), stbtt__cff_get_index(), stbtt__cff_index_count() (+11 more)

### Community 191 - "ImPool"
Cohesion: 0.16
Nodes (6): ImPoolIdx, ImPool, AliveCount, Buf, FreeIdx, Map

### Community 192 - "sax_parse"
Cohesion: 0.23
Nodes (22): InputType, IteratorType, json, JSON_HEDLEY_WARN_UNUSED_RESULT, NLOHMANN_BASIC_JSON_TPL, accept(), end_pos(), from_bjdata() (+14 more)

### Community 193 - "ImGuiShrinkWidthItem"
Cohesion: 0.50
Nodes (4): ImGuiShrinkWidthItem, Index, InitialWidth, Width

### Community 194 - "stbi__zbuf"
Cohesion: 0.30
Nodes (18): stbi__bit_reverse(), stbi__bitreverse16(), stbi__compute_huffman_codes(), stbi__fill_bits(), stbi__parse_huffman_block(), stbi__parse_uncompressed_block(), stbi__parse_zlib(), stbi__parse_zlib_header() (+10 more)

### Community 195 - "SettingsPanel.cpp"
Cohesion: 0.15
Nodes (14): ImFont, string, EditRow, group, id, id_edit, last_seen, name (+6 more)

### Community 196 - "vec<3, T, Q>"
Cohesion: 0.12
Nodes (16): GLM_CONSTEXPR vec(), E0, E1, E2, GLM_DEFAULT, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_DEFAULTED_FUNC_DECL (+8 more)

### Community 197 - "handle_value"
Cohesion: 0.24
Nodes (3): handle_value(), pair, size_t

### Community 198 - "json_sax_acceptor"
Cohesion: 0.15
Nodes (3): number_float_t, string_t, json_sax_acceptor

### Community 199 - "A1: SECTORS panel still computes sectors from the sample log"
Cohesion: 0.14
Nodes (15): A1: SECTORS panel still computes sectors from the sample log, A2: GPS fix type labelled against the protocol, Pro::AnalysisLap (operator-selected lap), B2: TRACK REPORT and SECTORS ignore the operator-selected lap, D3: SECTORS scans all vehicles and all samples every frame, Recommended fix order (A2, A1, A3/A4, B2, C1, B1/B5), Review severity scale A/B/C/D, Invariant: timing anchored to source timestamps, not frames (+7 more)

### Community 200 - "_noise.hpp"
Cohesion: 0.38
Nodes (15): GLM_FUNC_QUALIFIER, Q, vec, mod289(), permute(), taylorInvSqrt(), vec<2, T, Q> fade(), vec<2, T, Q> permute() (+7 more)

### Community 201 - ".operator -="
Cohesion: 0.17
Nodes (9): _apply_op(), GLM_FUNC_QUALIFIER, vec, op_div, op_equal, op_minus, op_mul, op_plus (+1 more)

### Community 203 - "vec<2, T, Q>"
Cohesion: 0.12
Nodes (15): E0, E1, GLM_DEFAULT, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_DEFAULTED_FUNC_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL (+7 more)

### Community 204 - "compatibility.hpp"
Cohesion: 0.35
Nodes (15): atan2(), GLM_FUNC_QUALIFIER, Q, vec, lerp(), saturate(), vec<2, T, Q> atan2(), vec<2, T, Q> lerp() (+7 more)

### Community 205 - "stbtt__run_charstring"
Cohesion: 0.61
Nodes (8): stbtt__csctx_close_shape(), stbtt__csctx_rccurve_to(), stbtt__csctx_rline_to(), stbtt__csctx_rmove_to(), stbtt__csctx_v(), stbtt__run_charstring(), stbtt__track_vertex(), stbtt__csctx

### Community 206 - "CsvMapping"
Cohesion: 0.15
Nodes (12): CsvSpeedUnit, CsvTimeFormat, CsvMapping, column, speed_unit, time_format, vehicle_id, detect_time_format() (+4 more)

### Community 207 - "ImGuiKeyRoutingData"
Cohesion: 0.13
Nodes (14): ImGuiKeyRoutingIndex, ImGui::UpdateKeyRoutingTable(), ImU16, ImGuiKeyRoutingData, Mods, NextEntryIndex, RoutingCurr, RoutingCurrScore (+6 more)

### Community 208 - "mat<2, 2, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 209 - "mat<2, 3, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 210 - "mat<2, 4, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 211 - "mat<3, 2, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 212 - "mat<3, 3, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 213 - "mat<3, 4, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 214 - "mat<4, 3, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 215 - "mat<4, 4, T, Q>"
Cohesion: 0.13
Nodes (14): col_type, GLM_CTOR_DECL, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type (+6 more)

### Community 216 - "create"
Cohesion: 0.24
Nodes (12): BasicJsonContext, create(), exception, position_t, size_t, string, invalid_iterator, other_error (+4 more)

### Community 217 - "Math.hpp"
Cohesion: 0.21
Nodes (7): AuxAngle(), AuxAngle::degrees(), AuxAngle::lam(), AuxAngle::lamd(), AuxAngle::radians(), AuxLatitude(), DAuxLatitude()

### Community 218 - "imgui/imgui_impl_opengl3_loader.h"
Cohesion: 0.26
Nodes (12): close_libgl(), get_proc(), GL3WGetProcAddressProc, GL3WglProc, imgl3wGetProcAddress(), imgl3wInit(), imgl3wInit2(), is_library_loaded() (+4 more)

### Community 219 - "ImSpanAllocator"
Cohesion: 0.15
Nodes (8): GetSpan(), ImSpan(), ImSpanAllocator, BasePtr, CurrIdx, CurrOff, Offsets, Sizes

### Community 220 - "imstb_rectpack.h"
Cohesion: 0.31
Nodes (11): stbrp_context, stbrp_node, stbrp_init_target(), stbrp_pack_rects(), stbrp_setup_allow_out_of_mem(), stbrp_setup_heuristic(), stbrp__skyline_find_best_pos(), stbrp__skyline_find_min_y() (+3 more)

### Community 221 - "stbi__parse_png_file"
Cohesion: 0.24
Nodes (14): stbi__compute_transparency(), stbi__compute_transparency16(), stbi__create_png_alpha_expand8(), stbi__create_png_image(), stbi__create_png_image_raw(), stbi__de_iphone(), stbi__expand_png_palette(), stbi__get_chunk_header() (+6 more)

### Community 222 - "CsvTable"
Cohesion: 0.15
Nodes (12): D6: CSV import holds the whole file as strings, CsvTable, columns, comma_decimal, data_rows, delimiter, first_data_line, rows (+4 more)

### Community 223 - "OSGB.hpp"
Cohesion: 0.27
Nodes (10): CentralScale(), EquatorialRadius(), FalseEasting(), FalseNorthing(), Flattening(), Forward(), OriginLatitude(), OriginLongitude() (+2 more)

### Community 224 - "Ellipsoid3.hpp"
Cohesion: 0.11
Nodes (6): G, Cartesian3::cart2rand(), vec3, GeodesicLine3, Conformal3, GeodesicLine3

### Community 225 - "Utility.hpp"
Cohesion: 0.24
Nodes (9): fract(), fractionalyear(), string, nummatch(), str(), Utility::str<Math::real>(), Utility::val<bool>(), Utility::val<std::string>() (+1 more)

### Community 226 - "mat<4, 2, T, Q>"
Cohesion: 0.15
Nodes (12): col_type, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, GLM_NOEXCEPT, length_type, mat<4, 2, T, Q> (+4 more)

### Community 227 - "qua"
Cohesion: 0.15
Nodes (12): GLM_CTOR_DECL, GLM_DEFAULT, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_DEFAULTED_FUNC_DECL, GLM_FUNC_DECL, length_type, qua (+4 more)

### Community 228 - "tdualquat"
Cohesion: 0.15
Nodes (12): GLM_CTOR_DECL, GLM_DEFAULT, GLM_DEFAULTED_FUNC_DECL, GLM_FUNC_DECL, length_type, Q, qua, tdualquat (+4 more)

### Community 230 - "ingest_wire_packet"
Cohesion: 0.22
Nodes (10): capture_keyframe_if_due(), player_loop(), thin_keyframes(), player_loop(), TelemetryLogSource, TelemetryPacket, ingest_start(), ingest_stop() (+2 more)

### Community 231 - "DST"
Cohesion: 0.17
Nodes (10): fft_t, DST, DST, _fft, fft_transform, fft_transform2, GEOGRAPHICLIB_EXPORT, _nN (+2 more)

### Community 232 - "_swizzle_base2"
Cohesion: 0.38
Nodes (12): N, E0, E1, E2, E3, Q, value, Stub (+4 more)

### Community 233 - "protocol.h"
Cohesion: 0.18
Nodes (4): timeOut, elapsedTime_ms, initTimer, timeval

### Community 235 - "vec<1, T, Q>"
Cohesion: 0.17
Nodes (11): GLM_DEFAULT, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_DEFAULTED_FUNC_DECL, GLM_FUNC_DECL, GLM_FUNC_DISCARD_DECL, length_type, vec<1, T, Q> (+3 more)

### Community 236 - "ImGuiDebugAllocInfo"
Cohesion: 0.18
Nodes (11): ImGui::DebugAllocHook(), ImS16, ImGuiDebugAllocEntry, AllocCount, FrameCount, FreeCount, ImGuiDebugAllocInfo, LastEntriesBuf (+3 more)

### Community 237 - "ImGuiInputEventMouseButton"
Cohesion: 0.17
Nodes (12): ImGuiMouseSource, ImGuiInputEventMouseButton, Down, MouseSource, ImGuiInputEventMousePos, MouseSource, PosX, PosY (+4 more)

### Community 238 - "json_sax_dom_parser"
Cohesion: 0.18
Nodes (8): lexer_t, vector, json_sax_dom_parser, allow_exceptions, errored, m_lexer_ref, object_element, ref_stack

### Community 239 - "matrix.hpp"
Cohesion: 0.11
Nodes (10): outerProduct_trait, outerProduct_trait<2, 2, T, Q>, outerProduct_trait<2, 3, T, Q>, outerProduct_trait<2, 4, T, Q>, outerProduct_trait<3, 2, T, Q>, outerProduct_trait<3, 3, T, Q>, outerProduct_trait<3, 4, T, Q>, outerProduct_trait<4, 2, T, Q> (+2 more)

### Community 240 - "vec<4, T, Q>"
Cohesion: 0.18
Nodes (10): GLM_DEFAULT, GLM_DEFAULT_CTOR, GLM_DEFAULTED_DEFAULT_CTOR_DECL, GLM_DEFAULTED_FUNC_DECL, GLM_FUNC_DECL, length_type, vec<4, T, Q>, GLM_CONSTEXPR (+2 more)

### Community 241 - "VehicleJournal"
Cohesion: 0.18
Nodes (11): shared_ptr, map, string, vector, replay_journal(), VehicleJournal, best_lap_id, best_lap_time (+3 more)

### Community 242 - "ImGuiContextHook"
Cohesion: 0.20
Nodes (9): ImGuiContextHookCallback, ImGui::AddContextHook(), ImGuiContextHookType, ImGuiContextHook, Callback, HookId, Owner, Type (+1 more)

### Community 243 - "ImTextCharFromUtf8"
Cohesion: 0.15
Nodes (17): ImGuiTypingSelectFlags, ImWchar, ImFont::CalcWordWrapPositionA(), ImGui::DebugTextEncoding(), ImStrlenW(), ImTextCharFromUtf8(), ImTextCharToUtf8(), ImTextCharToUtf8_inline() (+9 more)

### Community 244 - "Sample"
Cohesion: 0.20
Nodes (10): Sample, accel, fix, g_lat, g_long, lat, lon, speed_mps (+2 more)

### Community 245 - "TrackEditor.cpp"
Cohesion: 0.51
Nodes (9): CloseLoop(), vec2, vector, EndGapMeters(), MeasureWidth(), metersToNorm(), normToMeters(), PolylineLengthMeters() (+1 more)

### Community 246 - "UserInfo"
Cohesion: 0.20
Nodes (11): adminResponses(), consumePendingTrack(), vec2, vector, string, jsonPointArray(), UserInfo, expires (+3 more)

### Community 247 - "RaceStatusBar.cpp"
Cohesion: 0.27
Nodes (10): ModeManager, DrawBorderedRectangle, DrawRectangle, DrawText, Initialize, OnScreenResized, RaceStatusBar::RaceStatusBar(), RecalculateDimensions (+2 more)

### Community 248 - "graphify reference: extra exports and benchmark"
Cohesion: 0.22
Nodes (8): graphify reference: extra exports and benchmark, Step 6b - Wiki (only if --wiki flag), Step 7 - Neo4j export (only if --neo4j or --neo4j-push flag), Step 7a - FalkorDB export (only if --falkordb or --falkordb-push flag), Step 7b - SVG export (only if --svg flag), Step 7c - GraphML export (only if --graphml flag), Step 7d - MCP server (only if --mcp flag), Step 8 - Token reduction benchmark (only if total_words > 5000)

### Community 249 - "vec"
Cohesion: 0.22
Nodes (9): F0, F1, E0, E1, E2, E3, GLM_FUNC_DISCARD_DECL, Q (+1 more)

### Community 250 - "ImGuiMultiSelectIO"
Cohesion: 0.12
Nodes (17): ImGuiSelectionRequestType, ImGuiSelectionUserData, ImS8, ImGuiMultiSelectIO, ItemsCount, NavIdItem, NavIdSelected, RangeSrcItem (+9 more)

### Community 251 - "TrackCenterInfo"
Cohesion: 0.25
Nodes (8): vec2, SplinePoint, position, tangent, TrackCenterInfo, geometric_center, is_closed, offset

### Community 252 - "string"
Cohesion: 0.42
Nodes (9): ofstream, string, FinalizeOpenAndSaveTxt(), openOutFile(), sanitizeName(), saveCentreLineLocked(), saveDualEdgeLocked(), SaveFinalizedAsTxt() (+1 more)

### Community 253 - "ImGuiListClipperData"
Cohesion: 0.12
Nodes (14): ImGuiListClipper, ImS8, ImGuiListClipperData, ItemsFrozen, ListClipper, LossynessOffset, Ranges, StepNo (+6 more)

### Community 254 - ".parse_error"
Cohesion: 0.67
Nodes (3): Exception, string, parse_error()

### Community 255 - "byte_container_with_subtype.hpp"
Cohesion: 0.29
Nodes (5): byte_container_with_subtype, operator!=(), set_subtype(), subtype(), subtype_type

### Community 256 - "operator->"
Cohesion: 0.25
Nodes (8): difference_type, iter_impl, IterImpl, friend, reference, operator->(), value(), pointer

### Community 257 - "ImGui_ImplGlfw_WndProc"
Cohesion: 0.25
Nodes (8): LPARAM, LRESULT, HWND, ImGuiMouseSource, GetMouseSourceFromMessageExtraInfo(), ImGui_ImplGlfw_WndProc(), UINT, WPARAM

### Community 258 - "init_gentype<genType, GENTYPE_MAT>"
Cohesion: 0.29
Nodes (6): genType, GLM_FUNC_QUALIFIER, init_gentype<genType, GENTYPE_MAT>, GLM_CONSTEXPR, init_gentype<genType, GENTYPE_QUAT>, GLM_CONSTEXPR

### Community 259 - "ImGuiTableInstanceData"
Cohesion: 0.25
Nodes (7): ImGuiTableInstanceData, HoveredRowLast, HoveredRowNext, LastFrozenHeight, LastOuterHeight, LastTopHeadersRowHeight, TableInstanceID

### Community 260 - "ImGuiTextIndex"
Cohesion: 0.25
Nodes (4): ImGuiTextIndex, append, EndOffset, LineOffsets

### Community 261 - "uint8_t"
Cohesion: 0.50
Nodes (8): uint8_t, vector, to_bjdata(), to_bson(), to_cbor(), to_msgpack(), to_ubjson(), output_adapter

### Community 262 - "ImGui_ImplGlfw_OnCanvasSizeChange"
Cohesion: 0.29
Nodes (7): EM_BOOL, EmscriptenFullscreenChangeEvent, EmscriptenUiEvent, EmscriptenWheelEvent, ImGui_ImplEmscripten_FullscreenChangeCallback(), ImGui_ImplEmscripten_WheelCallback(), ImGui_ImplGlfw_OnCanvasSizeChange()

### Community 263 - "GLFWvidmode"
Cohesion: 0.29
Nodes (7): GLFWvidmode, blueBits, greenBits, height, redBits, refreshRate, width

### Community 264 - "_swizzle.hpp"
Cohesion: 0.48
Nodes (6): _swizzle_base0, _buffer, _swizzle_base1<2, T, Q, E0,E1,-1,-2, false>, _swizzle_base1<3, T, Q, E0,E1,E2,3, false>, _swizzle_base1<4, T, Q, E0,E1,E2,E3, false>, _swizzle_base1<N, T, Q, E0, E1, E2, E3, false>

### Community 265 - "ImGuiStyleVarInfo"
Cohesion: 0.21
Nodes (11): ImGuiStyleVar, ImGui::GetStyleVarInfo(), ImGui::PopStyleVar(), ImGui::PushStyleVar(), ImGui::PushStyleVarX(), ImGui::PushStyleVarY(), ImGuiDataType, ImGuiStyleVarInfo (+3 more)

### Community 266 - "RenderRaceStatusBar"
Cohesion: 0.24
Nodes (6): string, FormatTime, GetSessionTimeString, GetSystemTimeString, ModeManager, RenderRaceStatusBar

### Community 267 - "hex_bytes"
Cohesion: 0.29
Nodes (7): decode(), hex_bytes(), NLOHMANN_JSON_NAMESPACE_BEGIN, string, uint8_t, namespace(), uint32_t

### Community 268 - "graphify reference: query, path, explain"
Cohesion: 0.33
Nodes (5): For /graphify explain, For /graphify path, graphify reference: query, path, explain, Step 0 — Constrained query expansion (REQUIRED before traversal), Step 1 — Traversal

### Community 269 - "Karpathy Guidelines"
Cohesion: 0.33
Nodes (5): 1. Think Before Coding, 2. Simplicity First, 3. Surgical Changes, 4. Goal-Driven Execution, Karpathy Guidelines

### Community 270 - "readarray"
Cohesion: 0.40
Nodes (6): IntT, istream, ostream, vector, readarray(), writearray()

### Community 271 - "ExampleMemberInfo"
Cohesion: 0.33
Nodes (6): ImGuiDataType, ExampleMemberInfo, DataCount, DataType, Name, Offset

### Community 272 - "TelemetryTrackBuilder.h"
Cohesion: 0.22
Nodes (8): MapOrigin, Settings, closeRadiusNorm, minLoopLengthMeters, minPointDistanceNorm, minPointsToClose, splinePointsPerSegment, SplinePoint

### Community 273 - "dump_integer"
Cohesion: 0.40
Nodes (6): dump_integer(), number_integer_t, number_unsigned_t, NumberType, is_negative_number(), remove_sign()

### Community 274 - "glm_i128_interleave"
Cohesion: 0.60
Nodes (4): glm_uvec4, glm_i128_interleave(), glm_i128_interleave2(), GLM_FUNC_QUALIFIER

### Community 275 - "ImGuiKeyData"
Cohesion: 0.40
Nodes (5): ImGuiKeyData, AnalogValue, Down, DownDuration, DownDurationPrev

### Community 276 - "DemoWindowTables"
Cohesion: 0.25
Nodes (9): ImGuiTableColumnFlags, ImGuiTableFlags, DemoWindowTables(), EditTableColumnsFlags(), EditTableSizingFlags(), PopStyleCompact(), PushStyleCompact(), ShowTableColumnsStatusFlags() (+1 more)

### Community 277 - "graphify reference: add a URL and watch a folder"
Cohesion: 0.50
Nodes (3): For /graphify add, For --watch, graphify reference: add a URL and watch a folder

### Community 278 - "graphify reference: commit hook and native CLAUDE.md integration"
Cohesion: 0.50
Nodes (3): For git commit hook, For native CLAUDE.md integration, graphify reference: commit hook and native CLAUDE.md integration

### Community 279 - "graphify reference: incremental update and cluster-only"
Cohesion: 0.50
Nodes (3): For --cluster-only, For --update (incremental re-extraction), graphify reference: incremental update and cluster-only

### Community 280 - "dump_float"
Cohesion: 0.50
Nodes (4): false_type, dump_float(), number_float_t, true_type

### Community 281 - "ImGui::GetAllocatorFunctions"
Cohesion: 0.67
Nodes (4): ImGuiMemAllocFunc, ImGuiMemFreeFunc, ImGui::GetAllocatorFunctions(), ImGui::SetAllocatorFunctions()

### Community 282 - "CsvChannel"
Cohesion: 0.50
Nodes (4): CsvChannel, csv_channel_hint(), csv_channel_label(), csv_channel_required()

### Community 283 - "GeographicErr"
Cohesion: 0.50
Nodes (3): GeographicErr, string, runtime_error

### Community 284 - "C1: two locking idioms on one mutex"
Cohesion: 0.29
Nodes (7): C1: two locking idioms on one mutex, C3: backward seek holds the vehicles mutex, forward seek does not, VehiclesLock (reentrant bulk-section lock), Prefer a queue to a mutex, Shared data must be protected, VehiclesLock, owns_

### Community 285 - "ImGuiColorMod"
Cohesion: 0.50
Nodes (4): ImGuiCol, ImGuiColorMod, BackupValue, Col

### Community 286 - "ImGuiPlotArrayGetterData"
Cohesion: 0.50
Nodes (3): ImGuiPlotArrayGetterData, Stride, Values

### Community 287 - "from_json"
Cohesion: 0.67
Nodes (4): from_json(), BasicJsonType, to_json(), TargetType

### Community 289 - "JournalBuild"
Cohesion: 0.25
Nodes (8): time_point, JournalBuild, from, index, next_update_ms, running, started, total

### Community 292 - "from_json"
Cohesion: 0.67
Nodes (3): ENUM_TYPE, from_json(), BasicJsonType

### Community 293 - "ImGuiTableColumnSortSpecs"
Cohesion: 0.25
Nodes (7): ImGuiSortDirection, ImS16, ImGuiTableColumnSortSpecs, ColumnIndex, ColumnUserID, SortDirection, SortOrder

### Community 294 - "ImGuiInputEvent"
Cohesion: 0.25
Nodes (7): ImGuiInputEventType, ImGuiInputSource, ImGuiInputEvent, AddedByTestEngine, EventId, Source, Type

### Community 295 - "SectorSnapshot"
Cohesion: 0.29
Nodes (6): GetSectorSnapshot(), SectorSnapshot, delta, hasDelta, live, t

### Community 298 - "namespace"
Cohesion: 0.67
Nodes (3): NLOHMANN_JSON_NAMESPACE_BEGIN, NLOHMANN_JSON_NAMESPACE_END, namespace()

### Community 314 - "TrackTrailerHeader"
Cohesion: 0.33
Nodes (6): TrackTrailerHeader, file_name, format, magic, payload_size, version

### Community 315 - "SyntheticCar"
Cohesion: 0.33
Nodes (6): SyntheticCar, accel_mps2, device_id, seq, speed_mps, target_speed_kph

### Community 316 - "type"
Cohesion: 0.47
Nodes (4): GLM_CONSTEXPR, type, data, glm_f64vec2

### Community 317 - "FindLatestInputEvent"
Cohesion: 0.33
Nodes (6): ImGuiInputEventType, FindLatestInputEvent(), AddFocusEvent, AddKeyAnalogEvent, AddKeyEvent, AddMouseButtonEvent

### Community 318 - "internal_iterator.hpp"
Cohesion: 0.33
Nodes (4): NLOHMANN_JSON_NAMESPACE_BEGIN, namespace(), NLOHMANN_JSON_NAMESPACE_BEGIN, namespace()

### Community 319 - "CarLapSessions"
Cohesion: 0.40
Nodes (5): CarLapSessions, globalLapnumber, lapnumber, samples, vector

### Community 321 - "ImGuiResizeGripDef"
Cohesion: 0.40
Nodes (5): ImGuiResizeGripDef, AngleMax12, AngleMin12, CornerPosN, InnerDir

### Community 322 - "stbtt_GetKerningTable"
Cohesion: 0.40
Nodes (5): stbtt_GetKerningTable(), stbtt_kerningentry, advance, glyph1, glyph2

### Community 323 - "FrameStat"
Cohesion: 0.50
Nodes (4): FrameStat, arrival_ms, clock_diff, lost_before

### Community 324 - "ImGuiStyleMod"
Cohesion: 0.67
Nodes (3): ImGuiStyleVar, ImGuiStyleMod, VarIdx

### Community 327 - "openDevice"
Cohesion: 0.50
Nodes (4): openDevice, SerialDataBits, SerialParity, SerialStopBits

## Knowledge Gaps
- **2517 isolated node(s):** `OpenGL`, `magic`, `version`, `origin_easting`, `origin_northing` (+2512 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 3510 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **17 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `T()` connect `T` to `ImGuiID`, `_swizzle.hpp`, `Constants.hpp`, `_matrix_vectorize.hpp`, `ImVec4`, `PolygonAreaT`, `imgui_widgets.cpp`, `ordered_map.hpp`, `type`, `vector`, `json.hpp`, `ImPool`, `qualifier.hpp`, `sax_parse`, `ImGuiTextBuffer`, `vec<3, T, Q>`, `imgui.h`, `ImMax`, `_noise.hpp`, `.operator -=`, `vec<2, T, Q>`, `compatibility.hpp`, `type_ptr.hpp`, `ImSpanAllocator`, `compute_vector_decl.hpp`, `Utility.hpp`, `tdualquat`, `ImChunkStream`, `_swizzle_base2`, `imgui_internal.h`, `_vectorize.hpp`, `vec`, `gtc/quaternion.hpp`, `AngleT`?**
  _High betweenness centrality (0.226) - this node is a cross-community bridge._
- **Why does `ImGuiContext` connect `ImGuiContext` to `ImGuiID`, `imgui_tables.cpp`, `ImGuiTextIndex`, `ImGuiNavItemData`, `ImGuiLastItemData`, `ImVec4`, `ImGuiColorMod`, `ImGuiNextWindowData`, `ImGuiInputEvent`, `ImGuiPayload`, `ImGuiMetricsConfig`, `imgui.cpp`, `ImGuiIDStackTool`, `imgui_widgets.cpp`, `ImPool`, `ImGuiShrinkWidthItem`, `ImGuiTextBuffer`, `ImGuiStyleMod`, `ImMax`, `ImDrawList`, `imstb_textedit.h`, `ImGuiKeyRoutingData`, `ImGui::ShowMetricsWindow`, `ImGuiNextItemData`, `ImGuiViewportP`, `ImGuiBoxSelectState`, `ImGuiID`, `ImGuiWindowSettings`, `ImChunkStream`, `ImGuiDebugAllocInfo`, `ImGuiInputEventMouseButton`, `ImGuiContextHook`, `imgui_internal.h`, `ImGuiViewport`, `~ImVector`, `ImGuiListClipperData`?**
  _High betweenness centrality (0.072) - this node is a cross-community bridge._
- **Why does `ImPool` connect `ImPool` to `T`, `ImGuiID`, `ImGuiContext`, `ImGuiNavItemData`, `imgui_internal.h`, `~ImVector`, `ImGuiID`?**
  _High betweenness centrality (0.034) - this node is a cross-community bridge._
- **What connects `OpenGL`, `magic`, `version` to the rest of the system?**
  _2517 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `UI` be split into smaller, more focused modules?**
  _Cohesion score 0.02477017364657814 - nodes in this community are weakly interconnected._
- **Should `TrackReviewPanel.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.06874669487043893 - nodes in this community are weakly interconnected._
- **Should `Vehicle` be split into smaller, more focused modules?**
  _Cohesion score 0.03508771929824561 - nodes in this community are weakly interconnected._