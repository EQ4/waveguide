#include "tetrahedral_program.h"

using namespace std;

TetrahedralProgram::TetrahedralProgram(const cl::Context & context,
                                       bool build_immediate)
        : Program(context, source, build_immediate) {
}

const string TetrahedralProgram::source{
#ifdef DIAGNOSTIC
    "#define DIAGNOSTIC\n"
#endif
    R"(
    #define PORTS (4)

    typedef struct {
        int ports[PORTS];
        float3 position;
        bool inside;
    } Node;

    kernel void waveguide
    (   global float * current
    ,   global float * previous
    ,   global Node * nodes
    ,   unsigned long read
    ,   global float * output
    ) {
        size_t index = get_global_id(0);
        global Node * node = nodes + index;

        if (! node->inside) {
            return;
        }

        float temp = 0;

        //  waveguide logic goes here
        for (int i = 0; i != PORTS; ++i) {
            int port_index = node->ports[i];
            if (port_index >= 0 && nodes[port_index].inside)
                temp += current[port_index];
        }

        temp /= 2;
        temp -= previous[index];

        previous[index] = temp;

        if (index == read) {
            *output = previous[index];
        }
    }
    )"};
