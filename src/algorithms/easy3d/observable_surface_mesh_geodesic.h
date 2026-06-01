/********************************************************************
 * Copyright (C) 2020-2021 by Liangliang Nan <liangliang.nan@gmail.com>
 * Copyright (C) 2011-2020 the Polygon Mesh Processing Library developers.
 * Copyright (C) 2026 3D Claw contributors
 *
 * The core algorithm in this file is adapted from Easy3D/PMP geodesic
 * propagation. 3D Claw keeps the observer/progress contract in product-owned
 * code so official Easy3D can be consumed as an external dependency.
 ********************************************************************/

#ifndef CLAW3D_ALGORITHMS_EASY3D_OBSERVABLE_SURFACE_MESH_GEODESIC_H
#define CLAW3D_ALGORITHMS_EASY3D_OBSERVABLE_SURFACE_MESH_GEODESIC_H

/// Easy3D geodesic-distance helper with progress observation hooks.

#include <easy3d/core/surface_mesh.h>
#include <cfloat>
#include <climits>
#include <map>
#include <set>
#include <vector>

namespace claw3d::algo {

    /**
     * \brief This class computes geodesic distance from a set of seed vertices.
     * \class ObservableSurfaceMeshGeodesic
     * \details The method works by a Dykstra-like breadth first traversal from the seed vertices, implemented by a
     * heap structure. See the following paper for more details:
     *  - Kimmel and Sethian. Computing geodesic paths on manifolds. Proceedings of the National Academy of Sciences,
     *    95(15):8431-8435, 1998.
     */
    class ObservableSurfaceMeshGeodesic {
    public:
        //! \brief Optional observer hook for live front-propagation
        //! visualization. Default is a no-op so existing callers compile
        //! and behave unchanged. Set via set_observer().
        //!
        //! on_vertex_settled() fires immediately after a vertex is popped
        //! off the priority queue and marked processed. go_further()
        //! returning false breaks the propagation loop (cooperative cancel).
        struct ObserverHook {
            virtual ~ObserverHook() = default;
            virtual void on_vertex_settled(unsigned int /*settled_count*/,
                                           easy3d::SurfaceMesh::Vertex /*v*/,
                                           float /*distance*/,
                                           std::size_t /*front_size*/) {}
            virtual bool go_further() { return true; }
        };

        //! \brief Construct from mesh.
        //! \param mesh The mesh on which to compute the geodesic distances.
        //! \param use_virtual_edges A flag to control the use of virtual edges. Default: true.
        //! \sa compute() to actually compute the geodesic distances.
        explicit ObservableSurfaceMeshGeodesic(easy3d::SurfaceMesh *mesh, bool use_virtual_edges = true);

        // destructor
        ~ObservableSurfaceMeshGeodesic();

        //! \brief Install an optional observer. Pass nullptr to disable.
        //! Lifetime is owned by the caller and must outlive compute().
        void set_observer(ObserverHook *hook) { observer_ = hook; }

        //! \brief Compute geodesic distances from specified seed points.
        //! \details The results are store as SurfaceMesh::VertexProperty<float> with a name "v:geodesic:distance".
        //! \param[in] seed The vector of seed vertices.
        //! \param[in] max_dist The maximum distance up to which to compute the
        //! geodesic distances.
        //! \param[in] max_num The maximum number of neighbors up to which to
        //! compute the geodesic distances.
        //! \param[out] neighbors The vector of neighbor vertices.
        //! \return The number of neighbors that have been found.
        unsigned int compute(const std::vector<easy3d::SurfaceMesh::Vertex> &seed,
                             float max_dist = FLT_MAX,
                             unsigned int max_num = INT_MAX,
                             std::vector<easy3d::SurfaceMesh::Vertex> *neighbors = nullptr);

        //! \brief Access the computed geodesic distance.
        //! \param[in] v The vertex for which to return the geodesic distance.
        //! \return The geodesic distance of vertex \p v.
        //! \pre The function compute() has been called before.
        //! \pre The vertex \p v needs to be a valid vertex handle of the mesh
        //! used during construction.
        float operator()(easy3d::SurfaceMesh::Vertex v) const { return distance_[v]; }

        //! \brief Use the normalized distances as texture coordinates
        //! \details Stores the normalized distances in a vertex property of type
        //! TexCoord named "v:tex". Re-uses any existing vertex property of the
        //! same type and name.
        void distance_to_texture_coordinates();

    private: // private types
        // functor for comparing two vertices w.r.t. their geodesic distance
        class VertexCmp {
        public:
            explicit VertexCmp(const easy3d::SurfaceMesh::VertexProperty<float> &dist) : dist_(dist) {}

            bool operator()(easy3d::SurfaceMesh::Vertex v0, easy3d::SurfaceMesh::Vertex v1) const {
                return ((dist_[v0] == dist_[v1]) ? (v0 < v1)
                                                 : (dist_[v0] < dist_[v1]));
            }

        private:
            const easy3d::SurfaceMesh::VertexProperty<float> &dist_;
        };

        // priority queue using geodesic distance as sorting criterion
        using PriorityQueue =
            std::set<easy3d::SurfaceMesh::Vertex, VertexCmp>;

        // virtual edges for walking through obtuse triangles
        struct VirtualEdge {
            VirtualEdge(easy3d::SurfaceMesh::Vertex v, float l) : vertex(v), length(l) {}

            easy3d::SurfaceMesh::Vertex vertex;
            float length;
        };

        // set for storing virtual edges
        using VirtualEdges =
            std::map<easy3d::SurfaceMesh::Halfedge, VirtualEdge>;

    private: // private methods
        void find_virtual_edges();

        unsigned int init_front(const std::vector<easy3d::SurfaceMesh::Vertex> &seed,
                                std::vector<easy3d::SurfaceMesh::Vertex> *neighbors);

        unsigned int propagate_front(float max_dist, unsigned int max_num,
                                     std::vector<easy3d::SurfaceMesh::Vertex> *neighbors);

        void heap_vertex(easy3d::SurfaceMesh::Vertex v);

        float distance(easy3d::SurfaceMesh::Vertex v0, easy3d::SurfaceMesh::Vertex v1,
                       easy3d::SurfaceMesh::Vertex v2, float r0 = FLT_MAX,
                       float r1 = FLT_MAX);

    private: // private data
        easy3d::SurfaceMesh *mesh_;

        bool use_virtual_edges_;
        VirtualEdges virtual_edges_;

        PriorityQueue *front_;

        easy3d::SurfaceMesh::VertexProperty<float> distance_;
        easy3d::SurfaceMesh::VertexProperty<bool> processed_;

        // Optional observer; nullptr means no instrumentation (zero cost).
        ObserverHook *observer_ = nullptr;
    };

} // namespace claw3d::algo


#endif // CLAW3D_ALGORITHMS_EASY3D_OBSERVABLE_SURFACE_MESH_GEODESIC_H
