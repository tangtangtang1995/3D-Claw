// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "alpha_wrap_runner.h"

#include <atomic>
#include <cmath>
#include <exception>
#include <unordered_map>
#include <utility>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/alpha_wrap_3.h>
#include <CGAL/Alpha_wrap_3/internal/Alpha_wrap_3.h>
#include <CGAL/Alpha_wrap_3/internal/Triangle_soup_oracle.h>
#include <CGAL/Alpha_wrap_3/internal/Point_set_oracle.h>

using AW3_Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using AW3_Point  = AW3_Kernel::Point_3;
using AW3_Mesh   = CGAL::Surface_mesh<AW3_Point>;

// ==========================================================================
// Impl (must be before LivePreviewVisitor; visitor accesses impl members)
// ==========================================================================

struct AlphaWrapRunner::Impl {
    using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Point_3 = Kernel::Point_3;
    using Mesh   = CGAL::Surface_mesh<Point_3>;

    std::vector<Point_3> points;
    std::vector<std::array<int, 3>> faces;
    Mesh result;
    bool has_input = false;
    bool done = false;

    AW3_Config cfg{};
    bool live_preview = false;

    // Live preview
    std::deque<AW3_FrameEvent> live_queue;
    std::mutex live_mutex;
    std::vector<AW3_Point3d> live_surface_verts;
    std::vector<AW3_Triangle> live_surface_faces;
    std::mutex live_surface_mutex;
    bool live_surface_dirty = false;
    std::atomic<bool> cancelled{false};

    // Error state
    mutable std::mutex error_mutex;
    std::string error_message;
    std::atomic<bool> has_error_flag{false};

    // Progress
    std::atomic<int> running_step{0};
    std::atomic<int> running_steiner{0};
    std::atomic<int> running_carved{0};
};

namespace {

// Defaults; overridden per-run via AW3_Config::gate_event_interval and
// progress_event_interval (auto-tuned by the worker based on input size).
static constexpr int DEFAULT_PROGRESS_INTERVAL = 50;
static constexpr int DEFAULT_GATE_INTERVAL = 25;
static constexpr std::size_t LIVE_QUEUE_MAX = 20000;

template <typename CGALMesh>
void cgal_mesh_to_pod(const CGALMesh& cgal_mesh,
    std::vector<AW3_Point3d>& out_verts,
    std::vector<AW3_Triangle>& out_faces);

// ==========================================================================
// Live-preview visitor
// ==========================================================================

struct LivePreviewVisitor : CGAL::Alpha_wraps_3::internal::Wrapping_default_visitor
{
    AlphaWrapRunner::Impl* impl;
    int step_counter = 0;
    int steiner_counter = 0;

    template <typename AlphaWrapper>
    void on_alpha_wrapping_begin(const AlphaWrapper&) {
        AW3_FrameEvent ev{};
        ev.type = AW3_FrameEvent::Init;
        ev.step = 0;
        push_event(ev);
    }

    template <typename AlphaWrapper>
    void on_flood_fill_begin(const AlphaWrapper&) {}

    template <typename Wrapper>
    bool go_further(const Wrapper&) {
        return !impl->cancelled.load(std::memory_order_relaxed);
    }

    template <typename AlphaWrapper, typename Gate>
    void before_facet_treatment(const AlphaWrapper& w, const Gate& gate) {
        ++step_counter;

        const int gate_iv = impl->cfg.gate_event_interval > 0
            ? impl->cfg.gate_event_interval : DEFAULT_GATE_INTERVAL;
        const int prog_iv = impl->cfg.progress_event_interval > 0
            ? impl->cfg.progress_event_interval : DEFAULT_PROGRESS_INTERVAL;

        if (impl->live_preview && step_counter % gate_iv == 0) {
            AW3_FrameEvent ev{};
            ev.type = AW3_FrameEvent::Gate;
            ev.step = step_counter;
            ev.num_steiner_total = steiner_counter;
            ev.num_carved_total = step_counter - steiner_counter;
            ev.gate_queue_size = static_cast<int>(w.queue().size());

            using Tr = typename AlphaWrapper::Triangulation;
            const auto& tr = w.triangulation();
            const auto& f = gate.facet();
            const auto ch = f.first;
            const int s = f.second;
            const auto& p0 = tr.point(ch, Tr::vertex_triple_index(s, 0));
            const auto& p1 = tr.point(ch, Tr::vertex_triple_index(s, 1));
            const auto& p2 = tr.point(ch, Tr::vertex_triple_index(s, 2));

            auto fill = [](double dst[3], const auto& p) {
                dst[0] = CGAL::to_double(p.x());
                dst[1] = CGAL::to_double(p.y());
                dst[2] = CGAL::to_double(p.z());
            };
            fill(ev.gate_p0, p0);
            fill(ev.gate_p1, p1);
            fill(ev.gate_p2, p2);
            ev.point[0] = (ev.gate_p0[0] + ev.gate_p1[0] + ev.gate_p2[0]) / 3.0;
            ev.point[1] = (ev.gate_p0[1] + ev.gate_p1[1] + ev.gate_p2[1]) / 3.0;
            ev.point[2] = (ev.gate_p0[2] + ev.gate_p1[2] + ev.gate_p2[2]) / 3.0;
#ifdef CGAL_AW3_USE_SORTED_PRIORITY_QUEUE
            ev.gate_radius = std::sqrt(CGAL::to_double(gate.priority()));
#endif
            push_live_event(ev);
        }

        // Progress event: keep the UI alive even when no Steiner point is added.
        if (step_counter % prog_iv == 0) {
            int carved = step_counter - steiner_counter;
            AW3_FrameEvent ev{};
            ev.type = AW3_FrameEvent::Progress;
            ev.step = step_counter;
            ev.num_steiner_total = steiner_counter;
            ev.num_carved_total = carved;
            ev.gate_queue_size = static_cast<int>(w.queue().size());
            push_event(ev);
            impl->running_step.store(step_counter);
            impl->running_steiner.store(steiner_counter);
            impl->running_carved.store(carved);
        }

        // Gate events deferred because the template visitor can't easily extract facet
        // vertices across all CGAL triangulation types. Steiner point
        // accumulation + progress stats provide sufficient live feedback.
        (void)gate;
    }

    template <typename Wrapper, typename Point>
    void before_Steiner_point_insertion(const Wrapper&, const Point&) {}

    template <typename Wrapper, typename VertexHandle>
    void after_Steiner_point_insertion(const Wrapper& w, VertexHandle v) {
        ++steiner_counter;
        auto p = v->point();

        int carved = step_counter - steiner_counter;
        AW3_FrameEvent ev{};
        ev.type = AW3_FrameEvent::SteinerR1; // R1/R2 not yet implemented
        ev.step = step_counter;
        ev.point[0] = CGAL::to_double(p.x());
        ev.point[1] = CGAL::to_double(p.y());
        ev.point[2] = CGAL::to_double(p.z());
        ev.num_steiner_total = steiner_counter;
        ev.num_carved_total = carved;
        ev.gate_queue_size = static_cast<int>(w.queue().size());

        push_event(ev);

        if (impl->live_preview &&
            impl->cfg.live_surface_interval > 0 &&
            steiner_counter % impl->cfg.live_surface_interval == 0) {
            capture_surface_snapshot(w, ev);
        }

        impl->running_step.store(step_counter);
        impl->running_steiner.store(steiner_counter);
        impl->running_carved.store(carved);
    }

    template <typename AlphaWrapper>
    void on_flood_fill_end(const AlphaWrapper&) {
        int carved = step_counter - steiner_counter;
        AW3_FrameEvent ev{};
        ev.type = AW3_FrameEvent::Done;
        ev.step = step_counter;
        ev.num_steiner_total = steiner_counter;
        ev.num_carved_total = carved;
        push_event(ev);

        impl->running_step.store(step_counter);
        impl->running_steiner.store(steiner_counter);
        impl->running_carved.store(carved);
    }

    template <typename AlphaWrapper>
    void on_alpha_wrapping_end(const AlphaWrapper&) {}

private:
    template <typename Wrapper>
    void capture_surface_snapshot(const Wrapper& w, const AW3_FrameEvent& base_ev) {
        try {
            AW3_Mesh cgal_mesh;
            w.extract_surface(cgal_mesh, get(CGAL::vertex_point, cgal_mesh), true);

            std::vector<AW3_Point3d> verts;
            std::vector<AW3_Triangle> faces;
            cgal_mesh_to_pod(cgal_mesh, verts, faces);

            AW3_FrameEvent ev = base_ev;
            ev.type = AW3_FrameEvent::SurfaceSnapshot;
            ev.surface_vertices = static_cast<int>(verts.size());
            ev.surface_faces = static_cast<int>(faces.size());

            {
                std::lock_guard<std::mutex> lk(impl->live_surface_mutex);
                impl->live_surface_verts = std::move(verts);
                impl->live_surface_faces = std::move(faces);
                impl->live_surface_dirty = true;
            }
            push_live_event(ev);
        } catch (...) {
        }
    }

    void push_event(AW3_FrameEvent& ev) {
        // live_preview: push to thread-safe live queue, with Steiner
        // subsampling for large inputs so the queue does not immediately
        // overflow.
        if (impl->live_preview) {
            if (ev.type == AW3_FrameEvent::SteinerR1 ||
                ev.type == AW3_FrameEvent::SteinerR2) {
                const int sub = impl->cfg.live_steiner_subsample;
                if (sub > 1 && (steiner_counter % sub) != 0)
                    return;
            }
            push_live(ev);
        }
    }

    void push_live_event(AW3_FrameEvent& ev) {
        if (impl->live_preview)
            push_live(ev);
    }

    void push_live(AW3_FrameEvent& ev) {
        std::lock_guard<std::mutex> lk(impl->live_mutex);
        if (impl->live_queue.size() >= LIVE_QUEUE_MAX)
            impl->live_queue.pop_front();
        impl->live_queue.push_back(ev);
    }
};

// ==========================================================================
// Final live-surface snapshot
// ==========================================================================

void publish_final_live_surface_snapshot(AlphaWrapRunner::Impl* impl) {
    if (!impl->live_preview ||
        impl->cfg.live_surface_interval <= 0 ||
        impl->cancelled.load(std::memory_order_relaxed) ||
        impl->result.num_vertices() == 0 ||
        impl->result.num_faces() == 0) {
        return;
    }

    std::vector<AW3_Point3d> verts;
    std::vector<AW3_Triangle> faces;
    auto& mesh = impl->result;
    verts.reserve(mesh.num_vertices());
    faces.reserve(mesh.num_faces());

    std::vector<int> vmap(mesh.num_vertices(), -1);
    int vi = 0;
    for (auto v : mesh.vertices()) {
        auto& p = mesh.point(v);
        verts.push_back({CGAL::to_double(p.x()),
                         CGAL::to_double(p.y()),
                         CGAL::to_double(p.z())});
        vmap[v.idx()] = vi++;
    }
    for (auto f : mesh.faces()) {
        int fi[3], i = 0;
        for (auto v : CGAL::vertices_around_face(mesh.halfedge(f), mesh)) {
            fi[i++] = vmap[v.idx()];
            if (i >= 3) break;
        }
        if (i == 3)
            faces.push_back({fi[0], fi[1], fi[2]});
    }

    AW3_FrameEvent ev{};
    ev.type = AW3_FrameEvent::SurfaceSnapshot;
    ev.step = impl->running_step.load(std::memory_order_relaxed);
    ev.num_steiner_total = impl->running_steiner.load(std::memory_order_relaxed);
    ev.num_carved_total = impl->running_carved.load(std::memory_order_relaxed);
    ev.surface_vertices = static_cast<int>(verts.size());
    ev.surface_faces = static_cast<int>(faces.size());

    {
        std::lock_guard<std::mutex> lk(impl->live_surface_mutex);
        impl->live_surface_verts = std::move(verts);
        impl->live_surface_faces = std::move(faces);
        impl->live_surface_dirty = true;
    }
    {
        std::lock_guard<std::mutex> lk(impl->live_mutex);
        if (impl->live_queue.size() >= LIVE_QUEUE_MAX)
            impl->live_queue.pop_front();
        impl->live_queue.push_back(ev);
    }
}

} // anonymous namespace

// ==========================================================================
// Constructor / Destructor
// ==========================================================================

AlphaWrapRunner::AlphaWrapRunner() : impl_(std::make_unique<Impl>()) {}
AlphaWrapRunner::~AlphaWrapRunner() = default;

// ==========================================================================
// Input
// ==========================================================================

void AlphaWrapRunner::set_input_mesh(
    const std::vector<AW3_Point3d>& vertices,
    const std::vector<AW3_Triangle>& faces)
{
    impl_->points.clear(); impl_->faces.clear();
    {
        std::lock_guard<std::mutex> lk(impl_->live_mutex);
        impl_->live_queue.clear();
    }
    {
        std::lock_guard<std::mutex> lk(impl_->live_surface_mutex);
        impl_->live_surface_verts.clear();
        impl_->live_surface_faces.clear();
        impl_->live_surface_dirty = false;
    }
    impl_->has_input = false; impl_->done = false;
    impl_->cancelled.store(false);
    impl_->running_step.store(0);
    impl_->running_steiner.store(0);
    impl_->running_carved.store(0);
    impl_->points.reserve(vertices.size());
    for (auto& v : vertices) impl_->points.emplace_back(v.x, v.y, v.z);
    impl_->faces.reserve(faces.size());
    for (auto& f : faces) impl_->faces.push_back({f.v0, f.v1, f.v2});
    impl_->has_input = true;
}

void AlphaWrapRunner::set_input_cloud(const std::vector<AW3_Point3d>& points)
{
    impl_->points.clear(); impl_->faces.clear();
    {
        std::lock_guard<std::mutex> lk(impl_->live_mutex);
        impl_->live_queue.clear();
    }
    {
        std::lock_guard<std::mutex> lk(impl_->live_surface_mutex);
        impl_->live_surface_verts.clear();
        impl_->live_surface_faces.clear();
        impl_->live_surface_dirty = false;
    }
    impl_->has_input = false; impl_->done = false;
    impl_->cancelled.store(false);
    impl_->running_step.store(0);
    impl_->running_steiner.store(0);
    impl_->running_carved.store(0);
    impl_->points.reserve(points.size());
    for (auto& p : points) impl_->points.emplace_back(p.x, p.y, p.z);
    impl_->has_input = true;
}

// ==========================================================================
// Run
// ==========================================================================

static void run_fast(AlphaWrapRunner::Impl* impl, const AW3_Config& cfg)
{
    if (!impl->faces.empty())
        CGAL::alpha_wrap_3(impl->points, impl->faces, cfg.alpha, cfg.offset, impl->result);
    else
        CGAL::alpha_wrap_3(impl->points, cfg.alpha, cfg.offset, impl->result);
}

static void run_with_live_preview(AlphaWrapRunner::Impl* impl, const AW3_Config& cfg)
{
    using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Geom_traits = CGAL::Alpha_wraps_3::internal::Alpha_wrap_AABB_geom_traits<Kernel>;
    Geom_traits gt;

    if (!impl->faces.empty()) {
        using Oracle = CGAL::Alpha_wraps_3::internal::Triangle_soup_oracle<Kernel>;
        using AW3 = CGAL::Alpha_wraps_3::internal::Alpha_wrapper_3<Oracle>;
        Oracle oracle(cfg.alpha, gt);
        oracle.add_triangle_soup(impl->points, impl->faces,
            CGAL::parameters::default_values());
        LivePreviewVisitor visitor;
        visitor.impl = impl;
        AW3 wrapper(oracle);
        AW3_Mesh output;
        wrapper(cfg.alpha, cfg.offset, output,
            CGAL::parameters::visitor(visitor),
            CGAL::parameters::default_values());
        impl->result = std::move(output);
    } else {
        using Oracle = CGAL::Alpha_wraps_3::internal::Point_set_oracle<Kernel>;
        using AW3 = CGAL::Alpha_wraps_3::internal::Alpha_wrapper_3<Oracle>;
        Oracle oracle(gt);
        oracle.add_point_set(impl->points, CGAL::parameters::default_values());
        LivePreviewVisitor visitor;
        visitor.impl = impl;
        AW3 wrapper(oracle);
        AW3_Mesh output;
        wrapper(cfg.alpha, cfg.offset, output,
            CGAL::parameters::visitor(visitor),
            CGAL::parameters::default_values());
        impl->result = std::move(output);
    }
}

void AlphaWrapRunner::run(const AW3_Config& cfg)
{
    impl_->cfg = cfg;
    impl_->done = false;
    {
        std::lock_guard<std::mutex> lk(impl_->live_mutex);
        impl_->live_queue.clear();
    }
    {
        std::lock_guard<std::mutex> lk(impl_->live_surface_mutex);
        impl_->live_surface_verts.clear();
        impl_->live_surface_faces.clear();
        impl_->live_surface_dirty = false;
    }
    impl_->cancelled.store(false);
    {
        std::lock_guard<std::mutex> lk(impl_->error_mutex);
        impl_->error_message.clear();
    }
    impl_->has_error_flag.store(false, std::memory_order_release);
    impl_->running_step.store(0);
    impl_->running_steiner.store(0);
    impl_->running_carved.store(0);
    impl_->live_preview = cfg.live_preview;

    if (!impl_->has_input) return;

    if (cfg.live_preview)
        run_with_live_preview(impl_.get(), cfg);
    else
        run_fast(impl_.get(), cfg);

    publish_final_live_surface_snapshot(impl_.get());
    impl_->done = true;
}

// ==========================================================================
// Live preview
// ==========================================================================

bool AlphaWrapRunner::drain_live_events(std::vector<AW3_FrameEvent>& out_events)
{
    out_events.clear();
    std::lock_guard<std::mutex> lk(impl_->live_mutex);
    if (impl_->live_queue.empty()) return false;
    out_events.assign(impl_->live_queue.begin(), impl_->live_queue.end());
    impl_->live_queue.clear();
    return true;
}

bool AlphaWrapRunner::drain_live_surface_snapshot(
    std::vector<AW3_Point3d>& verts,
    std::vector<AW3_Triangle>& faces)
{
    verts.clear();
    faces.clear();
    std::lock_guard<std::mutex> lk(impl_->live_surface_mutex);
    if (!impl_->live_surface_dirty)
        return false;
    verts = std::move(impl_->live_surface_verts);
    faces = std::move(impl_->live_surface_faces);
    impl_->live_surface_dirty = false;
    return true;
}

void AlphaWrapRunner::cancel() { impl_->cancelled.store(true, std::memory_order_relaxed); }
bool AlphaWrapRunner::is_cancelled() const { return impl_->cancelled.load(std::memory_order_relaxed); }

// --- Error reporting ---
void AlphaWrapRunner::set_error(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lk(impl_->error_mutex);
        impl_->error_message = msg;
    }
    impl_->has_error_flag.store(true, std::memory_order_release);
    // Push an Error event so any live consumer sees it in order.
    if (impl_->live_preview) {
        AW3_FrameEvent ev{};
        ev.type = AW3_FrameEvent::Error;
        ev.step = impl_->running_step.load(std::memory_order_relaxed);
        ev.num_steiner_total = impl_->running_steiner.load(std::memory_order_relaxed);
        ev.num_carved_total = impl_->running_carved.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(impl_->live_mutex);
        if (impl_->live_queue.size() >= LIVE_QUEUE_MAX)
            impl_->live_queue.pop_front();
        impl_->live_queue.push_back(ev);
    }
}

bool AlphaWrapRunner::has_error() const {
    return impl_->has_error_flag.load(std::memory_order_acquire);
}

std::string AlphaWrapRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

void AlphaWrapRunner::clear_error() {
    {
        std::lock_guard<std::mutex> lk(impl_->error_mutex);
        impl_->error_message.clear();
    }
    impl_->has_error_flag.store(false, std::memory_order_release);
}

// ==========================================================================
// Result access
// ==========================================================================

void AlphaWrapRunner::get_current_surface(
    std::vector<AW3_Point3d>& verts, std::vector<AW3_Triangle>& faces) const
{ get_result(verts, faces); }

void AlphaWrapRunner::get_result(
    std::vector<AW3_Point3d>& verts, std::vector<AW3_Triangle>& faces) const
{
    verts.clear(); faces.clear();
    if (!impl_->done) return;
    auto& mesh = impl_->result;
    verts.reserve(mesh.num_vertices());
    faces.reserve(mesh.num_faces());
    std::vector<int> vmap(mesh.num_vertices(), -1);
    int vi = 0;
    for (auto v : mesh.vertices()) {
        auto& p = mesh.point(v);
        verts.push_back({(double)p.x(), (double)p.y(), (double)p.z()});
        vmap[v.idx()] = vi++;
    }
    for (auto f : mesh.faces()) {
        int fi[3], i = 0;
        for (auto v : CGAL::vertices_around_face(mesh.halfedge(f), mesh)) {
            fi[i++] = vmap[v.idx()];
            if (i >= 3) break;
        }
        if (i == 3) faces.push_back({fi[0], fi[1], fi[2]});
    }
}

// ==========================================================================
// State
// ==========================================================================

float AlphaWrapRunner::progress() const { return impl_->done ? 1.0f : 0.0f; }
bool  AlphaWrapRunner::is_done() const { return impl_->done; }
int   AlphaWrapRunner::current_vertex_count() const { return impl_->done ? (int)impl_->result.num_vertices() : 0; }
int   AlphaWrapRunner::current_face_count() const { return impl_->done ? (int)impl_->result.num_faces() : 0; }

// ==========================================================================
// POD conversion helpers
// ==========================================================================

namespace {

template <typename CGALMesh>
void cgal_mesh_to_pod(const CGALMesh& cgal_mesh,
    std::vector<AW3_Point3d>& out_verts,
    std::vector<AW3_Triangle>& out_faces)
{
    out_verts.clear(); out_faces.clear();
    out_verts.reserve(cgal_mesh.num_vertices());
    out_faces.reserve(cgal_mesh.num_faces());
    std::unordered_map<typename decltype(cgal_mesh.vertices().begin())::value_type,
                       int> vmap;
    int vi = 0;
    for (auto v : cgal_mesh.vertices()) {
        auto& pt = cgal_mesh.point(v);
        out_verts.push_back({CGAL::to_double(pt.x()),
                             CGAL::to_double(pt.y()),
                             CGAL::to_double(pt.z())});
        vmap[v] = vi++;
    }
    for (auto f : cgal_mesh.faces()) {
        int fi[3], i = 0;
        for (auto v : CGAL::vertices_around_face(cgal_mesh.halfedge(f), cgal_mesh)) {
            fi[i++] = vmap[v];
            if (i >= 3) break;
        }
        if (i == 3) out_faces.push_back({fi[0], fi[1], fi[2]});
    }
}
} // anonymous namespace
