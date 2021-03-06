#include "boundaries.h"
#include "test_flag.h"
#include "logger.h"

#include <cmath>
#include <algorithm>

using namespace std;

CuboidBoundary::CuboidBoundary(const Vec3f& c0, const Vec3f& c1)
        : c0(c0)
        , c1(c1) {
}

bool CuboidBoundary::inside(const Vec3f& v) const {
    return (c0 < v).all() && (v < c1).all();
}

CuboidBoundary CuboidBoundary::get_aabb() const {
    return *this;
}

Vec3f CuboidBoundary::get_dimensions() const {
    return c1 - c0;
}

CuboidBoundary get_cuboid_boundary(const vector<Vec3f>& vertices) {
    Vec3f mini, maxi;
    mini = maxi = vertices.front();
    for (auto i = vertices.begin() + 1; i != vertices.end(); ++i) {
        mini = i->apply(mini, [](auto a, auto b) { return min(a, b); });
        maxi = i->apply(maxi, [](auto a, auto b) { return max(a, b); });
    }
    return CuboidBoundary(mini, maxi);
}

SphereBoundary::SphereBoundary(const Vec3f& c, float radius)
        : c(c)
        , radius(radius)
        , boundary(-radius, radius) {
}

bool SphereBoundary::inside(const Vec3f& v) const {
    return (v - c).mag() < radius;
}

CuboidBoundary SphereBoundary::get_aabb() const {
    return boundary;
}

Vec3i MeshBoundary::hash_point(const Vec3f& v) const {
    return ((v - boundary.c0) / cell_size)
        .map([](auto i) -> int { return floor(i); });
}

vector<vector<MeshBoundary::reference_store>>
MeshBoundary::get_triangle_references() const {
    vector<vector<reference_store>> ret(DIVISIONS,
                                        vector<reference_store>(DIVISIONS));
    for (auto i = 0u; i != triangles.size(); ++i) {
        const auto& t = triangles[i];
        const auto bounding_box = get_cuboid_boundary(
            {vertices[t.v0], vertices[t.v1], vertices[t.v2]});
        const auto min_indices = hash_point(bounding_box.c0);
        const auto max_indices = hash_point(bounding_box.c1) + 1;

        for (auto j = min_indices.x; j != max_indices.x && j != DIVISIONS;
             ++j) {
            for (auto k = min_indices.y; k != max_indices.y && k != DIVISIONS;
                 ++k) {
                ret[j][k].push_back(i);
            }
        }
    }
    return ret;
}

MeshBoundary::MeshBoundary(const vector<Triangle>& triangles,
                           const vector<Vec3f>& vertices)
        : triangles(triangles)
        , vertices(vertices)
        , boundary(get_cuboid_boundary(vertices))
        , cell_size(boundary.get_dimensions() / DIVISIONS)
        , triangle_references(get_triangle_references()) {
#ifdef TESTING
    auto fname = build_string("./file-mesh.txt");
    ofstream file(fname);
    for (const auto& i : this->triangles) {
        auto v0 = this->vertices[i.v0];
        file << build_string(v0.x, " ", v0.y, " ", v0.z, " ");
        auto v1 = this->vertices[i.v1];
        file << build_string(v1.x, " ", v1.y, " ", v1.z, " ");
        auto v2 = this->vertices[i.v2];
        file << build_string(v2.x, " ", v2.y, " ", v2.z, " ");
        file << endl;
    }
#endif
}

class Ray {
public:
    Ray(const Vec3f& position = Vec3f(), const Vec3f& direction = Vec3f())
            : position(position)
            , direction(direction) {
    }
    Vec3f position;
    Vec3f direction;
};

class Intersects {
public:
    Intersects()
            : intersects(false) {
    }

    Intersects(float distance)
            : intersects(true)
            , distance(distance) {
    }

    bool intersects;
    float distance;
};

ostream& operator<<(ostream& strm, const Intersects& obj) {
    return strm << "Intersects {" << obj.intersects << ", " << obj.distance
                << "}";
}

using TriangleVerts = array<Vec3f, 3>;

Intersects triangle_intersection(const TriangleVerts& tri, const Ray& ray) {
    auto EPSILON = 0.0001;

    auto e0 = tri[1] - tri[0];
    auto e1 = tri[2] - tri[0];

    auto pvec = ray.direction.cross(e1);
    auto det = e0.dot(pvec);

    if (-EPSILON < det && det < EPSILON)
        return Intersects();

    auto invdet = 1 / det;
    auto tvec = ray.position - tri[0];
    auto ucomp = invdet * tvec.dot(pvec);

    if (ucomp < 0 || 1 < ucomp)
        return Intersects();

    auto qvec = tvec.cross(e0);
    auto vcomp = invdet * ray.direction.dot(qvec);

    if (vcomp < 0 || 1 < vcomp + ucomp)
        return Intersects();

    auto dist = invdet * e1.dot(qvec);

    if (dist < 0)
        return Intersects();

    return Intersects(dist);
}

Intersects triangle_intersection(const Triangle& tri,
                                 const vector<Vec3f>& vertices,
                                 const Ray& ray) {
    const auto v0 = vertices[tri.v0];
    const auto v1 = vertices[tri.v1];
    const auto v2 = vertices[tri.v2];
    return triangle_intersection({{v0, v1, v2}}, ray);
}

MeshBoundary::reference_store MeshBoundary::get_references(
    const Vec3i& i) const {
    return get_references(i.x, i.y);
}

MeshBoundary::reference_store MeshBoundary::get_references(int x, int y) const {
    if (0 <= x && x < DIVISIONS && 0 <= y && y < DIVISIONS)
        return triangle_references[x][y];
    return reference_store();
}

bool MeshBoundary::inside(const Vec3f& v) const {
    //  cast ray through point along Z axis and check for intersections
    //  with each of the referenced triangles then count number of intersections
    //  on one side of the point
    //  if intersection number is even, point is outside, else it's inside
    const Ray ray(v, Vec3f(0, 0, 1));
    const auto references = get_references(hash_point(v));
    return count_if(references.begin(),
                    references.end(),
                    [this, &ray](const auto& i) {
                        auto intersection =
                            triangle_intersection(triangles[i], vertices, ray);
                        return intersection.intersects;
                    }) %
           2;
}

CuboidBoundary MeshBoundary::get_aabb() const {
    return boundary;
}
