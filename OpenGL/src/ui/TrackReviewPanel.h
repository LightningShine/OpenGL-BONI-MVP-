#pragma once

#include <imgui.h>

// Full-screen review screen shown after both track edges are recorded
// (EdgePhase::Review). The user tunes smoothing, closes the loop, drags
// points, checks the final look — then saves or re-records the track.
namespace TrackReviewPanel
{
	// Call once per frame. Draws nothing unless the recorder is in Review.
	void Render(ImFont* titleFont, ImFont* textFont, ImFont* monoFont);
}
