#include "TrackReviewPanel.h"

#include "../Config.h"
#include "../rendering/Interpolation.h"
#include "../track/TelemetryTrackBuilder.h"
#include "../track/TrackEditor.h"
#include "ui_scale.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	// ── App palette (same colors as the rest of the UI) ─────────────────────
	constexpr ImU32 COL_BG        = IM_COL32(13, 13, 13, 255);
	constexpr ImU32 COL_PANEL     = IM_COL32(0x20, 0x20, 0x20, 255);
	constexpr ImU32 COL_TITLE_BG  = IM_COL32(0x29, 0x29, 0x29, 255);
	constexpr ImU32 COL_GOLD      = IM_COL32(0xDA, 0xA5, 0x40, 255);
	constexpr ImU32 COL_TEXT      = IM_COL32(235, 235, 235, 255);
	constexpr ImU32 COL_TEXT_DIM  = IM_COL32(0x8A, 0x8A, 0x8A, 255);
	constexpr ImU32 COL_RAW_GHOST = IM_COL32(0x55, 0x55, 0x55, 150);
	constexpr ImU32 COL_EDGE      = IM_COL32(0xDC, 0xDC, 0xDC, 255);
	constexpr ImU32 COL_ASPHALT   = IM_COL32(0x2A, 0x2A, 0x2A, 255);
	constexpr ImU32 COL_WARN_RED  = IM_COL32(0xCE, 0x2B, 0x2B, 255);
	constexpr ImU32 COL_OK_GREEN  = IM_COL32(0x6E, 0xF9, 0x8E, 255);

	// ── Tuning constants ────────────────────────────────────────────────────
	constexpr int   EDGE_POINT_COUNT = 200;   // points per edge after cleanup
	constexpr float MAX_CLOSE_GAP_M  = 5.0f;  // ends farther apart stay open
	constexpr float CLOSE_BLEND_M    = 15.0f; // gap is spread over this length
	constexpr float PICK_RADIUS_PT   = 12.0f; // mouse-to-point grab distance (пункты × DPI)
	constexpr float MIN_TRACK_WIDTH_M = 0.5f; // narrower sections get a red warning mark
	constexpr int   MAX_UNDO_STEPS   = 50;
	constexpr float SIDEBAR_WIDTH_PT = 300.0f;
	constexpr float HANDLE_SPACING_PT = 18.0f; // target on-screen distance between handles

	float pick_radius_px()    { return ui_scale::points(PICK_RADIUS_PT); }
	float handle_spacing_px() { return ui_scale::points(HANDLE_SPACING_PT); }

	// One undo step = both edges as they were before a drag started.
	struct EdgeSnapshot
	{
		std::vector<glm::vec2> left;
		std::vector<glm::vec2> right;
	};

	struct PanelState
	{
		bool loaded = false;              // raw edges pulled from the recorder

		std::vector<glm::vec2> rawLeft;   // as recorded (ghost display)
		std::vector<glm::vec2> rawRight;
		std::vector<glm::vec2> leftEdge;  // cleaned + user-edited result
		std::vector<glm::vec2> rightEdge;

		float smoothingCm = 4.0f;         // tolerance slider value
		bool  loopClosed  = false;        // both edges welded start-to-end
		float leftGapM    = 0.0f;         // start/end gap before welding
		float rightGapM   = 0.0f;

		float lengthM   = 0.0f;           // centre-line length
		float minWidthM = 0.0f;
		float maxWidthM = 0.0f;

		// 2D viewport (world = normalized map units)
		glm::vec2 viewCenter{ 0.0f, 0.0f };
		float     viewScale = 1.0f;       // pixels per world unit
		bool      viewFitted = false;

		// Point editing
		bool  editMode      = false;
		bool  moveBothEdges = false;      // drag shifts the whole road, width kept
		float dragRadiusM   = 5.0f;       // proportional-editing influence radius
		int   dragEdge      = -1;         // 0 = left, 1 = right, -1 = none
		int   dragIndex     = -1;
		std::vector<EdgeSnapshot> undoStack;
		std::vector<EdgeSnapshot> redoStack;
	};

	PanelState s_panel;

	float normToMeters(float norm) { return norm * (float)MapConstants::MAP_SIZE; }

	// ── Cleanup pipeline: raw edges → smoothed, aligned, welded edges ───────
	void RebuildEdgesFromRaw()
	{
		std::vector<glm::vec2> left  = TrackEditor::SmoothEdge(s_panel.rawLeft,  s_panel.smoothingCm / 100.0f);
		std::vector<glm::vec2> right = TrackEditor::SmoothEdge(s_panel.rawRight, s_panel.smoothingCm / 100.0f);

		s_panel.leftGapM  = TrackEditor::EndGapMeters(left);
		s_panel.rightGapM = TrackEditor::EndGapMeters(right);
		const bool leftClosed  = TrackEditor::CloseLoop(left,  MAX_CLOSE_GAP_M, CLOSE_BLEND_M);
		const bool rightClosed = TrackEditor::CloseLoop(right, MAX_CLOSE_GAP_M, CLOSE_BLEND_M);
		s_panel.loopClosed = leftClosed && rightClosed;

		// Equal point counts + matching direction: needed for the width
		// stats, the asphalt preview and the .trk2 save format.
		left  = resamplePolyline(left,  EDGE_POINT_COUNT);
		right = resamplePolyline(right, EDGE_POINT_COUNT);
		alignPolylineDirection(right, left);

		s_panel.leftEdge  = std::move(left);
		s_panel.rightEdge = std::move(right);
		s_panel.undoStack.clear();
	}

	void RecalcStats()
	{
		std::vector<glm::vec2> centre(s_panel.leftEdge.size());
		for (size_t i = 0; i < centre.size(); ++i)
			centre[i] = (s_panel.leftEdge[i] + s_panel.rightEdge[i]) * 0.5f;
		s_panel.lengthM = TrackEditor::PolylineLengthMeters(centre);
		TrackEditor::MeasureWidth(s_panel.leftEdge, s_panel.rightEdge,
		                          s_panel.minWidthM, s_panel.maxWidthM);
	}

	void LoadFromRecorder()
	{
		s_panel = PanelState{};
		s_panel.rawLeft  = TelemetryTrackBuilder::GetLeftEdgeSnapshot();
		s_panel.rawRight = TelemetryTrackBuilder::GetRawPointsSnapshot();
		RebuildEdgesFromRaw();
		RecalcStats();
		s_panel.loaded = true;
	}

	// ── World ↔ screen transform ────────────────────────────────────────────
	ImVec2 WorldToScreen(glm::vec2 world, ImVec2 canvasCenter)
	{
		return { canvasCenter.x + (world.x - s_panel.viewCenter.x) * s_panel.viewScale,
		         canvasCenter.y - (world.y - s_panel.viewCenter.y) * s_panel.viewScale };
	}

	glm::vec2 ScreenToWorld(ImVec2 screen, ImVec2 canvasCenter)
	{
		return { s_panel.viewCenter.x + (screen.x - canvasCenter.x) / s_panel.viewScale,
		         s_panel.viewCenter.y - (screen.y - canvasCenter.y) / s_panel.viewScale };
	}

	void FitViewToTrack(ImVec2 canvasSize)
	{
		if (s_panel.rawLeft.empty()) return;
		glm::vec2 lo = s_panel.rawLeft[0], hi = lo;
		auto extend = [&](const std::vector<glm::vec2>& pts) {
			for (const glm::vec2& p : pts) {
				lo = glm::min(lo, p);
				hi = glm::max(hi, p);
			}
		};
		extend(s_panel.rawLeft);
		extend(s_panel.rawRight);

		const glm::vec2 range = glm::max(hi - lo, glm::vec2(1e-6f));
		s_panel.viewCenter = (lo + hi) * 0.5f;
		s_panel.viewScale  = std::min((canvasSize.x - 80.0f) / range.x,
		                              (canvasSize.y - 80.0f) / range.y);
		s_panel.viewFitted = true;
	}

	// ── Undo / redo ─────────────────────────────────────────────────────────
	// A snapshot is taken when a drag starts; a new action clears the redo
	// history (the standard editor convention).
	void PushUndoSnapshot()
	{
		if (s_panel.undoStack.size() >= MAX_UNDO_STEPS)
			s_panel.undoStack.erase(s_panel.undoStack.begin());
		s_panel.undoStack.push_back({ s_panel.leftEdge, s_panel.rightEdge });
		s_panel.redoStack.clear();
	}

	void Undo()
	{
		if (s_panel.undoStack.empty()) return;
		s_panel.redoStack.push_back({ s_panel.leftEdge, s_panel.rightEdge });
		s_panel.leftEdge  = std::move(s_panel.undoStack.back().left);
		s_panel.rightEdge = std::move(s_panel.undoStack.back().right);
		s_panel.undoStack.pop_back();
		RecalcStats();
	}

	void Redo()
	{
		if (s_panel.redoStack.empty()) return;
		s_panel.undoStack.push_back({ s_panel.leftEdge, s_panel.rightEdge });
		s_panel.leftEdge  = std::move(s_panel.redoStack.back().left);
		s_panel.rightEdge = std::move(s_panel.redoStack.back().right);
		s_panel.redoStack.pop_back();
		RecalcStats();
	}

	// Esc during a drag: throw the change away and restore the pre-drag state.
	void CancelDrag()
	{
		if (s_panel.dragEdge < 0 || s_panel.undoStack.empty()) return;
		s_panel.leftEdge  = s_panel.undoStack.back().left;
		s_panel.rightEdge = s_panel.undoStack.back().right;
		s_panel.undoStack.pop_back();
		s_panel.dragEdge  = -1;
		s_panel.dragIndex = -1;
		RecalcStats();
	}

	// ── Point editing ───────────────────────────────────────────────────────
	// Handle density adapts to zoom: aim for handle_spacing_px() between
	// handles on screen, so zooming in exposes finer control.
	int HandleStep(const std::vector<glm::vec2>& pts)
	{
		if (pts.size() < 2) return 1;
		float lengthNorm = 0.0f;
		for (size_t i = 1; i < pts.size(); ++i)
			lengthNorm += glm::distance(pts[i], pts[i - 1]);
		const float spacingPx = (lengthNorm / (float)pts.size()) * s_panel.viewScale;
		if (spacingPx <= 0.001f) return 8;
		const int step = (int)(handle_spacing_px() / spacingPx);
		return std::clamp(step, 1, 8);
	}

	// Finds the edit handle closest to the mouse. Returns false if none is
	// within pick_radius_px().
	bool FindPointUnderMouse(ImVec2 mouse, ImVec2 canvasCenter, int& outEdge, int& outIndex)
	{
		float bestDist = pick_radius_px();
		outEdge = -1; outIndex = -1;
		const std::vector<glm::vec2>* edges[2] = { &s_panel.leftEdge, &s_panel.rightEdge };
		for (int e = 0; e < 2; ++e)
		{
			const int step = HandleStep(*edges[e]);
			for (int i = 0; i < (int)edges[e]->size(); i += step)
			{
				const ImVec2 p = WorldToScreen((*edges[e])[i], canvasCenter);
				const float dist = std::hypot(p.x - mouse.x, p.y - mouse.y);
				if (dist < bestDist) { bestDist = dist; outEdge = e; outIndex = i; }
			}
		}
		return outEdge >= 0;
	}

	// Moves point `index` of one edge by worldDelta. Neighbours within the
	// influence radius (arc distance) follow with a cosine falloff —
	// Blender's proportional editing, so the curve bends without kinks.
	void ApplyProportionalMove(std::vector<glm::vec2>& pts, int index, glm::vec2 worldDelta)
	{
		if (pts.empty()) return;

		const float edgeLengthM = TrackEditor::PolylineLengthMeters(pts);
		const float stepM       = edgeLengthM / (float)pts.size(); // avg distance between points
		if (stepM <= 0.0f) return;

		const int reach = (int)(s_panel.dragRadiusM / stepM);
		const int count = (int)pts.size();
		for (int offset = -reach; offset <= reach; ++offset)
		{
			// The loop is closed, so the influence wraps around the ends.
			const int i = ((index + offset) % count + count) % count;
			const float arcDistM = std::abs(offset) * stepM;
			const float weight = 0.5f * (1.0f + std::cos(3.14159265f * arcDistM / s_panel.dragRadiusM));
			pts[i] += worldDelta * weight;
		}
	}

	void DragPointProportional(int edge, int index, glm::vec2 worldDelta)
	{
		ApplyProportionalMove(edge == 0 ? s_panel.leftEdge : s_panel.rightEdge, index, worldDelta);
		// "Move both edges": the opposite edge follows at the same spot, so
		// the whole road shifts and the width stays untouched.
		if (s_panel.moveBothEdges)
			ApplyProportionalMove(edge == 0 ? s_panel.rightEdge : s_panel.leftEdge, index, worldDelta);
	}

	// ── Drawing helpers ─────────────────────────────────────────────────────
	void DrawPolyline(ImDrawList* dl, const std::vector<glm::vec2>& pts,
	                  ImVec2 canvasCenter, ImU32 color, float thickness, bool closed)
	{
		for (size_t i = 1; i < pts.size(); ++i)
			dl->AddLine(WorldToScreen(pts[i - 1], canvasCenter),
			            WorldToScreen(pts[i], canvasCenter), color, thickness);
		if (closed && pts.size() > 2)
			dl->AddLine(WorldToScreen(pts.back(), canvasCenter),
			            WorldToScreen(pts.front(), canvasCenter), color, thickness);
	}

	// Asphalt preview: filled quads between the edges — the same geometry the
	// final track mesh is built from, so this IS the end result.
	void DrawAsphalt(ImDrawList* dl, ImVec2 canvasCenter)
	{
		const auto& L = s_panel.leftEdge;
		const auto& R = s_panel.rightEdge;
		const size_t count = std::min(L.size(), R.size());
		for (size_t i = 0; i + 1 < count; ++i)
			dl->AddQuadFilled(WorldToScreen(L[i], canvasCenter),
			                  WorldToScreen(L[i + 1], canvasCenter),
			                  WorldToScreen(R[i + 1], canvasCenter),
			                  WorldToScreen(R[i], canvasCenter), COL_ASPHALT);
		if (s_panel.loopClosed && count > 2)
			dl->AddQuadFilled(WorldToScreen(L[count - 1], canvasCenter),
			                  WorldToScreen(L[0], canvasCenter),
			                  WorldToScreen(R[0], canvasCenter),
			                  WorldToScreen(R[count - 1], canvasCenter), COL_ASPHALT);
	}

	// Red cross-lines where the track is narrower than MIN_TRACK_WIDTH_M.
	void DrawNarrowWarnings(ImDrawList* dl, ImVec2 canvasCenter)
	{
		const auto& L = s_panel.leftEdge;
		const auto& R = s_panel.rightEdge;
		const size_t count = std::min(L.size(), R.size());
		for (size_t i = 0; i < count; ++i)
			if (normToMeters(glm::distance(L[i], R[i])) < MIN_TRACK_WIDTH_M)
				dl->AddLine(WorldToScreen(L[i], canvasCenter),
				            WorldToScreen(R[i], canvasCenter), COL_WARN_RED, 2.0f);
	}

	void DrawEditPoints(ImDrawList* dl, ImVec2 canvasCenter, ImVec2 mouse)
	{
		const std::vector<glm::vec2>* edges[2] = { &s_panel.leftEdge, &s_panel.rightEdge };
		for (int e = 0; e < 2; ++e)
		{
			const int step = HandleStep(*edges[e]);
			for (int i = 0; i < (int)edges[e]->size(); i += step)
			{
				const ImVec2 p = WorldToScreen((*edges[e])[i], canvasCenter);
				const bool grabbed = (s_panel.dragEdge == e && s_panel.dragIndex == i);
				const bool hovered = std::hypot(p.x - mouse.x, p.y - mouse.y) < pick_radius_px();
				dl->AddCircleFilled(p, grabbed ? 5.0f : 3.5f,
				                    (grabbed || hovered) ? COL_TEXT : COL_GOLD);
			}
		}
	}

	// Gold circle around the grabbed point showing how far the drag reaches.
	void DrawInfluenceCircle(ImDrawList* dl, ImVec2 canvasCenter)
	{
		if (s_panel.dragEdge < 0) return;
		const std::vector<glm::vec2>& pts =
			(s_panel.dragEdge == 0) ? s_panel.leftEdge : s_panel.rightEdge;
		if (s_panel.dragIndex >= (int)pts.size()) return;

		const ImVec2 center  = WorldToScreen(pts[s_panel.dragIndex], canvasCenter);
		const float radiusPx = (s_panel.dragRadiusM / (float)MapConstants::MAP_SIZE) * s_panel.viewScale;
		dl->AddCircle(center, radiusPx, IM_COL32(0xDA, 0xA5, 0x40, 120), 48, 1.5f);
	}

	// ── Canvas: view control + point dragging ───────────────────────────────
	void HandleCanvasInput(ImVec2 canvasPos, ImVec2 canvasSize, ImVec2 canvasCenter)
	{
		ImGui::SetCursorScreenPos(canvasPos);
		ImGui::InvisibleButton("##ReviewCanvas", canvasSize);
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetMousePos();
		ImGuiIO& io = ImGui::GetIO();

		// Alt+wheel — influence radius; plain wheel — zoom around the cursor
		if (hovered && io.MouseWheel != 0.0f)
		{
			if (s_panel.editMode && io.KeyAlt)
			{
				s_panel.dragRadiusM = std::clamp(
					s_panel.dragRadiusM + (io.MouseWheel > 0.0f ? 1.0f : -1.0f), 1.0f, 20.0f);
			}
			else
			{
				const glm::vec2 anchor = ScreenToWorld(mouse, canvasCenter);
				s_panel.viewScale *= (io.MouseWheel > 0.0f) ? 1.15f : 1.0f / 1.15f;
				const glm::vec2 shifted = ScreenToWorld(mouse, canvasCenter);
				s_panel.viewCenter += anchor - shifted;
			}
		}

		// Pan with the right/middle mouse button
		if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
		                ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
		{
			s_panel.viewCenter.x -= io.MouseDelta.x / s_panel.viewScale;
			s_panel.viewCenter.y += io.MouseDelta.y / s_panel.viewScale;
		}

		if (!s_panel.editMode)
			return;

		// Grab a point
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			int edge, index;
			if (FindPointUnderMouse(mouse, canvasCenter, edge, index))
			{
				PushUndoSnapshot();
				s_panel.dragEdge  = edge;
				s_panel.dragIndex = index;
			}
		}
		// Drag it
		if (s_panel.dragEdge >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const glm::vec2 worldDelta(io.MouseDelta.x / s_panel.viewScale,
			                           -io.MouseDelta.y / s_panel.viewScale);
			DragPointProportional(s_panel.dragEdge, s_panel.dragIndex, worldDelta);
		}
		// Release
		if (s_panel.dragEdge >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			s_panel.dragEdge = -1;
			s_panel.dragIndex = -1;
			RecalcStats();
		}
		// Esc — cancel the drag in progress; Ctrl+Z / Ctrl+Y — undo / redo
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			CancelDrag();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
			Undo();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
			Redo();
	}

	// ── Sidebar ─────────────────────────────────────────────────────────────
	// Gold-styled slider with a label above it. Returns true when the value
	// changed.
	bool GoldSlider(const char* label, const char* id, float* value,
	                float minV, float maxV, const char* format)
	{
		ImGui::TextColored(ImColor(COL_GOLD), "%s", label);
		ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImColor(0x13, 0x13, 0x13).Value);
		ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImColor(COL_GOLD).Value);
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImColor(0xEE, 0xB9, 0x54).Value);
		ImGui::SetNextItemWidth(-1.0f); // fill the sidebar width
		const bool changed = ImGui::SliderFloat(id, value, minV, maxV, format);
		ImGui::PopStyleColor(3);
		return changed;
	}

	void DrawStats(ImFont* monoFont)
	{
		if (monoFont) ImGui::PushFont(monoFont);
		ImGui::SetWindowFontScale(0.45f);
		ImGui::Text("Length %.1f m", s_panel.lengthM);
		ImGui::Text("Width  %.2f - %.2f m", s_panel.minWidthM, s_panel.maxWidthM);
		if (s_panel.loopClosed)
			ImGui::TextColored(ImColor(COL_OK_GREEN), "Loop   closed");
		else
			ImGui::TextColored(ImColor(COL_WARN_RED), "Gap L %.1fm R %.1fm",
			                   s_panel.leftGapM, s_panel.rightGapM);
		if (s_panel.minWidthM < MIN_TRACK_WIDTH_M)
			ImGui::TextColored(ImColor(COL_WARN_RED), "Track too narrow!");
		ImGui::SetWindowFontScale(1.0f);
		if (monoFont) ImGui::PopFont();
	}

	// SAVE / RE-RECORD / DISCARD, pinned to the sidebar bottom.
	void DrawActionButtons()
	{
		const float btnH = ui_scale::points(40.0f);

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button,        ImColor(COL_GOLD).Value);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(0xEE, 0xB9, 0x54).Value);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImColor(0xC6, 0x91, 0x2C).Value);
		ImGui::PushStyleColor(ImGuiCol_Text,          ImColor(0x12, 0x12, 0x12).Value);
		if (ImGui::Button("SAVE TRACK", { -1.0f, btnH }))
		{
			TelemetryTrackBuilder::SubmitReviewedEdges(s_panel.leftEdge, s_panel.rightEdge);
			s_panel = PanelState{};
		}
		ImGui::PopStyleColor(4);

		ImGui::PushStyleColor(ImGuiCol_Button,        ImColor(0x2E, 0x2E, 0x2E).Value);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImColor(0x3A, 0x3A, 0x3A).Value);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImColor(0x26, 0x26, 0x26).Value);
		if (ImGui::Button("RE-RECORD", { -1.0f, btnH }))
		{
			TelemetryTrackBuilder::StartLeftEdge();
			s_panel = PanelState{};
		}
		if (ImGui::Button("DISCARD", { -1.0f, btnH }))
		{
			TelemetryTrackBuilder::Stop();
			s_panel = PanelState{};
		}
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
	}

	void DrawSidebar(ImVec2 pos, ImVec2 size, ImFont* titleFont, ImFont* textFont, ImFont* monoFont)
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(pos, { pos.x + size.x, pos.y + size.y }, COL_PANEL);

		// Title bar with the gold accent stripe (same style as the modals).
		// Явный размер текста — чтобы "TRACK REVIEW" всегда влезал в панель.
		const float titleH  = ui_scale::points(44.0f);
		const float titleFs = ui_scale::points(22.0f);
		dl->AddRectFilled(pos, { pos.x + size.x, pos.y + titleH }, COL_TITLE_BG);
		dl->AddRectFilled({ pos.x, pos.y + 3.0f }, { pos.x + 4.0f, pos.y + titleH - 3.0f }, COL_GOLD, 2.0f);
		ImFont* tf = titleFont ? titleFont : ImGui::GetFont();
		dl->AddText(tf, titleFs, { pos.x + 14.0f, pos.y + (titleH - titleFs) * 0.5f },
		            COL_TEXT, "TRACK REVIEW");

		// A child window gives the controls their own clip rect and normal
		// vertical layout — no absolute cursor math.
		ImGui::SetCursorScreenPos({ pos.x, pos.y + titleH });
		const float pad = ui_scale::points(16.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { pad, pad });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(COL_PANEL).Value);
		ImGui::BeginChild("##ReviewSidebar", { size.x, size.y - titleH }, false,
		                  ImGuiWindowFlags_NoScrollbar);
		if (textFont) ImGui::PushFont(textFont);

		// Smoothing: tolerance in centimeters. Changing it rebuilds the
		// edges from the raw recording (manual edits are discarded).
		if (GoldSlider("Smoothing tolerance", "##smoothing",
		               &s_panel.smoothingCm, 1.0f, 100.0f, "%.0f cm"))
		{
			RebuildEdgesFromRaw();
			RecalcStats();
		}
		ImGui::TextColored(ImColor(COL_TEXT_DIM), "Slider resets manual edits");
		ImGui::Dummy({ 0, 10 });

		// Edit mode + its tools
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImColor(COL_GOLD).Value);
		ImGui::Checkbox("Edit points", &s_panel.editMode);
		if (s_panel.editMode)
		{
			ImGui::Checkbox("Move both edges", &s_panel.moveBothEdges);
			GoldSlider("Influence radius", "##dragRadius",
			           &s_panel.dragRadiusM, 1.0f, 20.0f, "%.0f m");
		}
		ImGui::PopStyleColor();
		ImGui::TextColored(ImColor(COL_TEXT_DIM), "Ctrl+Z undo  Ctrl+Y redo");
		ImGui::TextColored(ImColor(COL_TEXT_DIM), "Esc cancel  Alt+wheel radius");
		ImGui::TextColored(ImColor(COL_TEXT_DIM), "Wheel zoom  RMB pan");
		ImGui::Dummy({ 0, 14 });

		DrawStats(monoFont);

		// Pin the action buttons to the bottom of the sidebar.
		const float buttonsBlockH = 3.0f * ui_scale::points(40.0f)
		                          + 2.0f * ImGui::GetStyle().ItemSpacing.y
		                          + ui_scale::points(16.0f);
		const float fillH = ImGui::GetContentRegionAvail().y - buttonsBlockH;
		if (fillH > 0.0f) ImGui::Dummy({ 0, fillH });
		DrawActionButtons();

		if (textFont) ImGui::PopFont();
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
}

namespace TrackReviewPanel
{
	void Render(ImFont* titleFont, ImFont* textFont, ImFont* monoFont)
	{
		if (TelemetryTrackBuilder::GetPhase() != EdgePhase::Review)
		{
			s_panel.loaded = false; // recorder left Review externally
			return;
		}
		if (!s_panel.loaded)
			LoadFromRecorder();

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowFocus(); // full-screen takeover: stay above other windows
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
		// The app's global style scales ItemSpacing by the window size, which
		// gives enormous gaps; this screen uses its own compact spacing.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
		                    { ui_scale::points(8.0f), ui_scale::points(8.0f) });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
		                    { ui_scale::points(6.0f), ui_scale::points(6.0f) });
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
		                    { ui_scale::points(8.0f), ui_scale::points(6.0f) });
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImColor(COL_BG).Value);
		if (!ImGui::Begin("##TrackReview", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::End();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(4);
			return;
		}

		const ImVec2 canvasPos  = viewport->Pos;
		const float sidebar_w = ui_scale::points(SIDEBAR_WIDTH_PT);
		const ImVec2 canvasSize = { viewport->Size.x - sidebar_w, viewport->Size.y };
		const ImVec2 canvasCenter = { canvasPos.x + canvasSize.x * 0.5f,
		                              canvasPos.y + canvasSize.y * 0.5f };

		if (!s_panel.viewFitted)
			FitViewToTrack(canvasSize);

		HandleCanvasInput(canvasPos, canvasSize, canvasCenter);

		// Draw order: asphalt → raw ghosts → edges → warnings → edit handles
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 mouse = ImGui::GetMousePos();
		DrawAsphalt(dl, canvasCenter);
		DrawPolyline(dl, s_panel.rawLeft,  canvasCenter, COL_RAW_GHOST, 1.0f, false);
		DrawPolyline(dl, s_panel.rawRight, canvasCenter, COL_RAW_GHOST, 1.0f, false);
		DrawPolyline(dl, s_panel.leftEdge,  canvasCenter, COL_EDGE, 2.0f, s_panel.loopClosed);
		DrawPolyline(dl, s_panel.rightEdge, canvasCenter, COL_EDGE, 2.0f, s_panel.loopClosed);
		DrawNarrowWarnings(dl, canvasCenter);
		if (s_panel.editMode)
		{
			DrawEditPoints(dl, canvasCenter, mouse);
			DrawInfluenceCircle(dl, canvasCenter);
		}

		DrawSidebar({ canvasPos.x + canvasSize.x, canvasPos.y },
		            { sidebar_w, viewport->Size.y }, titleFont, textFont, monoFont);

		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(4);
	}
}
