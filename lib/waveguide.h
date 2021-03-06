#pragma once

#include "tetrahedral_program.h"
#include "iterative_tetrahedral_mesh.h"
#include "cl_structs.h"
#include "logger.h"
#include "conversions.h"

#include <array>
#include <type_traits>
#include <algorithm>

template <typename T>
class Waveguide {
public:
    struct PowerFunction {
        virtual float operator()(const Vec3f & a, const Vec3f & b) const = 0;
    };

    struct BasicPowerFunction : public PowerFunction {
        float operator()(const Vec3f & a, const Vec3f & b) const override {
            return (a == b).all() ? 1 : 0;
        }
    };

    struct InversePowerFunction : public PowerFunction {
        InversePowerFunction(float power)
                : power(power) {
        }

        float operator()(const Vec3f & a, const Vec3f & b) const override {
            return power / (a - b).mag();
        }

        const float power;
    };

    struct InverseSquarePowerFunction : public PowerFunction {
        InverseSquarePowerFunction(float power)
                : power(power) {
        }

        float operator()(const Vec3f & a, const Vec3f & b) const override {
            return power / (a - b).mag_squared();
        }

        const float power;
    };

    using size_type = std::vector<cl_float>::size_type;
    using kernel_type = decltype(std::declval<T>().get_kernel());

    Waveguide(const T & program, cl::CommandQueue & queue, size_type nodes)
            : queue(queue)
            , kernel(program.get_kernel())
            , nodes(nodes)
            , storage(
                  {{cl::Buffer(program.template getInfo<CL_PROGRAM_CONTEXT>(),
                               CL_MEM_READ_WRITE,
                               sizeof(cl_float) * nodes),
                    cl::Buffer(program.template getInfo<CL_PROGRAM_CONTEXT>(),
                               CL_MEM_READ_WRITE,
                               sizeof(cl_float) * nodes)}})
            , previous(&storage[0])
            , current(&storage[1])
            , output(program.template getInfo<CL_PROGRAM_CONTEXT>(),
                     CL_MEM_READ_WRITE,
                     sizeof(cl_float)) {
    }

    virtual ~Waveguide() noexcept = default;

    virtual cl_float run_step(size_type o,
                              cl::CommandQueue & queue,
                              kernel_type & kernel,
                              size_type nodes,
                              cl::Buffer & previous,
                              cl::Buffer & current,
                              cl::Buffer & output) = 0;

    virtual size_type get_index_for_coordinate(const Vec3f & v) const = 0;
    virtual Vec3f get_coordinate_for_index(size_type index) const = 0;

    size_type get_nodes() const {
        return nodes;
    }

    std::vector<cl_float> initialise_mesh(const PowerFunction & u,
                                          const Vec3f & excitation) {
        std::vector<size_type> indices(nodes);
        std::iota(indices.begin(), indices.end(), 0);

        std::vector<cl_float> ret(nodes);
        std::transform(indices.begin(),
                       indices.end(),
                       ret.begin(),
                       [this, &u, &excitation](auto i) {
                           return u(this->get_coordinate_for_index(i),
                                    excitation);
                       });
        return ret;
    }

    std::vector<cl_float> run(const Vec3f & e,
                              const PowerFunction & u,
                              size_type o,
                              size_type steps) {
        Logger::log("beginning simulation with: ", nodes, " nodes");

        std::vector<cl_float> n(nodes, 0);
        cl::copy(queue, n.begin(), n.end(), *previous);

        n = initialise_mesh(u, e);
        cl::copy(queue, n.begin(), n.end(), *current);

        std::vector<cl_float> ret(steps);

        auto counter = 0u;
        std::generate(
            ret.begin(),
            ret.end(),
            [this, &counter, &steps, &o] {
                auto ret = this->run_step(
                    o, queue, kernel, nodes, *previous, *current, output);

                std::swap(current, previous);

                auto percent = counter * 100 / (steps - 1);
                std::cout << "\r" << percent << "% done" << std::flush;

                counter += 1;

                return ret;
            });

        std::cout << std::endl;

        return ret;
    }

    std::vector<cl_float> run_basic(const Vec3f & e,
                                    size_type o,
                                    size_type steps) {
        auto estimated_source_index = get_index_for_coordinate(e);
        auto source_position = get_coordinate_for_index(estimated_source_index);
        return run(source_position, BasicPowerFunction(), o, steps);
    }

    std::vector<cl_float> run_inverse(const Vec3f & e,
                                      float power,
                                      size_type o,
                                      size_type steps) {
        return run(e, InversePowerFunction(power), o, steps);
    }

    std::vector<cl_float> run_inverse_square(const Vec3f & e,
                                             float power,
                                             size_type o,
                                             size_type steps) {
        return run(e, InverseSquarePowerFunction(power), o, steps);
    }

private:
    cl::CommandQueue & queue;
    kernel_type kernel;
    const size_type nodes;

    std::array<cl::Buffer, 2> storage;

    cl::Buffer * previous;
    cl::Buffer * current;

    cl::Buffer output;
};

class TetrahedralWaveguide : public Waveguide<TetrahedralProgram> {
public:
    TetrahedralWaveguide(const TetrahedralProgram & program,
                         cl::CommandQueue & queue,
                         const std::vector<Node> & nodes);
    virtual ~TetrahedralWaveguide() noexcept = default;

    cl_float run_step(size_type o,
                      cl::CommandQueue & queue,
                      kernel_type & kernel,
                      size_type nodes,
                      cl::Buffer & previous,
                      cl::Buffer & current,
                      cl::Buffer & output) override;

private:
    std::vector<Node> nodes;
    cl::Buffer node_buffer;
};

class IterativeTetrahedralWaveguide : public TetrahedralWaveguide {
public:
    IterativeTetrahedralWaveguide(const TetrahedralProgram & program,
                                  cl::CommandQueue & queue,
                                  const Boundary & boundary,
                                  float cube_side);
    virtual ~IterativeTetrahedralWaveguide() noexcept = default;

    size_type get_index_for_coordinate(const Vec3f & v) const override;
    Vec3f get_coordinate_for_index(size_type index) const override;

private:
    IterativeTetrahedralWaveguide(const TetrahedralProgram & program,
                                  cl::CommandQueue & queue,
                                  const IterativeTetrahedralMesh & mesh);
    IterativeTetrahedralMesh mesh;
};
