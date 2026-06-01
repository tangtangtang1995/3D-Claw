// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ransac_runner.h"

#include <atomic>
#include <cmath>
#include <chrono>
#include <thread>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Efficient_RANSAC.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Plane.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Sphere.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Cylinder.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Cone.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Torus.h>
#include <CGAL/Shape_detection/Efficient_RANSAC/Efficient_RANSAC_traits.h>
#include <CGAL/property_map.h>

using RANSAC_Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;

// ==========================================================================
// Impl
// ==========================================================================

struct RansacRunner::Impl {
    // Input (POD copies, converted to CGAL types inside run)
    std::vector<RANSAC_Point3d>  input_points;
    std::vector<RANSAC_Vector3d> input_normals;
    bool has_input = false;
    bool done = false;

    RANSAC_Config cfg{};
    bool live_preview = false;

    // Result
    int num_shapes = 0;
    int total_pts = 0;

    // Live preview
    std::deque<RANSAC_FrameEvent> live_queue;
    std::mutex live_mutex;

    // ShapeAccepted events stored unconditionally; get_shape_plane() reads
    // them post-run to assemble the final result table.
    std::vector<RANSAC_FrameEvent> events;

    // Cancel / Error / Pause
    std::atomic<bool> cancelled{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> step_flag{false};
    std::mutex error_mutex;
    std::string error_message;
    std::atomic<bool> has_error_flag{false};

    // Progress
    std::atomic<int> running_step{0};
    std::atomic<int> running_shapes{0};
    std::atomic<int> running_remaining{0};
};

namespace {

static constexpr std::size_t LIVE_QUEUE_MAX = 10000;

// ==========================================================================
// RecordingVisitor inherits from CGAL's default visitor.
// ==========================================================================

struct RecordingVisitor : CGAL::Shape_detection::Efficient_RANSAC_default_visitor
{
    RansacRunner::Impl* impl = nullptr;
    int sample_count = 0;
    int candidate_count = 0;
    int sample_pushed = 0;     // how many Sampling events actually pushed to live queue
    int candidate_pushed = 0;  // how many Candidate events actually pushed to live queue
    int last_candidate_push = -100;

    // Last sampling indices (always recorded, never throttled).
    // Used to populate Candidate events with the sampling points that
    // were used to fit the candidate.
    std::set<std::size_t> last_sample_indices;

    // Extract plane equation from a shape (RANSAC param gives Traits access).
    // Used by on_shape_accepted to record the FINAL (post-refinement) plane.
    // NOT used by on_candidate; there we use the 3 sample points directly.
    // (see compute_plane_from_samples) because the shape's plane at
    // candidate-time may drift from the sampling triangle.
    template <typename RANSAC, typename Shape>
    bool extract_plane(const RANSAC&, const Shape* shape, double eq[4]) {
        using Traits = typename RANSAC::Traits_type;
        using PlaneShape = CGAL::Shape_detection::Plane<Traits>;
        auto* plane = dynamic_cast<const PlaneShape*>(shape);
        if (!plane) return false;
        typename Traits::Plane_3 p3 = static_cast<typename Traits::Plane_3>(*plane);
        double len = sqrt(p3.a()*p3.a() + p3.b()*p3.b() + p3.c()*p3.c());
        if (len < 1e-30) return false;
        eq[0] = CGAL::to_double(p3.a());
        eq[1] = CGAL::to_double(p3.b());
        eq[2] = CGAL::to_double(p3.c());
        eq[3] = CGAL::to_double(p3.d());
        return true;
    }

    template <typename RANSAC>
    void on_init(const RANSAC&) {
        RANSAC_FrameEvent ev{};
        ev.type = RANSAC_FrameEvent::Init;
        ev.total_points = impl->total_pts;
        ev.remaining_points = impl->total_pts;
        push_live(ev);
    }

    template <typename RANSAC>
    void on_sampling(const RANSAC&, const std::set<std::size_t>& indices) {
        ++sample_count;
        // Always remember the last sample indices; needed to anchor
        // Candidate events at the actual sampling positions.
        last_sample_indices = indices;
        if (!impl->live_preview) return;
        const int iv = impl->cfg.sample_event_interval > 0
            ? impl->cfg.sample_event_interval : 50;
        // Throttling: skip when live_throttle is enabled AND not paused.
        // Pause or live_throttle=false: every sampling event is pushed.
        bool throttled = impl->cfg.live_throttle &&
                         !impl->paused.load(std::memory_order_relaxed);
        if (throttled && sample_count % iv != 0) return;

        // For Plane fitting, RANSAC samples exactly 3 points. Look them up in
        // our POD `input_points` (same indexing as the CGAL input range).
        if (indices.size() < 3 || impl->input_points.empty()) return;
        RANSAC_FrameEvent ev{};
        ev.type = RANSAC_FrameEvent::Sampling;
        ev.step = sample_count;
        fill_sample_pts(ev, indices);
        push_live(ev);
        ++sample_pushed;
    }

    // Compute plane from the 3 last-sampled points (cross product).
    // This plane EXACTLY passes through all 3 points by construction. We
    // don't trust the shape's internal plane equation at on_candidate time:
    // CGAL's pipeline (cost_function / connected_component / score on
    // global octree) may have modified it between create_shape and
    // on_candidate, causing the gold sampling triangle and yellow patch
    // to visibly drift.
    bool compute_plane_from_samples(double eq[4]) {
        if (last_sample_indices.size() < 3) return false;
        double P[3][3]; int k = 0;
        for (auto idx : last_sample_indices) {
            if (k >= 3) break;
            if (idx >= impl->input_points.size()) return false;
            P[k][0] = impl->input_points[idx].x;
            P[k][1] = impl->input_points[idx].y;
            P[k][2] = impl->input_points[idx].z;
            ++k;
        }
        if (k < 3) return false;
        double v1[3] = {P[1][0]-P[0][0], P[1][1]-P[0][1], P[1][2]-P[0][2]};
        double v2[3] = {P[2][0]-P[0][0], P[2][1]-P[0][1], P[2][2]-P[0][2]};
        double nx = v1[1]*v2[2] - v1[2]*v2[1];
        double ny = v1[2]*v2[0] - v1[0]*v2[2];
        double nz = v1[0]*v2[1] - v1[1]*v2[0];
        double nn = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (nn < 1e-30) return false;
        eq[0] = nx / nn; eq[1] = ny / nn; eq[2] = nz / nn;
        eq[3] = -(P[0][0]*eq[0] + P[0][1]*eq[1] + P[0][2]*eq[2]);
        return true;
    }

    // Fill sample_pts[3][3] from sampling indices.
    void fill_sample_pts(RANSAC_FrameEvent& ev, const std::set<std::size_t>& indices) {
        int slot = 0;
        for (auto idx : indices) {
            if (slot >= 3) break;
            if (idx >= impl->input_points.size()) continue;
            ev.sample_pts[slot][0] = impl->input_points[idx].x;
            ev.sample_pts[slot][1] = impl->input_points[idx].y;
            ev.sample_pts[slot][2] = impl->input_points[idx].z;
            ++slot;
        }
    }

    template <typename RANSAC, typename Shape>
    void on_candidate(const RANSAC& er, const Shape* shape, double score) {
        ++candidate_count;
        int interval = impl->cfg.candidate_event_interval;
        bool push_now = (interval <= 1) ||
                        (candidate_count - last_candidate_push) >= interval;

        // When paused, force every candidate to push an event. Without this,
        // most Step clicks land on a subsampled-out iteration (interval=10 in
        // live mode -> only 1/10 candidates ever pushed) and produce no
        // visible update. Step granularity has to == event granularity to
        // feel right.
        if (impl->live_preview && impl->paused.load(std::memory_order_relaxed))
            push_now = true;

        if (impl->live_preview && push_now) {
            last_candidate_push = candidate_count;
            RANSAC_FrameEvent ev{};
            ev.type = RANSAC_FrameEvent::Candidate;
            ev.step = candidate_count;
            ev.inlier_count = (int)score;
            // Use exact plane from 3 sample points instead of shape's
            // internal plane (which improve_bound has least-squares refitted).
            compute_plane_from_samples(ev.plane_eq);
            fill_sample_pts(ev, last_sample_indices);

            push_live(ev);
            ++candidate_pushed;
        }

        // Per-candidate pause/step gate. The previous design parked the
        // pause-check in the CGAL `callback`, but callback fires at MANY
        // sites within one logical iteration (sampling, per shape-factory,
        // scoring, ...). Most of those sites have no visible event, so
        // "Step" frequently advanced past a callback boundary without any
        // visual change. Moving the gate here means one Step = exactly one
        // candidate event pushed (above) -> exactly one sample-triangle +
        // candidate-plane overlay update. Predictable.
        if (impl->live_preview && impl->paused.load(std::memory_order_relaxed)) {
            while (impl->paused.load(std::memory_order_relaxed) &&
                   !impl->cancelled.load(std::memory_order_relaxed)) {
                if (impl->step_flag.exchange(false, std::memory_order_relaxed)) {
                    // Stepped: unblock so CGAL proceeds to the NEXT candidate;
                    // when on_candidate fires again it'll see paused=true and
                    // block, giving exactly one candidate per Step click.
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }

    template <typename RANSAC, typename Shape>
    void on_inliers(const RANSAC& er, const Shape* shape, int count) {
        if (!impl->live_preview) return;
        RANSAC_FrameEvent ev{};
        ev.type = RANSAC_FrameEvent::InlierVote;
        ev.step = candidate_count;
        ev.inlier_count = count;
        ev.remaining_points = impl->running_remaining.load();
        extract_plane(er, shape, ev.plane_eq);
        push_live(ev);
    }

    template <typename RANSAC, typename Shape>
    void on_shape_accepted(const RANSAC& er, const Shape* shape, int id) {
        int inliers = (int)shape->indices_of_assigned_points().size();
        RANSAC_FrameEvent ev{};
        ev.type = RANSAC_FrameEvent::ShapeAccepted;
        ev.step = candidate_count;
        ev.inlier_count = inliers;
        ev.shape_id = id;
        ev.shapes_found = id + 1;
        extract_plane(er, shape, ev.plane_eq);
        ev.remaining_points = impl->running_remaining.load();
        push_live(ev);
        impl->events.push_back(ev); // always store; needed for result extraction

        impl->running_shapes.store(id + 1);
    }

    template <typename RANSAC>
    void on_peel(const RANSAC&, int remaining) {
        impl->running_remaining.store(remaining);
        if (!impl->live_preview) return;
        RANSAC_FrameEvent ev{};
        ev.type = RANSAC_FrameEvent::Peel;
        ev.remaining_points = remaining;
        ev.total_points = impl->total_pts;
        ev.shapes_found = impl->running_shapes.load();
        push_live(ev);
    }

    template <typename RANSAC>
    void on_done(const RANSAC&) {
        RANSAC_FrameEvent ev{};
        ev.type = RANSAC_FrameEvent::Done;
        ev.shapes_found = impl->running_shapes.load();
        ev.remaining_points = impl->running_remaining.load();
        push_live(ev);

        impl->running_step.store(candidate_count);
    }

private:
    void push_live(RANSAC_FrameEvent& ev) {
        if (!impl->live_preview) return;
        std::lock_guard<std::mutex> lk(impl->live_mutex);
        if (impl->live_queue.size() >= LIVE_QUEUE_MAX)
            impl->live_queue.pop_front();
        impl->live_queue.push_back(ev);
    }
};

} // anonymous namespace

// ==========================================================================
// Constructor / Destructor
// ==========================================================================

RansacRunner::RansacRunner() : impl_(std::make_unique<Impl>()) {}
RansacRunner::~RansacRunner() = default;

// ==========================================================================
// Input
// ==========================================================================

void RansacRunner::set_input(
    const std::vector<RANSAC_Point3d>& points,
    const std::vector<RANSAC_Vector3d>& normals)
{
    clear_error();
    if (points.size() != normals.size()) {
        impl_->input_points.clear();
        impl_->input_normals.clear();
        impl_->has_input = false;
        set_error("RANSAC input point/normal count mismatch");
    } else {
        impl_->input_points = points;
        impl_->input_normals = normals;
        impl_->has_input = !points.empty();
    }
    impl_->done = false;
    impl_->cancelled.store(false);
    impl_->paused.store(false);
    impl_->step_flag.store(false);
    impl_->events.clear();
    { std::lock_guard<std::mutex> lk(impl_->live_mutex); impl_->live_queue.clear(); }
    impl_->running_step.store(0);
    impl_->running_shapes.store(0);
    impl_->running_remaining.store(0);
}

// ==========================================================================
// Run
// ==========================================================================

void RansacRunner::run(const RANSAC_Config& cfg)
{
    impl_->cfg = cfg;
    impl_->done = false;
    impl_->live_preview = cfg.live_preview;
    impl_->num_shapes = 0;
    impl_->total_pts = (int)impl_->input_points.size();
    impl_->cancelled.store(false);
    impl_->paused.store(false);
    impl_->step_flag.store(false);
    impl_->events.clear();
    { std::lock_guard<std::mutex> lk(impl_->live_mutex); impl_->live_queue.clear(); }
    impl_->running_step.store(0);
    impl_->running_shapes.store(0);
    impl_->running_remaining.store(impl_->total_pts);

    if (!impl_->has_input) {
        if (!impl_->has_error_flag.load())
            set_error("RANSAC input is empty");
        impl_->done = true;
        return;
    }

    // Build input as vector<pair<Point_3, Vector_3>> (avoids Point_set_3
    // which pulls in CGAL CORE/BigInt and GMP/MPFR).
    using Point_3 = RANSAC_Kernel::Point_3;
    using Vector_3 = RANSAC_Kernel::Vector_3;
    using InputItem = std::pair<Point_3, Vector_3>;
    using InputRange = std::vector<InputItem>;
    using PointMap = CGAL::First_of_pair_property_map<InputItem>;
    using NormalMap = CGAL::Second_of_pair_property_map<InputItem>;

    InputRange input;
    input.reserve(impl_->input_points.size());
    for (size_t i = 0; i < impl_->input_points.size(); ++i) {
        auto& p = impl_->input_points[i];
        auto& n = impl_->input_normals[i];
        input.emplace_back(Point_3(p.x, p.y, p.z), Vector_3(n.x, n.y, n.z));
    }

    using Traits = CGAL::Shape_detection::Efficient_RANSAC_traits<RANSAC_Kernel, InputRange, PointMap, NormalMap>;
    using Efficient_ransac = CGAL::Shape_detection::Efficient_RANSAC<Traits>;
    using Parameters = typename Efficient_ransac::Parameters;

    Efficient_ransac er;
    er.set_input(input, PointMap(), NormalMap());
    er.add_shape_factory<CGAL::Shape_detection::Plane<Traits>>();

    Parameters params;
    if (cfg.epsilon >= 0) params.epsilon = cfg.epsilon;
    if (cfg.normal_threshold >= 0) params.normal_threshold = cfg.normal_threshold;
    if (cfg.cluster_epsilon >= 0) params.cluster_epsilon = cfg.cluster_epsilon;
    if (cfg.min_points > 0) params.min_points = (std::size_t)cfg.min_points;

    // Always use visitor path; ShapeAccepted events are needed for
    // result extraction (get_shape_plane) even in fast mode.
    RecordingVisitor visitor;
    visitor.impl = impl_.get();

    // Auto-tune guardrails when live_preview is active
    if (cfg.live_preview && cfg.live_throttle) {
        auto tune = [](int n, int base, int hi, int huge) {
            if (n >= 1000000) return huge;
            if (n >=  200000) return hi;
            return base;
        };
        impl_->cfg.candidate_event_interval = tune(
            impl_->total_pts, cfg.candidate_event_interval,
            cfg.candidate_event_interval * 4, cfg.candidate_event_interval * 16);
    } else {
        // No throttle or not live: push every event
        impl_->cfg.candidate_event_interval = 1;
    }

    // Callback is now CANCEL-ONLY. CGAL calls it at many sites within one
    // logical round (per-sample, per shape-factory, etc.); most of those
    // sites don't correspond to any visible event. Pause/step lives in
    // visitor::on_candidate instead, where one tick == one candidate ==
    // one visible overlay update.
    auto cb = [this](double) -> bool {
        return !impl_->cancelled.load(std::memory_order_relaxed);
    };
    er.detect_with_visitor(params, visitor, cb);
    impl_->num_shapes = (int)er.shapes().size();

    impl_->paused.store(false, std::memory_order_relaxed);
    impl_->done = true;
}

// ==========================================================================
// Live preview
// ==========================================================================

bool RansacRunner::drain_live_events(std::vector<RANSAC_FrameEvent>& out_events)
{
    out_events.clear();
    std::lock_guard<std::mutex> lk(impl_->live_mutex);
    if (impl_->live_queue.empty()) return false;
    out_events.assign(impl_->live_queue.begin(), impl_->live_queue.end());
    impl_->live_queue.clear();
    return true;
}

void RansacRunner::cancel() { impl_->cancelled.store(true, std::memory_order_relaxed); }
bool RansacRunner::is_cancelled() const { return impl_->cancelled.load(std::memory_order_relaxed); }

void RansacRunner::pause()  { impl_->paused.store(true, std::memory_order_relaxed); }
void RansacRunner::resume() { impl_->paused.store(false, std::memory_order_relaxed); }
void RansacRunner::step()   { impl_->step_flag.store(true, std::memory_order_relaxed); }
bool RansacRunner::is_paused() const { return impl_->paused.load(std::memory_order_relaxed); }

// ==========================================================================
// Error reporting
// ==========================================================================

void RansacRunner::set_error(const std::string& msg) {
    { std::lock_guard<std::mutex> lk(impl_->error_mutex); impl_->error_message = msg; }
    impl_->has_error_flag.store(true, std::memory_order_release);
}
bool RansacRunner::has_error() const { return impl_->has_error_flag.load(std::memory_order_acquire); }
std::string RansacRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex); return impl_->error_message;
}
void RansacRunner::clear_error() {
    impl_->has_error_flag.store(false, std::memory_order_release);
}

// ==========================================================================
// Running stats
// ==========================================================================

int  RansacRunner::total_shapes() const { return impl_->running_shapes.load(); }
int  RansacRunner::total_points() const { return impl_->total_pts; }

// ==========================================================================
// Result
// ==========================================================================

bool RansacRunner::is_done() const { return impl_->done; }
int  RansacRunner::num_shapes() const { return impl_->num_shapes; }
float RansacRunner::progress() const { return impl_->done ? 1.0f : 0.0f; }

void RansacRunner::get_shape_plane(int idx, double plane_eq[4], int& inlier_count) const
{
    for (auto& ev : impl_->events) {
        if (ev.type == RANSAC_FrameEvent::ShapeAccepted && ev.shape_id == idx) {
            for (int i = 0; i < 4; ++i) plane_eq[i] = ev.plane_eq[i];
            inlier_count = ev.inlier_count;
            return;
        }
    }
}

// ==========================================================================
// Plane analysis: 2D convex hull + alpha shape (DLL-side, CGAL safe)
// ==========================================================================

#include <CGAL/convex_hull_2.h>
#include <CGAL/Alpha_shape_2.h>
#include <CGAL/Alpha_shape_vertex_base_2.h>
#include <CGAL/Alpha_shape_face_base_2.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <queue>
#include <map>

using AS_Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using AS_Point2 = AS_Kernel::Point_2;

void ransac_convex_hull_2d(const double* in_pts, int n,
    const double plane_eq[4],
    std::vector<RANSAC_Point3d>& out_ch)
{
    out_ch.clear();
    if (n < 3) return;

    double a = plane_eq[0], b = plane_eq[1], c = plane_eq[2];
    double nlen = std::sqrt(a*a + b*b + c*c);
    if (nlen < 1e-20) return;
    double nx = a/nlen, ny = b/nlen, nz = c/nlen;

    // Local orthonormal basis on plane
    double ux, uy, uz, vx, vy, vz;
    if (std::abs(nx) < 0.9) {
        ux=ny*0 - nz*0; uy=nz*1 - nx*0; uz=nx*0 - ny*1;
    } else {
        ux=ny*1 - nz*0; uy=nz*0 - nx*1; uz=nx*0 - ny*0;
    }
    double ulen = std::sqrt(ux*ux + uy*uy + uz*uz);
    ux/=ulen; uy/=ulen; uz/=ulen;
    vx = ny*uz - nz*uy; vy = nz*ux - nx*uz; vz = nx*uy - ny*ux;

    double ox = in_pts[0], oy = in_pts[1], oz = in_pts[2];

    std::vector<AS_Point2> pts2d; pts2d.reserve(n);
    for (int i = 0; i < n; ++i) {
        double dx = in_pts[i*3] - ox, dy = in_pts[i*3+1] - oy, dz = in_pts[i*3+2] - oz;
        double du = dx*ux + dy*uy + dz*uz;
        double dv = dx*vx + dy*vy + dz*vz;
        pts2d.emplace_back(du, dv);
    }

    std::vector<AS_Point2> ch;
    CGAL::convex_hull_2(pts2d.begin(), pts2d.end(), std::back_inserter(ch));

    for (auto& p2 : ch) {
        double x = ox + p2.x()*ux + p2.y()*vx;
        double y = oy + p2.x()*uy + p2.y()*vy;
        double z = oz + p2.x()*uz + p2.y()*vz;
        out_ch.push_back({x, y, z});
    }
}

using AS_Vb = CGAL::Alpha_shape_vertex_base_2<AS_Kernel>;
using AS_Fb = CGAL::Alpha_shape_face_base_2<AS_Kernel>;
using AS_Tds = CGAL::Triangulation_data_structure_2<AS_Vb, AS_Fb>;
using AS_Dt = CGAL::Delaunay_triangulation_2<AS_Kernel, AS_Tds>;
using CGAL_AS = CGAL::Alpha_shape_2<AS_Dt>;

// Face info tag for CDT: 0=unknown, 1=inside, 2=outside
struct ASFaceInfo { int tag = 0; };

using AS_Vb2 = CGAL::Triangulation_vertex_base_2<AS_Kernel>;
using AS_Cfb = CGAL::Constrained_triangulation_face_base_2<AS_Kernel>;
using AS_Fb2 = CGAL::Triangulation_face_base_with_info_2<ASFaceInfo, AS_Kernel, AS_Cfb>;
using AS_Tds2 = CGAL::Triangulation_data_structure_2<AS_Vb2, AS_Fb2>;
using CDT2 = CGAL::Constrained_Delaunay_triangulation_2<AS_Kernel, AS_Tds2>;

void ransac_alpha_shape_2d(const double* in_pts, int n,
    const double plane_eq[4], double alpha,
    std::vector<RANSAC_Point3d>& out_verts,
    std::vector<std::array<int,3>>& out_tris)
{
    out_verts.clear(); out_tris.clear();
    if (n < 3) return;

    double a = plane_eq[0], b = plane_eq[1], c = plane_eq[2];
    double nlen = std::sqrt(a*a + b*b + c*c);
    if (nlen < 1e-20) return;
    double nx = a/nlen, ny = b/nlen, nz = c/nlen;

    double ux, uy, uz, vx, vy, vz;
    if (std::abs(nx) < 0.9) {
        ux=ny*0 - nz*0; uy=nz*1 - nx*0; uz=nx*0 - ny*1;
    } else {
        ux=ny*1 - nz*0; uy=nz*0 - nx*1; uz=nx*0 - ny*0;
    }
    double ulen = std::sqrt(ux*ux + uy*uy + uz*uz);
    ux/=ulen; uy/=ulen; uz/=ulen;
    vx = ny*uz - nz*uy; vy = nz*ux - nx*uz; vz = nx*uy - ny*ux;

    double ox = in_pts[0], oy = in_pts[1], oz = in_pts[2];

    std::vector<AS_Point2> pts2d; pts2d.reserve(n);
    for (int i = 0; i < n; ++i) {
        double dx = in_pts[i*3] - ox, dy = in_pts[i*3+1] - oy, dz = in_pts[i*3+2] - oz;
        double du = dx*ux + dy*uy + dz*uz;
        double dv = dx*vx + dy*vy + dz*vz;
        pts2d.emplace_back(du, dv);
    }

    // Use REGULARIZED mode: drop singular / regular 1-d simplices, keep
    // only the 2-manifold parts. Without this the alpha complex contains
    // dangling edges and isolated faces that the dual CDT-flood approach
    // (previous implementation) classified inconsistently; visible as
    // nested colored "tree rings" inside the mesh on the screenshot.
    CGAL_AS as(pts2d.begin(), pts2d.end(), CGAL_AS::FT(alpha),
               CGAL_AS::REGULARIZED);

    // Walk alpha shape faces directly. classify() returns one of
    // EXTERIOR / SINGULAR / REGULAR / INTERIOR per face; we only want
    // INTERIOR (filled). No more CDT round-trip, no more flood fill from
    // bbox corners, no more "hole flood doesn't reach inside" bug.
    std::map<typename AS_Dt::Vertex_handle, int> vh_to_idx;
    auto idx_of = [&](typename AS_Dt::Vertex_handle vh) -> int {
        auto it = vh_to_idx.find(vh);
        if (it != vh_to_idx.end()) return it->second;
        int idx = (int)out_verts.size();
        vh_to_idx[vh] = idx;
        auto p = vh->point();
        double x = ox + CGAL::to_double(p.x())*ux + CGAL::to_double(p.y())*vx;
        double y = oy + CGAL::to_double(p.x())*uy + CGAL::to_double(p.y())*vy;
        double z = oz + CGAL::to_double(p.x())*uz + CGAL::to_double(p.y())*vz;
        out_verts.push_back({x, y, z});
        return idx;
    };

    for (auto fit = as.finite_faces_begin(); fit != as.finite_faces_end(); ++fit) {
        if (as.classify(fit) != CGAL_AS::INTERIOR) continue;
        int i0 = idx_of(fit->vertex(0));
        int i1 = idx_of(fit->vertex(1));
        int i2 = idx_of(fit->vertex(2));
        out_tris.push_back({i0, i1, i2});
    }
}
