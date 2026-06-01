// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "region_growing_runner.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <thread>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Shape_detection/Region_growing/Region_growing.h>
#include <CGAL/Shape_detection/Region_growing/Point_set/K_neighbor_query.h>
#include <CGAL/Shape_detection/Region_growing/Point_set/Least_squares_plane_fit_region.h>
#include <CGAL/Shape_detection/Region_growing/Point_set/Least_squares_plane_fit_sorting.h>

// ==========================================================================
// Custom property map: index to vector element.
// ==========================================================================

template <typename T>
struct VectorPropertyMap {
    using value_type = T;
    using reference = const T&;
    using key_type = std::size_t;
    using category = boost::readable_property_map_tag;

    const std::vector<T>* data = nullptr;
    VectorPropertyMap() = default;
    explicit VectorPropertyMap(const std::vector<T>& v) : data(&v) {}
};

namespace boost {
template <typename T>
struct property_traits<VectorPropertyMap<T>> {
    using key_type = std::size_t;
    using value_type = T;
    using reference = const T&;
    using category = readable_property_map_tag;
};
}

template <typename T>
const T& get(const VectorPropertyMap<T>& pm, std::size_t idx) {
    return (*pm.data)[idx];
}

// ==========================================================================
// CGAL kernel + type aliases
// ==========================================================================

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using Vector_3 = Kernel::Vector_3;
using Plane_3 = Kernel::Plane_3;

using PointMap = VectorPropertyMap<Point_3>;
using NormalMap = VectorPropertyMap<Vector_3>;

using NeighborQuery = CGAL::Shape_detection::Point_set::K_neighbor_query<
    Kernel, std::size_t, PointMap>;

using RegionType = CGAL::Shape_detection::Point_set::Least_squares_plane_fit_region<
    Kernel, std::size_t, PointMap, NormalMap>;

using RegionGrowing = CGAL::Shape_detection::Region_growing<
    NeighborQuery, RegionType,
    typename RegionType::Region_index_map,
    CGAL::Shape_detection::Region_growing_default_visitor>;

// ==========================================================================
// Impl must be defined BEFORE RecordingVisitor (RecordingVisitor uses Impl*).
// ==========================================================================

struct RegionGrowingRunner::Impl {
    std::vector<RG_Point3d>  input_points;
    std::vector<RG_Vector3d> input_normals;
    std::vector<Point_3>      cg_points;
    std::vector<Vector_3>     cg_normals;
    bool has_input = false;
    int  total_pts = 0;

    RG_Config cfg{};
    bool live_preview = false;

    // Results
    std::vector<RG_RegionResult> results;
    mutable std::mutex result_mutex;

    // Live queue
    std::deque<RG_FrameEvent> live_queue;
    std::mutex live_mutex;

    // Cancel / Error / Pause
    std::atomic<bool> cancel_flag{ false };
    std::atomic<bool> paused{ false };
    std::atomic<bool> step_flag{ false };
    std::atomic<bool> done{ false };
    std::mutex error_mutex;
    std::string error_message;
    std::atomic<bool> has_error_flag{ false };

    // Running stats
    std::atomic<int> running_visited{ 0 };
    std::atomic<int> running_progress{ 0 };
    std::atomic<int> running_seed_attempts{ 0 };
    std::atomic<int> running_rejected_regions{ 0 };
    std::atomic<int> running_accepted_regions{ 0 };
    std::atomic<int> running_region_size{ 0 };
    std::atomic<int> running_current_seed{ -1 };
    std::atomic<int> running_frontier_items{ 0 };
    std::atomic<int> running_neighbor_accepted{ 0 };
    std::atomic<int> running_neighbor_rejected{ 0 };
    std::atomic<int> running_primitive_refits{ 0 };

    // Timing buckets in nanoseconds. Single-writer (worker thread) so
    // std::atomic isn't strictly required for correctness, but keeping
    // them atomic so debug_stats() on the main thread sees torn-free
    // snapshots.
    std::atomic<int64_t> ns_total{ 0 };
    std::atomic<int64_t> ns_setup{ 0 };
    std::atomic<int64_t> ns_accepted_regions{ 0 };
    std::atomic<int64_t> ns_rejected_seeds{ 0 };
    std::atomic<int64_t> ns_visitor_overhead{ 0 };
};

// ==========================================================================
// RecordingVisitor: concrete visitor for process visualization.
// ==========================================================================

namespace {

static constexpr std::size_t LIVE_QUEUE_MAX = 8192;

struct RecordingVisitor {
    RegionGrowingRunner::Impl* impl = nullptr;
    const RegionType* region_type = nullptr;

    int neighbor_accepted_count = 0;
    int neighbor_rejected_count = 0;
    int frontier_count = 0;
    int progress_count = 0;
    int paced_event_count = 0;        // accumulator for pacing sleep
    double cached_plane[4] = { 0,0,0,0 };
    int current_seed = -1;
    int current_region_id = 0;

    // Per-seed timer. on_seed_selected records the start; on_region_accepted /
    // on_region_rejected stops and bins the elapsed into the right bucket.
    // Single-writer (worker thread) field; no atomic needed.
    std::chrono::steady_clock::time_point seed_t0;

    // instrument_full: when true the visitor maintains the debug atomics,
    // heartbeats, and per-event chrono::now overhead. Set only when the user
    // asked for live preview. In non-live runs the visitor stays installed so
    // on_region_accepted can still record results, but the per-event
    // bookkeeping is skipped to keep the algorithm at near-vanilla speed.
    bool instrument_full = false;

    // --- hooks (called from CGAL Region_growing via visitor template param) ---

    void on_seed_selected(std::size_t seed) {
        current_seed = (int)seed;
        seed_t0 = std::chrono::steady_clock::now();
        // seed_attempts is the cheapest meaningful UI progress counter,
        // updated in both modes so the dialog's "attempts X/Y" line
        // stays informative even with instrument_full=false.
        impl->running_seed_attempts.fetch_add(1, std::memory_order_relaxed);
        if (!instrument_full) return;
        impl->running_current_seed.store(current_seed, std::memory_order_relaxed);
        impl->running_region_size.store(0); // reset for new region
        log_heartbeat("seed");
        if (!impl->live_preview || !impl->cfg.live_show_tentative) {
            gate();
            return;
        }
        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::SeedSelected;
        ev.seed_index = current_seed;
        ev.region_id = current_region_id;
        fill_point(ev.point, seed);
        push_event(ev);
        gate();
    }

    void on_frontier_item(std::size_t item) {
        // Per-event hot path: in non-instrument mode return immediately so
        // CGAL stays close to a vanilla default-visitor call site.
        if (!instrument_full) return;
        ++frontier_count;
        impl->running_frontier_items.fetch_add(1, std::memory_order_relaxed);
        log_heartbeat("frontier");
        if (!impl->live_preview || !impl->cfg.live_show_tentative) return;
        int iv = impl->cfg.neighbor_event_interval;
        bool throttled = impl->cfg.live_throttle && !impl->paused.load();
        if (throttled && frontier_count % std::max(1, iv) != 0) return;

        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::FrontierItem;
        ev.item_index = (int)item;
        ev.region_size = impl->running_region_size.load();
        fill_point(ev.point, item);
        push_event(ev);
    }

    void on_neighbor_accepted(std::size_t item, std::size_t neighbor) {
        // Hot path: call count = N * avg_kNN. In non-instrument mode skip
        // everything; the debug atomics and the heartbeat are only useful
        // when there's a live overlay to look at.
        if (!instrument_full) return;
        ++neighbor_accepted_count;
        impl->running_neighbor_accepted.fetch_add(1, std::memory_order_relaxed);
        impl->running_region_size.fetch_add(1);
        log_heartbeat("neighbor_accept");
        if (!impl->live_preview || !impl->cfg.live_show_tentative) return;
        if (impl->cfg.live_throttle && !impl->paused.load()) {
            const int iv = std::max(1, impl->cfg.accepted_event_interval);
            if (neighbor_accepted_count % iv != 0)
                return;
        }
        // Sampled accepted events are still paced in push_event below.
        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::NeighborAccepted;
        ev.item_index = (int)item;
        ev.neighbor_index = (int)neighbor;
        ev.region_id = current_region_id;
        ev.region_size = impl->running_region_size.load();
        fill_point(ev.point, neighbor);
        std::memcpy(ev.plane, cached_plane, sizeof(cached_plane));
        push_event(ev);
    }

    void on_neighbor_rejected(std::size_t item, std::size_t neighbor) {
        if (!instrument_full) return;
        ++neighbor_rejected_count;
        impl->running_neighbor_rejected.fetch_add(1, std::memory_order_relaxed);
        log_heartbeat("neighbor_reject");
        if (!impl->live_preview || !impl->cfg.live_show_tentative) return;
        // Rejected events don't drive coloring; keep them sparse to avoid
        // stealing pacing budget from accepted events.
        int iv = std::max(1, impl->cfg.neighbor_event_interval);
        if (impl->cfg.live_throttle && !impl->paused.load() &&
            neighbor_rejected_count % iv != 0) return;

        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::NeighborRejected;
        ev.item_index = (int)item;
        ev.neighbor_index = (int)neighbor;
        ev.region_size = impl->running_region_size.load();
        fill_point(ev.point, neighbor);
        push_event(ev);
    }

    void on_primitive_refit(const std::vector<std::size_t>& region) {
        // cached_plane MUST stay up to date in both modes; on_region_accepted
        // stores it into the result record. Only the debug atomic + event
        // push are gated.
        if (region_type) {
            Plane_3 plane = region_type->primitive();
            double a = CGAL::to_double(plane.a());
            double b = CGAL::to_double(plane.b());
            double c = CGAL::to_double(plane.c());
            double d = CGAL::to_double(plane.d());
            double len = std::sqrt(a * a + b * b + c * c);
            if (len > 1e-30) { a /= len; b /= len; c /= len; d /= len; }
            cached_plane[0] = a; cached_plane[1] = b;
            cached_plane[2] = c; cached_plane[3] = d;
        }
        if (!instrument_full) return;
        impl->running_primitive_refits.fetch_add(1, std::memory_order_relaxed);
        log_heartbeat("refit");
        if (!impl->live_preview || !impl->cfg.live_show_tentative) return;

        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::PrimitiveRefit;
        ev.region_size = (int)region.size();
        ev.region_id = current_region_id;
        std::memcpy(ev.plane, cached_plane, sizeof(cached_plane));
        push_event(ev);
    }

    void on_fit_check(bool fits) {
        if (!instrument_full) return;
        if (!impl->live_preview || !impl->cfg.live_show_tentative || fits) return;
        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::PrimitiveRefit;
        ev.region_size = impl->running_region_size.load();
        ev.region_id = current_region_id;
        std::memcpy(ev.plane, cached_plane, sizeof(cached_plane));
        push_event(ev);
    }

    void on_region_accepted(int region_id, const std::vector<std::size_t>& region) {
        current_region_id = region_id + 1;
        impl->running_accepted_regions.store(current_region_id);
        impl->running_visited.fetch_add((int)region.size(), std::memory_order_relaxed);
        bin_seed_time(impl->ns_accepted_regions);

        if (instrument_full) log_heartbeat("accepted", true);

        // Store final result (always, regardless of live_preview)
        RG_RegionResult res;
        res.region_id = region_id;
        res.size = (int)region.size();
        std::memcpy(res.plane, cached_plane, sizeof(cached_plane));
        for (auto idx : region) res.indices.push_back((int)idx);
        {
            std::lock_guard<std::mutex> lk(impl->result_mutex);
            impl->results.push_back(std::move(res));
        }

        if (!instrument_full) return;
        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::RegionAccepted;
        ev.region_id = region_id;
        ev.seed_index = current_seed;
        ev.region_size = (int)region.size();
        ev.accepted_regions = current_region_id;
        ev.visited_points = impl->running_visited.load();
        std::memcpy(ev.plane, cached_plane, sizeof(cached_plane));
        push_event(ev);
    }

    void on_region_rejected() {
        impl->running_rejected_regions.fetch_add(1, std::memory_order_relaxed);
        bin_seed_time(impl->ns_rejected_seeds);
        if (!instrument_full) return;
        log_heartbeat("rejected");
        if (!impl->live_preview || !impl->cfg.live_show_tentative) return;
        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::RegionRejected;
        ev.seed_index = current_seed;
        push_event(ev);
    }

    bool should_stop() const {
        if (!impl)
            return false;
        return impl->cancel_flag.load(std::memory_order_relaxed);
    }

    void on_progress(int visited, int total, int regions) {
        // running_progress drives the dialog's progress bar even in non-live
        // mode, so this store is always done. The heavier bookkeeping below
        // is gated on instrument_full.
        impl->running_progress.store(visited, std::memory_order_relaxed);
        int current_regions = impl->running_accepted_regions.load(std::memory_order_relaxed);
        if (regions > current_regions)
            impl->running_accepted_regions.store(regions, std::memory_order_relaxed);
        if (!instrument_full) return;
        ++progress_count;
        log_heartbeat("progress");
        if (!impl->live_preview) return;
        int iv = impl->cfg.progress_event_interval;
        bool throttled = impl->cfg.live_throttle && !impl->paused.load();
        if (throttled && progress_count % std::max(1, iv) != 0) return;

        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::Progress;
        ev.step = visited;
        ev.visited_points = visited;
        ev.total_points = total;
        ev.accepted_regions = impl->running_accepted_regions.load(std::memory_order_relaxed);
        push_event(ev);
    }

private:
    // Bin the elapsed time since on_seed_selected into the given bucket
    // (ns_accepted_regions or ns_rejected_seeds). Cheap: one chrono::now,
    // one subtract, one atomic add. Active in both live and non-live modes
    // so the post-run timing breakdown is always available.
    void bin_seed_time(std::atomic<int64_t>& bucket) {
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        const int64_t dt =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - seed_t0).count();
        if (dt > 0)
            bucket.fetch_add(dt, std::memory_order_relaxed);
    }

    void log_heartbeat(const char*, bool = false) {
    }


    void fill_point(double out[3], std::size_t idx) {
        if (idx >= impl->cg_points.size()) return;
        auto& p = impl->cg_points[idx];
        out[0] = CGAL::to_double(p.x());
        out[1] = CGAL::to_double(p.y());
        out[2] = CGAL::to_double(p.z());
    }

    void push_event(RG_FrameEvent& ev) {
        ev.total_points = impl->total_pts;
        if (!impl->live_preview) return;
        {
            auto keep_live_event = [](int type) {
                return type == RG_FrameEvent::SeedSelected ||
                    type == RG_FrameEvent::RegionAccepted ||
                    type == RG_FrameEvent::RegionRejected ||
                    type == RG_FrameEvent::Done;
            };
            std::lock_guard<std::mutex> lk(impl->live_mutex);
            if (impl->live_queue.size() >= LIVE_QUEUE_MAX) {
                if (!keep_live_event(ev.type))
                    return;
                auto it = std::find_if(impl->live_queue.begin(), impl->live_queue.end(),
                    [&](const RG_FrameEvent& old_ev) {
                        return !keep_live_event(old_ev.type);
                    });
                if (it != impl->live_queue.end())
                    impl->live_queue.erase(it);
                else
                    impl->live_queue.pop_front();
            }
            impl->live_queue.push_back(ev);
        }
        // Pacing: the ONE knob that controls "how fast does the BFS edge
        // appear to spread". Default 50 events per 5 ms is about 10k pts/s, which
        // at 60 FPS is about 166 newly-colored points per frame; slow enough
        // to see individual points appear, fast enough that a 100k cloud
        // finishes in ~10 s. Pause/cancel break out via the gate() check
        // the next event up; no need to interrupt sleep early.
        if (impl->cfg.live_throttle && !impl->paused.load(std::memory_order_relaxed)) {
            ++paced_event_count;
            int batch = std::max(1, impl->cfg.pacing_batch);
            if (paced_event_count >= batch) {
                paced_event_count = 0;
                int ms = std::max(0, impl->cfg.pacing_sleep_ms);
                if (ms > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
        }
    }

    void gate() {
        if (!impl->live_preview) return;
        if (impl->paused.load(std::memory_order_relaxed)) {
            while (impl->paused.load(std::memory_order_relaxed) &&
                !impl->cancel_flag.load(std::memory_order_relaxed)) {
                if (impl->step_flag.exchange(false, std::memory_order_relaxed))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }
};

} // anonymous namespace

// ==========================================================================
// Custom Region_growing type with RecordingVisitor
// ==========================================================================

using RegionGrowingVis = CGAL::Shape_detection::Region_growing<
    NeighborQuery, RegionType,
    typename RegionType::Region_index_map,
    RecordingVisitor>;

// ==========================================================================
// Constructor / Destructor
// ==========================================================================

RegionGrowingRunner::RegionGrowingRunner() : impl_(std::make_unique<Impl>()) {}
RegionGrowingRunner::~RegionGrowingRunner() = default;

// ==========================================================================
// Input
// ==========================================================================

void RegionGrowingRunner::set_input(
    const std::vector<RG_Point3d>& points,
    const std::vector<RG_Vector3d>& normals)
{
    impl_->input_points = points;
    impl_->input_normals = normals;
    impl_->has_input = true;
    impl_->total_pts = (int)points.size();

    impl_->cg_points.clear();
    impl_->cg_normals.clear();
    impl_->cg_points.reserve(points.size());
    impl_->cg_normals.reserve(normals.size());
    for (size_t i = 0; i < points.size(); ++i) {
        impl_->cg_points.emplace_back(points[i].x, points[i].y, points[i].z);
    }
    for (size_t i = 0; i < normals.size(); ++i) {
        impl_->cg_normals.emplace_back(normals[i].x, normals[i].y, normals[i].z);
    }
}

// ==========================================================================
// Run
// ==========================================================================

void RegionGrowingRunner::run(const RG_Config& cfg) {
    if (!impl_->has_input || impl_->cg_points.empty()) {
        set_error("No input points set");
        return;
    }

    impl_->cfg = cfg;
    impl_->live_preview = cfg.live_preview;
    impl_->cancel_flag = false;
    impl_->paused = false;
    impl_->step_flag = false;
    impl_->done = false;
    impl_->running_visited = 0;
    impl_->running_progress = 0;
    impl_->running_seed_attempts = 0;
    impl_->running_rejected_regions = 0;
    impl_->running_accepted_regions = 0;
    impl_->running_region_size = 0;
    impl_->running_current_seed = -1;
    impl_->running_frontier_items = 0;
    impl_->running_neighbor_accepted = 0;
    impl_->running_neighbor_rejected = 0;
    impl_->running_primitive_refits = 0;
    impl_->ns_total = 0;
    impl_->ns_setup = 0;
    impl_->ns_accepted_regions = 0;
    impl_->ns_rejected_seeds = 0;
    impl_->ns_visitor_overhead = 0;

    // Clear previous results and live queue
    {
        std::lock_guard<std::mutex> lk(impl_->result_mutex);
        impl_->results.clear();
    }
    {
        std::lock_guard<std::mutex> lk(impl_->live_mutex);
        impl_->live_queue.clear();
    }

    clear_error();

    // Auto-tune parameters (work on mutable copy in impl_->cfg)
    auto& acfg = impl_->cfg;
    int n = impl_->total_pts;
    if (acfg.k_neighbors <= 0) acfg.k_neighbors = 12;
    if (acfg.max_distance < 0)
        acfg.max_distance = 0.05;
    if (acfg.min_region_size < 0)
        acfg.min_region_size = std::max(30, (int)(n * 0.0001));

    // Build CGAL objects.
    // Item = std::size_t (index). point_map/normal_map map index to Point_3/Vector_3.
    // The input range for K_neighbor_query is a range of Items (indices), not points.
    PointMap point_map(impl_->cg_points);
    NormalMap normal_map(impl_->cg_normals);

    std::vector<std::size_t> item_range;
    item_range.reserve(impl_->cg_points.size());
    for (std::size_t i = 0; i < impl_->cg_points.size(); ++i)
        item_range.push_back(i);

    using Clock = std::chrono::steady_clock;
    const auto t_run0 = Clock::now();

    NeighborQuery neighbor_query(
        item_range,
        CGAL::parameters::point_map(point_map)
            .k_neighbors((std::size_t)acfg.k_neighbors));

    RegionType region_type(
        CGAL::parameters::point_map(point_map)
            .normal_map(normal_map)
            .maximum_distance(acfg.max_distance)
            .maximum_angle(acfg.max_angle_deg)
            .minimum_region_size((std::size_t)acfg.min_region_size));

    // Sort seeds by local plane-fit quality. Seeds on flat areas are tried
    // first and cover most of the cloud in a few regions, so the remaining
    // seeds (on curved/noisy areas) are quickly skipped because their
    // neighbours are all visited. Without sorting, vanilla CGAL wastes ~80%
    // of runtime growing and rejecting thousands of failed regions on bad
    // seeds; the infamous "tail" problem.
    using Sorting = CGAL::Shape_detection::Point_set::Least_squares_plane_fit_sorting<
        Kernel, std::size_t, NeighborQuery, PointMap, NormalMap>;
    Sorting sorting(item_range, neighbor_query,
        CGAL::parameters::point_map(point_map).normal_map(normal_map));
    sorting.sort();

    RegionGrowingVis rg(item_range, sorting.ordered(),
        neighbor_query, region_type);

    rg.visitor().impl = impl_.get();
    rg.visitor().region_type = &region_type;
    // instrument_full = full debug atomics + heartbeat. Non-live runs only
    // need the visitor to record accepted regions; the per-event accounting
    // (~50 ns x tens of millions of events) is skipped.
    rg.visitor().instrument_full = impl_->live_preview;

    const auto t_setup_end = Clock::now();
    impl_->ns_setup.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t_setup_end - t_run0).count(),
        std::memory_order_relaxed);

    std::vector<std::pair<Plane_3, std::vector<std::size_t>>> cgal_regions;
    rg.detect(std::back_inserter(cgal_regions));
    impl_->running_progress.store(impl_->total_pts, std::memory_order_relaxed);

    const auto t_run_end = Clock::now();
    const int64_t ns_total =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t_run_end - t_run0).count();
    impl_->ns_total.store(ns_total, std::memory_order_relaxed);
    const int64_t ns_setup_v = impl_->ns_setup.load(std::memory_order_relaxed);
    const int64_t ns_acc = impl_->ns_accepted_regions.load(std::memory_order_relaxed);
    const int64_t ns_rej = impl_->ns_rejected_seeds.load(std::memory_order_relaxed);
    (void)ns_setup_v;
    (void)ns_acc;
    (void)ns_rej;

    // Push Done event
    {
        RG_FrameEvent ev{};
        ev.type = RG_FrameEvent::Done;
        ev.accepted_regions = (int)impl_->results.size();
        ev.visited_points = impl_->running_visited.load();
        ev.total_points = impl_->total_pts;
        if (impl_->live_preview) {
            std::lock_guard<std::mutex> lk(impl_->live_mutex);
            impl_->live_queue.push_back(ev);
        }
    }

    impl_->done = true;
}

// ==========================================================================
// Live preview
// ==========================================================================

bool RegionGrowingRunner::drain_live_events(std::vector<RG_FrameEvent>& out) {
    out.clear();
    std::lock_guard<std::mutex> lk(impl_->live_mutex);
    if (impl_->live_queue.empty()) return false;
    for (auto& e : impl_->live_queue) out.push_back(e);
    impl_->live_queue.clear();
    return true;
}

// ==========================================================================
// Cancel / Pause / Resume / Step
// ==========================================================================

void RegionGrowingRunner::cancel() { impl_->cancel_flag = true; }
bool RegionGrowingRunner::is_cancelled() const { return impl_->cancel_flag; }

void RegionGrowingRunner::pause() { impl_->paused = true; }
void RegionGrowingRunner::resume() { impl_->paused = false; }
void RegionGrowingRunner::step() { impl_->step_flag = true; }
bool RegionGrowingRunner::is_paused() const { return impl_->paused; }

// ==========================================================================
// Error
// ==========================================================================

bool RegionGrowingRunner::has_error() const { return impl_->has_error_flag; }

std::string RegionGrowingRunner::last_error() const {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    return impl_->error_message;
}

void RegionGrowingRunner::clear_error() {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    impl_->has_error_flag = false;
    impl_->error_message.clear();
}

void RegionGrowingRunner::set_error(const std::string& msg) {
    std::lock_guard<std::mutex> lk(impl_->error_mutex);
    impl_->has_error_flag = true;
    impl_->error_message = msg;
}

// ==========================================================================
// Status
// ==========================================================================

bool  RegionGrowingRunner::is_done()              const { return impl_->done; }
int   RegionGrowingRunner::num_regions()          const {
    std::lock_guard<std::mutex> lk(impl_->result_mutex);
    return (int)impl_->results.size();
}
int   RegionGrowingRunner::total_points()         const { return impl_->total_pts; }
int   RegionGrowingRunner::current_region_size()  const { return impl_->running_region_size.load(); }
float RegionGrowingRunner::progress()    const {
    if (impl_->total_pts <= 0) return 0.f;
    int covered = std::max(
        impl_->running_progress.load(std::memory_order_relaxed),
        impl_->running_visited.load(std::memory_order_relaxed));
    covered = std::max(
        covered,
        impl_->running_seed_attempts.load(std::memory_order_relaxed));
    covered = std::max(0, std::min(covered, impl_->total_pts));
    return (float)covered / (float)impl_->total_pts;
}

RG_DebugStats RegionGrowingRunner::debug_stats() const {
    RG_DebugStats stats;
    stats.total_points = impl_->total_pts;
    stats.seed_attempts = impl_->running_seed_attempts.load(std::memory_order_relaxed);
    stats.scan_progress = impl_->running_progress.load(std::memory_order_relaxed);
    stats.visited_points = impl_->running_visited.load(std::memory_order_relaxed);
    stats.accepted_regions = impl_->running_accepted_regions.load(std::memory_order_relaxed);
    stats.rejected_regions = impl_->running_rejected_regions.load(std::memory_order_relaxed);
    stats.current_region_size = impl_->running_region_size.load(std::memory_order_relaxed);
    stats.current_seed = impl_->running_current_seed.load(std::memory_order_relaxed);
    stats.frontier_items = impl_->running_frontier_items.load(std::memory_order_relaxed);
    stats.neighbor_accepted = impl_->running_neighbor_accepted.load(std::memory_order_relaxed);
    stats.neighbor_rejected = impl_->running_neighbor_rejected.load(std::memory_order_relaxed);
    stats.primitive_refits = impl_->running_primitive_refits.load(std::memory_order_relaxed);
    stats.cancel_requested = impl_->cancel_flag.load(std::memory_order_relaxed);
    auto ns_to_ms = [](int64_t ns) { return ns / 1000000.0; };
    stats.ms_total = ns_to_ms(impl_->ns_total.load(std::memory_order_relaxed));
    stats.ms_setup = ns_to_ms(impl_->ns_setup.load(std::memory_order_relaxed));
    stats.ms_accepted_regions = ns_to_ms(impl_->ns_accepted_regions.load(std::memory_order_relaxed));
    stats.ms_rejected_seeds = ns_to_ms(impl_->ns_rejected_seeds.load(std::memory_order_relaxed));
    stats.ms_visitor_overhead = ns_to_ms(impl_->ns_visitor_overhead.load(std::memory_order_relaxed));
    return stats;
}

void RegionGrowingRunner::get_region(int idx, RG_RegionResult& out) const {
    std::lock_guard<std::mutex> lk(impl_->result_mutex);
    if (idx >= 0 && idx < (int)impl_->results.size())
        out = impl_->results[idx];
}
