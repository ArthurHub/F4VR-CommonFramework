#include "DebugDraw.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

#include "../common/CommonUtils.h"
#include "../common/MatrixUtils.h"
#include "../f4vr/PlayerNodes.h"
#include "../vrcf/InputBindingParser.h"
#include "../vrcf/VRControllersManager.h"
#include "DebugDrawRenderer.h"

using namespace common;

namespace f4cf::debug
{
    namespace
    {
        // Havok meters -> game units for the havokToGame() convenience. The engine holds a live
        // scale global but 70 is its steady value; good enough for debug drawing (ROCK
        // PhysicsScale.h:15 kFallbackHavokToGame).
        constexpr float HAVOK_TO_GAME_SCALE = 70.0f;

        // watch-table layout: one "name: value" line per entry, stacked downward
        constexpr float WATCH_TABLE_TEXT_SIZE = 2.0f;
        // glyphs are 7 rows tall at `size` px per row; 9 rows of advance leaves a 2-row gap
        constexpr float WATCH_TABLE_ROW_STEP = 9.0f * WATCH_TABLE_TEXT_SIZE;

        // Default head HUD placement (used when no explicit watchAnchor is set). The anchor is a
        // world point in front of the HMD so the table gets natural stereo depth. FORWARD_DIST sets
        // that depth (~2 m at ~70 units/m); the offset fractions are of that forward distance, applied
        // along the HMD's local up/right so the table holds its screen spot as you look around. BOTTOM
        // drops it below the look axis (default, clear of your aim); TOP raises it (by more, to clear
        // the top edge); SIDE biases it left/right of centre.
        constexpr float HUD_FORWARD_DIST = 150.0f;
        constexpr float HUD_BOTTOM_FRACTION = 0.40f;
        constexpr float HUD_TOP_FRACTION = 0.10f;
        constexpr float HUD_SIDE_FRACTION = 0.40f;

        // Distance-scaled markers (point/sphere with distanceScaled=true): the passed size is what the
        // marker shows at REFERENCE distance; it scales linearly with distance to the viewer to hold a
        // constant apparent size. Floored so a marker right on top of you doesn't collapse to nothing.
        constexpr float MARKER_REFERENCE_DIST = 250.0f;
        constexpr float MARKER_MIN_DIST = 60.0f;

        // hotkey toggle state lives here (not in the header) to keep vrcf types out of DebugDraw.h
        std::string s_toggleBindingRaw;
        std::optional<vrcf::InputBinding> s_toggleBinding;

        /**
         * The codebase's local->world direction convention (rotation stored transposed — same as
         * MatrixUtils::localToWorldPoint, minus the translate/scale a direction doesn't need).
         */
        RE::NiPoint3 rotateLocalToWorld(const RE::NiMatrix3& rotate, const RE::NiPoint3& v)
        {
            return rotate.Transpose() * v;
        }

        /**
         * Two unit vectors perpendicular to dir (and to each other) for building circles/cones
         * around an arbitrary axis.
         */
        void perpendicularBasis(const RE::NiPoint3& dir, RE::NiPoint3& outU, RE::NiPoint3& outV)
        {
            RE::NiPoint3 reference(0, 0, 1);
            if (std::fabs(MatrixUtils::vec3Dot(dir, reference)) > 0.999f) {
                reference = RE::NiPoint3(1, 0, 0);
            }
            outU = MatrixUtils::vec3Norm(MatrixUtils::vec3Cross(dir, reference));
            outV = MatrixUtils::vec3Norm(MatrixUtils::vec3Cross(outU, dir));
        }

        /**
         * Strict ordering on colors so the publish sort groups equal-color lines into single draws.
         */
        bool colorLess(const Color& lhs, const Color& rhs)
        {
            return std::tie(lhs.r, lhs.g, lhs.b, lhs.a) < std::tie(rhs.r, rhs.g, rhs.b, rhs.a);
        }
    }

    DebugDraw& DebugDraw::get()
    {
        static DebugDraw instance;
        return instance;
    }

    /**
     * Runtime master switch layered over the INI value (bDebugDrawEnabled); wins until the next
     * setEnabled call. The in-headset hotkey routes through here too.
     */
    void DebugDraw::setEnabled(const bool enabled)
    {
        s_everUsed.store(true, std::memory_order_relaxed);
        _enabledOverride = enabled;
        refreshAppendActive();
    }

    bool DebugDraw::isEnabled() const
    {
        return effectiveEnabled();
    }

    /**
     * Tag subsequent draws with a channel name ("" = untagged, only the master switch applies).
     * Prefer channelScope() so the previous tag is restored automatically.
     */
    void DebugDraw::setChannel(const std::string_view channel)
    {
        _channel = channel;
        if (!channel.empty()) {
            _channelsSeen.emplace(channel); // remember it for the watch-table channel header
        }
        refreshAppendActive();
    }

    /**
     * Runtime per-channel override, layered over the INI-disabled set (sDebugDrawDisabledChannels).
     */
    void DebugDraw::setChannelEnabled(const std::string_view channel, const bool enabled)
    {
        _channelOverrides[std::string(channel)] = enabled;
        refreshAppendActive();
    }

    void DebugDraw::line(const RE::NiPoint3& start, const RE::NiPoint3& end, const Color& color, const float sec)
    {
        addLine(start, end, color, sec);
    }

    /**
     * Point marker: 3 axis-aligned crosses of the given half-size (ROCK DebugOverlayLineBatch.h:155).
     * With distanceScaled, the half-size is an apparent size held constant on screen at any range.
     */
    void DebugDraw::point(const RE::NiPoint3& pos, const float size, const bool distanceScaled, const Color& color, const float sec)
    {
        const float s = distanceScaled ? size * distanceScale(pos) : size;
        addLine(pos - RE::NiPoint3(s, 0, 0), pos + RE::NiPoint3(s, 0, 0), color, sec);
        addLine(pos - RE::NiPoint3(0, s, 0), pos + RE::NiPoint3(0, s, 0), color, sec);
        addLine(pos - RE::NiPoint3(0, 0, s), pos + RE::NiPoint3(0, 0, s), color, sec);
    }

    /**
     * Shaft segment plus a 4-line pyramid head at the tip. dir need not be normalized.
     */
    void DebugDraw::arrow(const RE::NiPoint3& origin, const RE::NiPoint3& dir, const float len, const Color& color, const float sec)
    {
        const RE::NiPoint3 n = MatrixUtils::vec3Norm(dir);
        const RE::NiPoint3 tip = origin + n * len;
        addLine(origin, tip, color, sec);

        RE::NiPoint3 u;
        RE::NiPoint3 v;
        perpendicularBasis(n, u, v);
        const float headLen = (std::min)(len * 0.25f, 8.0f);
        const RE::NiPoint3 headBase = tip - n * headLen;
        const float headRadius = headLen * 0.5f;
        addLine(tip, headBase + u * headRadius, color, sec);
        addLine(tip, headBase - u * headRadius, color, sec);
        addLine(tip, headBase + v * headRadius, color, sec);
        addLine(tip, headBase - v * headRadius, color, sec);
    }

    /**
     * Axis-aligned wire box: 12 edges from 8 corners. With distanceScaled the half-extents are an
     * apparent size held constant on screen (a marker), rather than a true world size.
     */
    void DebugDraw::boxAabb(const RE::NiPoint3& center, const RE::NiPoint3& halfExtents, const bool distanceScaled, const Color& color, const float sec)
    {
        RE::NiTransform transform;
        transform.MakeIdentity();
        transform.translate = center;
        boxObb(transform, halfExtents, distanceScaled, color, sec);
    }

    /**
     * Oriented wire box: local +-halfExtents corners carried to world through the transform (the
     * codebase's local->world convention, MatrixUtils::localToWorldPoint). With distanceScaled the
     * half-extents are scaled by distance to the box origin (transform.translate) to hold a constant
     * apparent size — the box stays put and keeps its orientation, only its size tracks distance.
     */
    void DebugDraw::boxObb(const RE::NiTransform& transform, const RE::NiPoint3& halfExtents, const bool distanceScaled, const Color& color, const float sec)
    {
        const RE::NiPoint3 he = distanceScaled ? halfExtents * distanceScale(transform.translate) : halfExtents;
        RE::NiPoint3 corners[8];
        for (int i = 0; i < 8; ++i) {
            const RE::NiPoint3 local((i & 1) ? he.x : -he.x, (i & 2) ? he.y : -he.y, (i & 4) ? he.z : -he.z);
            corners[i] = MatrixUtils::localToWorldPoint(transform, local);
        }
        // 12 edges: 4 along each local axis (partner = corner with that axis bit flipped, counted once)
        for (int i = 0; i < 8; ++i) {
            for (const int bit : { 1, 2, 4 }) {
                if ((i & bit) == 0) {
                    addLine(corners[i], corners[i | bit], color, sec);
                }
            }
        }
    }

    /**
     * Wire sphere as great circles: 3 orthogonal ones by default, plus extra Z-latitude rings when
     * rings > 3. With distanceScaled, the radius is an apparent size held constant on screen at any
     * range (a marker), rather than a true world radius.
     */
    void DebugDraw::sphere(const RE::NiPoint3& center, const float radius, const bool distanceScaled, const Color& color, const float sec, const int rings)
    {
        const float r = distanceScaled ? radius * distanceScale(center) : radius;
        const RE::NiPoint3 x(1, 0, 0);
        const RE::NiPoint3 y(0, 1, 0);
        const RE::NiPoint3 z(0, 0, 1);
        circle(center, x, y, r, color, sec);
        circle(center, x, z, r, color, sec);
        circle(center, y, z, r, color, sec);
        for (int i = 3; i < rings; ++i) {
            // extra latitude rings spread evenly above/below the equator
            const float t = static_cast<float>(i - 2) / static_cast<float>(rings - 2);
            const float zOffset = r * t;
            const float ringRadius = std::sqrt((std::max)(0.0f, r * r - zOffset * zOffset));
            circle(center + z * zOffset, x, y, ringRadius, color, sec);
            circle(center - z * zOffset, x, y, ringRadius, color, sec);
        }
    }

    /**
     * Wire capsule: end circles perpendicular to the axis, 4 side connectors, and 2 half-circle
     * caps per end (in both perpendicular planes).
     */
    void DebugDraw::capsule(const RE::NiPoint3& start, const RE::NiPoint3& end, const float radius, const Color& color, const float sec)
    {
        const RE::NiPoint3 axis = end - start;
        const float len = MatrixUtils::vec3Len(axis);
        if (len < 0.001f) {
            sphere(start, radius, false, color, sec);
            return;
        }
        const RE::NiPoint3 n = axis * (1.0f / len);
        RE::NiPoint3 u;
        RE::NiPoint3 v;
        perpendicularBasis(n, u, v);

        circle(start, u, v, radius, color, sec);
        circle(end, u, v, radius, color, sec);
        for (const auto& side : { u, v, u * -1.0f, v * -1.0f }) {
            addLine(start + side * radius, end + side * radius, color, sec);
        }

        // hemisphere caps: half circles in the (u,n) and (v,n) planes, bulging away from the body
        for (const auto& [center, capDir] : { std::pair{ start, n * -1.0f }, std::pair{ end, n } }) {
            for (const auto& planeU : { u, v }) {
                constexpr int CAP_SEGMENTS = 12;
                RE::NiPoint3 prev = center + planeU * radius;
                for (int i = 1; i <= CAP_SEGMENTS; ++i) {
                    const float angle = std::numbers::pi_v<float> * static_cast<float>(i) / CAP_SEGMENTS;
                    const RE::NiPoint3 next = center + planeU * (radius * std::cos(angle)) + capDir * (radius * std::sin(angle));
                    addLine(prev, next, color, sec);
                    prev = next;
                }
            }
        }
    }

    /**
     * Wire cone from apex along dir: base circle at len plus 8 fan lines. fullAngleDeg is the full
     * apex angle (a light's FOV maps 1:1), so base radius = len * tan(fullAngle / 2).
     */
    void DebugDraw::cone(const RE::NiPoint3& apex, const RE::NiPoint3& dir, const float len, const float fullAngleDeg, const Color& color, const float sec)
    {
        const RE::NiPoint3 n = MatrixUtils::vec3Norm(dir);
        RE::NiPoint3 u;
        RE::NiPoint3 v;
        perpendicularBasis(n, u, v);
        const RE::NiPoint3 baseCenter = apex + n * len;
        const float baseRadius = len * std::tan(MatrixUtils::degreesToRads(std::clamp(fullAngleDeg, 0.1f, 178.0f) * 0.5f));
        circle(baseCenter, u, v, baseRadius, color, sec);

        constexpr int FAN_LINES = 8;
        for (int i = 0; i < FAN_LINES; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / FAN_LINES;
            addLine(apex, baseCenter + u * (baseRadius * std::cos(angle)) + v * (baseRadius * std::sin(angle)), color, sec);
        }
    }

    /**
     * RGB orientation tripod: X red, Y green, Z blue, each len long in world units regardless of the
     * transform's scale — the fastest way to see where a node points.
     */
    void DebugDraw::axes(const RE::NiTransform& transform, const float len, const float sec)
    {
        const RE::NiPoint3& origin = transform.translate;
        addLine(origin, origin + MatrixUtils::vec3Norm(rotateLocalToWorld(transform.rotate, RE::NiPoint3(1, 0, 0))) * len, colors::Red, sec);
        addLine(origin, origin + MatrixUtils::vec3Norm(rotateLocalToWorld(transform.rotate, RE::NiPoint3(0, 1, 0))) * len, colors::Green, sec);
        addLine(origin, origin + MatrixUtils::vec3Norm(rotateLocalToWorld(transform.rotate, RE::NiPoint3(0, 0, 1))) * len, colors::Blue, sec);
    }

    /**
     * XY-plane grid centered on the point: parallel line sets extent out in both directions.
     */
    void DebugDraw::grid(const RE::NiPoint3& center, const float extent, const float step, const Color& color, const float sec)
    {
        if (step <= 0.01f || extent <= 0) {
            return;
        }
        for (float offset = 0; offset <= extent; offset += step) {
            for (const float side : { offset, -offset }) {
                addLine(center + RE::NiPoint3(side, -extent, 0), center + RE::NiPoint3(side, extent, 0), color, sec);
                addLine(center + RE::NiPoint3(-extent, side, 0), center + RE::NiPoint3(extent, side, 0), color, sec);
                if (fEqual(offset, 0)) {
                    break;
                }
            }
        }
    }

    void DebugDraw::polyline(const std::span<const RE::NiPoint3> points, const Color& color, const bool closed, const float sec)
    {
        for (std::size_t i = 1; i < points.size(); ++i) {
            addLine(points[i - 1], points[i], color, sec);
        }
        if (closed && points.size() > 2) {
            addLine(points.back(), points.front(), color, sec);
        }
    }

    /**
     * Arbitrary indexed triangle mesh drawn as its wire edges, shared edges deduplicated — the raw
     * escape hatch behind "render any shape".
     */
    void DebugDraw::mesh(const std::span<const RE::NiPoint3> vertices, const std::span<const std::uint16_t> indices, const Color& color, const float sec)
    {
        std::set<std::pair<std::uint16_t, std::uint16_t>> edges;
        const auto addEdge = [&](std::uint16_t a, std::uint16_t b) {
            if (a > b) {
                std::swap(a, b);
            }
            if (a != b && b < vertices.size() && edges.insert({ a, b }).second) {
                addLine(vertices[a], vertices[b], color, sec);
            }
        };
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            addEdge(indices[i], indices[i + 1]);
            addEdge(indices[i + 1], indices[i + 2]);
            addEdge(indices[i + 2], indices[i]);
        }
    }

    void DebugDraw::nodeAxes(const RE::NiAVObject* node, const float len)
    {
        if (node) {
            axes(node->world, len);
        }
    }

    /**
     * The node's world bounding sphere (center + radius as the engine culls it).
     */
    void DebugDraw::nodeBounds(const RE::NiAVObject* node, const Color& color, const float sec)
    {
        if (node) {
            sphere(node->worldBound.center, node->worldBound.fRadius, false, color, sec);
        }
    }

    /**
     * Screen-space HUD text at per-eye pixel coords (duplicated into both eye halves). Font covers
     * A-Z 0-9 - + = . , : / ( ) %; unknown characters render blank.
     */
    void DebugDraw::text(const std::string_view str, const float x, const float y, const Color& color, const float size)
    {
        s_everUsed.store(true, std::memory_order_relaxed);
        if (!isAppendActive() || str.empty()) {
            return;
        }
        _building.texts.push_back(internal::TextEntry{ .text = std::string(str), .x = x, .y = y, .size = size, .color = color });
    }

    /**
     * World label: a camera-facing billboard welded to worldPos (world-space geometry, same projection
     * as the shapes), so it stays stuck to the object and tilts with the world instead of the flat
     * screen-upright text that appears to rotate as you move your head.
     */
    void DebugDraw::label(const std::string_view str, const RE::NiPoint3& worldPos, const Color& color, const float size)
    {
        s_everUsed.store(true, std::memory_order_relaxed);
        if (!isAppendActive() || str.empty()) {
            return;
        }
        _building.texts.push_back(internal::TextEntry{ .text = std::string(str), .size = size, .color = color, .worldAnchor = worldPos, .worldAnchored = true, .billboard = true });
    }

    /**
     * Add/update one row of the HUD "name: value" table for this frame (laid out in call order at
     * the frame end). Immediate-mode like everything else: call it every frame the row should show.
     */
    void DebugDraw::watch(const std::string_view name, const std::string_view value)
    {
        s_everUsed.store(true, std::memory_order_relaxed);
        if (!isAppendActive()) {
            return;
        }
        const auto it = std::ranges::find(_watch, name, &std::pair<std::string, std::string>::first);
        if (it != _watch.end()) {
            it->second = value;
        } else {
            _watch.emplace_back(std::string(name), std::string(value));
        }
    }

    void DebugDraw::watchAnchor(const RE::NiPoint3& worldPos)
    {
        s_everUsed.store(true, std::memory_order_relaxed);
        _watchAnchor = worldPos;
    }

    void DebugDraw::watchAnchorNode(const RE::NiAVObject* node)
    {
        if (node) {
            watchAnchor(node->world.translate);
        }
    }

    RE::NiPoint3 DebugDraw::havokToGame(const RE::NiPoint3& point)
    {
        return point * HAVOK_TO_GAME_SCALE;
    }

    bool DebugDraw::effectiveEnabled() const
    {
        return _enabledOverride.value_or(_configEnabled);
    }

    /**
     * Recompute the per-append fast-path flag from the master switch and the current channel's
     * state (runtime override first, then the INI-disabled set) — so every draw call is a single
     * bool test instead of set lookups.
     */
    void DebugDraw::refreshAppendActive()
    {
        bool channelEnabled = true;
        if (!_channel.empty()) {
            if (const auto it = _channelOverrides.find(_channel); it != _channelOverrides.end()) {
                channelEnabled = it->second;
            } else {
                channelEnabled = !_configDisabledChannels.contains(_channel);
            }
        }
        _appendActive = effectiveEnabled() && channelEnabled;
    }

    /**
     * Multiplier turning an apparent size into a world size at p so a marker holds a constant on-screen
     * size: distance-to-camera / reference (floored near). 1.0 while the camera isn't known yet (first
     * frame / menus) so a marker just draws at its passed size instead of exploding.
     */
    float DebugDraw::distanceScale(const RE::NiPoint3& p) const
    {
        if (_cameraPos.x == 0.0f && _cameraPos.y == 0.0f && _cameraPos.z == 0.0f) {
            return 1.0f;
        }
        const float dist = MatrixUtils::vec3Len(p - _cameraPos);
        return (std::max)(dist, MARKER_MIN_DIST) / MARKER_REFERENCE_DIST;
    }

    /**
     * The single sink every primitive reduces to: append to this frame's list (budget-guarded), and
     * for sec > 0 also retain for re-emission until expiry.
     */
    void DebugDraw::addLine(const RE::NiPoint3& start, const RE::NiPoint3& end, const Color& color, const float sec)
    {
        s_everUsed.store(true, std::memory_order_relaxed);
        if (!isAppendActive()) {
            return;
        }
        if (_building.lines.size() * 2 + 2 > internal::MAX_LINE_VERTICES) {
            ++_rejectedLines;
            return;
        }
        _building.lines.push_back(internal::LineSegment{ .start = start, .end = end, .color = color });
        if (sec > 0) {
            _persist.push_back(TimedLine{ .segment = { .start = start, .end = end, .color = color }, .expiryMs = nowMillis() + static_cast<uint64_t>(sec * 1000.0f) });
        }
    }

    /**
     * Circle in the plane spanned by two unit axes.
     */
    void DebugDraw::circle(const RE::NiPoint3& center, const RE::NiPoint3& axisU, const RE::NiPoint3& axisV, const float radius, const Color& color, const float sec,
        const int segments)
    {
        RE::NiPoint3 prev = center + axisU * radius;
        for (int i = 1; i <= segments; ++i) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
            const RE::NiPoint3 next = center + axisU * (radius * std::cos(angle)) + axisV * (radius * std::sin(angle));
            addLine(prev, next, color, sec);
            prev = next;
        }
    }

    /**
     * Parse (on change) and poll the in-headset master toggle hotkey (sDebugDrawToggleBinding).
     */
    void DebugDraw::handleToggleHotkey(const std::string& configToggleBinding)
    {
        if (configToggleBinding != s_toggleBindingRaw) {
            s_toggleBindingRaw = configToggleBinding;
            s_toggleBinding = vrcf::parseInputBinding(configToggleBinding);
            if (!s_toggleBinding) {
                logger::warn("DebugDraw: invalid sDebugDrawToggleBinding '{}'", configToggleBinding);
            }
        }
        if (s_toggleBinding && s_toggleBinding->isEnabled() && vrcf::VRControllers.check(*s_toggleBinding)) {
            _enabledOverride = !effectiveEnabled();
            refreshAppendActive();
            logger::info("DebugDraw: hotkey toggled overlay {}", effectiveEnabled() ? "ON" : "OFF");
        }
    }

    /**
     * "channels: a b(off)" — the channels drawn to this frame, each flagged (off) when its own gate
     * (runtime override, else the INI-disabled set) is muting it. Empty when no channel was tagged.
     */
    std::string DebugDraw::channelStatusText() const
    {
        if (_channelsSeen.empty()) {
            return {};
        }
        std::string out = "channels:";
        for (const auto& name : _channelsSeen) {
            bool enabled;
            if (const auto it = _channelOverrides.find(name); it != _channelOverrides.end()) {
                enabled = it->second;
            } else {
                enabled = !_configDisabledChannels.contains(name);
            }
            out += " " + name + (enabled ? "" : "(off)");
        }
        return out;
    }

    /**
     * Lay the watch table out as stacked "name: value" rows, prefixed with a channel-status line
     * (bypasses the channel gate — each row was already gated when watch() stored it). Rows are
     * world-anchored labels stacking downward from the anchor's projected screen position, so they
     * read in VR (a corner HUD is cropped by the lens): the explicit watchAnchor when set, else the
     * default head HUD in front of the HMD. Falls back to nothing to draw if neither is available.
     */
    void DebugDraw::layoutWatchTable()
    {
        const auto anchor = _watchAnchor ? _watchAnchor : defaultHudAnchor();
        if (!anchor || (_watch.empty() && _channelsSeen.empty())) {
            return;
        }

        // Align the rows to the side the default HUD sits on (left rows flush-left, right flush-right,
        // otherwise centred). An explicit watchAnchor has no placement, so left-align it.
        internal::TextAlign align = internal::TextAlign::Left;
        if (!_watchAnchor) {
            if (_hudPlacement == HudPlacement::CenterLeft || _hudPlacement == HudPlacement::CenterTopLeft) {
                align = internal::TextAlign::Left;
            } else if (_hudPlacement == HudPlacement::CenterRight || _hudPlacement == HudPlacement::CenterTopRight) {
                align = internal::TextAlign::Right;
            } else {
                align = internal::TextAlign::Center;
            }
        }

        float y = 0.0f;
        const auto addRow = [&](std::string text) {
            _building.texts.push_back(internal::TextEntry{ .text = std::move(text),
                .y = y,
                .size = WATCH_TABLE_TEXT_SIZE,
                .color = colors::White,
                .worldAnchor = *anchor,
                .worldAnchored = true,
                .align = align });
            y += WATCH_TABLE_ROW_STEP;
        };

        if (auto channels = channelStatusText(); !channels.empty()) {
            addRow(std::move(channels));
        }
        for (const auto& [name, value] : _watch) {
            addRow(name + ": " + value);
        }
    }

    /**
     * Map a config string to a HUD placement (case/space/dash/underscore-insensitive), e.g.
     * "center", "center left", "center top right". Aliases: "top"/"top left"/"top right" (drop the
     * "center" prefix), "left"/"right". Unknown → Center.
     */
    DebugDraw::HudPlacement DebugDraw::parseHudPlacement(const std::string& text)
    {
        std::string key;
        for (const char ch : text) {
            if (std::isalpha(static_cast<unsigned char>(ch))) {
                key += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }
        if (key == "centertopleft" || key == "topleft") {
            return HudPlacement::CenterTopLeft;
        }
        if (key == "centertopright" || key == "topright") {
            return HudPlacement::CenterTopRight;
        }
        if (key == "centertop" || key == "top") {
            return HudPlacement::CenterTop;
        }
        if (key == "centerleft" || key == "left") {
            return HudPlacement::CenterLeft;
        }
        if (key == "centerright" || key == "right") {
            return HudPlacement::CenterRight;
        }
        return HudPlacement::Center;
    }

    /**
     * World point in front of the HMD for the default watch-table HUD, placed per _hudPlacement:
     * HUD_FORWARD_DIST along the look axis, then offset along the HMD's local up (down for the bottom
     * row, up by more for the top row) and right (for the left/right variants). Forward/up/right are
     * the HMD node's local +Y/+Z/+X carried to world (this codebase's transposed-rotation convention).
     * nullopt while the HMD node isn't loaded (menus / early load).
     */
    std::optional<RE::NiPoint3> DebugDraw::defaultHudAnchor() const
    {
        const auto* nodes = f4vr::getPlayerNodes();
        if (!nodes || !nodes->HmdNode) {
            return std::nullopt;
        }
        const RE::NiTransform& head = nodes->HmdNode->world;
        const RE::NiPoint3 forward = MatrixUtils::vec3Norm(head.rotate.Transpose() * RE::NiPoint3(0, 1, 0));
        const RE::NiPoint3 up = MatrixUtils::vec3Norm(head.rotate.Transpose() * RE::NiPoint3(0, 0, 1));
        const RE::NiPoint3 right = MatrixUtils::vec3Norm(head.rotate.Transpose() * RE::NiPoint3(1, 0, 0));

        const bool topRow = _hudPlacement == HudPlacement::CenterTop || _hudPlacement == HudPlacement::CenterTopLeft || _hudPlacement == HudPlacement::CenterTopRight;
        const float vert = topRow ? HUD_TOP_FRACTION : -HUD_BOTTOM_FRACTION;
        float side = 0.0f;
        if (_hudPlacement == HudPlacement::CenterLeft || _hudPlacement == HudPlacement::CenterTopLeft) {
            side = -HUD_SIDE_FRACTION;
        } else if (_hudPlacement == HudPlacement::CenterRight || _hudPlacement == HudPlacement::CenterTopRight) {
            side = HUD_SIDE_FRACTION;
        }

        return head.translate + forward * HUD_FORWARD_DIST + up * (HUD_FORWARD_DIST * vert) + right * (HUD_FORWARD_DIST * side);
    }

    /**
     * Frame-boundary producer step, driven by ModBase before the mod's onFrameUpdate: absorb config
     * changes, poll the hotkey, clear the last frame's command list, and re-emit unexpired timed
     * shapes. A no-op (single atomic read) until the first draw call ever.
     */
    void DebugDraw::onFrameStart(const bool configEnabled, const std::string& configDisabledChannels, const std::string& configToggleBinding, const std::string& configHudPlacement)
    {
        if (!s_everUsed.load(std::memory_order_relaxed)) {
            return;
        }
        auto& self = get();

        // head position for this frame — feeds distance-scaled markers (during onFrameUpdate) and the
        // billboard labels (copied into the published frame in onFrameEnd)
        if (const auto* nodes = f4vr::getPlayerNodes(); nodes && nodes->HmdNode) {
            self._cameraPos = nodes->HmdNode->world.translate;
        }

        self._configEnabled = configEnabled;
        if (configHudPlacement != self._hudPlacementRaw) {
            self._hudPlacementRaw = configHudPlacement;
            self._hudPlacement = parseHudPlacement(configHudPlacement);
        }
        if (configDisabledChannels != self._configDisabledChannelsRaw) {
            self._configDisabledChannelsRaw = configDisabledChannels;
            self._configDisabledChannels.clear();
            for (const auto& name : splitTrimmed(configDisabledChannels, ',')) {
                self._configDisabledChannels.insert(name);
            }
        }
        self.handleToggleHotkey(configToggleBinding);
        self.refreshAppendActive();

        self._building.clear();
        self._watch.clear();
        self._watchAnchor.reset();
        self._channelsSeen.clear();
        if (self._rejectedLines > 0) {
            logger::sample(5000, "DebugDraw: {} line(s) dropped over the {}-vertex budget", self._rejectedLines, internal::MAX_LINE_VERTICES);
            self._rejectedLines = 0;
        }

        // drop expired timed shapes, re-emit survivors into this frame (frame-rate independent)
        const uint64_t now = nowMillis();
        std::erase_if(self._persist, [now](const TimedLine& timed) {
            return now >= timed.expiryMs;
        });
        if (self.effectiveEnabled()) {
            for (const auto& timed : self._persist) {
                if (self._building.lines.size() * 2 + 2 > internal::MAX_LINE_VERTICES) {
                    break;
                }
                self._building.lines.push_back(timed.segment);
            }
        }
    }

    /**
     * Frame-boundary publish step, driven by ModBase after the mod's onFrameUpdate: lay out the
     * watch table, lazily install the renderer once there is something to draw, and swap the frame
     * to the render thread. A no-op until the first draw call ever.
     */
    void DebugDraw::onFrameEnd()
    {
        if (!s_everUsed.load(std::memory_order_relaxed)) {
            return;
        }
        auto& self = get();

        self.layoutWatchTable();

        if (!self._building.empty()) {
            renderer::ensureInstalled();
        }

        // hand this frame's head position to the render thread for orienting billboard labels
        self._building.cameraPos = self._cameraPos;

        // group same-color lines into contiguous runs so the renderer draws each run in one call
        std::ranges::stable_sort(self._building.lines, [](const internal::LineSegment& lhs, const internal::LineSegment& rhs) {
            return colorLess(lhs.color, rhs.color);
        });
        renderer::publish(std::move(self._building));
        self._building = {};
    }
}
