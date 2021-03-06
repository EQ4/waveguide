//  project internal
#include "waveguide.h"
#include "scene_data.h"
#include "test_flag.h"
#include "conversions.h"

#include "rayverb.h"

#include "cl_common.h"

//  dependency
#include "logger.h"
#include "filters_common.h"
#include "sinc.h"
#include "write_audio_file.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include "sndfile.hh"
#include "samplerate.h"

#include <gflags/gflags.h>

//  stdlib
#include <random>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>

using namespace std;
using namespace rapidjson;

// -1 <= z <= 1, -pi <= theta <= pi
cl_float3 spherePoint(float z, float theta) {
    const float ztemp = sqrtf(1 - z * z);
    return (cl_float3){{ztemp * cosf(theta), ztemp * sinf(theta), z, 0}};
}

vector<cl_float3> getRandomDirections(unsigned long num) {
    vector<cl_float3> ret(num);
    uniform_real_distribution<float> zDist(-1, 1);
    uniform_real_distribution<float> thetaDist(-M_PI, M_PI);
    auto seed = chrono::system_clock::now().time_since_epoch().count();
    default_random_engine engine(seed);

    for (auto && i : ret)
        i = spherePoint(zDist(engine), thetaDist(engine));

    return ret;
}

double a2db(double a) {
    return 20 * log10(a);
}

double db2a(double db) {
    return pow(10, db / 20);
}

vector<float> squintegrate(const std::vector<float> & sig) {
    vector<float> ret(sig.size());
    partial_sum(sig.rbegin(),
                sig.rend(),
                ret.rbegin(),
                [](auto i, auto j) { return i + j * j; });
    return ret;
}

int rt60(const vector<float> & sig) {
    auto squintegrated = squintegrate(sig);
    normalize(squintegrated);
    auto target = db2a(-60);
    return distance(squintegrated.begin(),
                    find_if(squintegrated.begin(),
                            squintegrated.end(),
                            [target](auto i) { return i < target; }));
}

MeshBoundary get_mesh_boundary(const SceneData & sd) {
    vector<Vec3f> v(sd.vertices.size());
    transform(sd.vertices.begin(),
              sd.vertices.end(),
              v.begin(),
              [](auto i) { return convert(i); });
    return MeshBoundary(sd.triangles, v);
}

vector<float> exponential_decay_envelope(int steps, float attenuation_factor) {
    vector<float> ret(steps);
    auto amp = 1.0f;
    generate(begin(ret),
             end(ret),
             [&amp, attenuation_factor] {
                 auto t = amp;
                 amp *= attenuation_factor;
                 return t;
             });
    return ret;
}

bool all_zero(const vector<float> & t) {
    auto ret = true;
    for (auto i = 0u; i != t.size(); ++i) {
        if (t[i] != 0) {
            Logger::log("non-zero at element: ", i, ", value: ", t[i]);
            ret = false;
        }
    }
    return ret;
}

int main(int argc, char ** argv) {
    Logger::restart();
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (argc != 5) {
        Logger::log_err(
            "expecting a config file, an input model, an input material file, "
            "and an output filename");
        return EXIT_FAILURE;
    }

    string config_file = argv[1];
    string model_file = argv[2];
    string material_file = argv[3];
    string output_file = argv[4];

    auto output_sr = 44100;
    auto bit_depth = 16;

    unsigned long format, depth;

    try {
        format = get_file_format(output_file);
        depth = get_file_depth(bit_depth);
    } catch (const runtime_error & e) {
        Logger::log_err("critical runtime error: ", e.what());
        return EXIT_FAILURE;
    }

    //  global params
    auto speed_of_sound = 340.0;

    auto max_freq = 1000;
    auto filter_freq = max_freq * 0.5;
    auto sr = max_freq * 4;
    auto divisions = (speed_of_sound * sqrt(3)) / sr;

    auto context = get_context();
    auto device = get_device(context);
    cl::CommandQueue queue(context, device);

    auto num_rays = 1024 * 32;
    auto num_impulses = 64;
    auto ray_hipass = 45.0;
    auto do_normalize = true;
    auto trim_predelay = false;
    auto trim_tail = false;
    auto remove_direct = false;
    auto volume_scale = 1.0;

    auto directions = getRandomDirections(num_rays);
    cl_float3 source{{0, 2, 0}};
    cl_float3 mic{{0, 2, 5}};

    Document document;
    attemptJsonParse (config_file, document);

    if (document.HasParseError())
    {
        cerr << "Encountered error while parsing config file:" << endl;
        cerr << GetParseError_En (document.GetParseError()) << endl;
        exit (1);
    }

    if (! document.IsObject())
    {
        cerr << "Rayverb config must be stored in a JSON object" << endl;
        exit (1);
    }


    ConfigValidator cv;

    cv.addRequiredValidator("rays", num_rays);
    cv.addRequiredValidator("reflections", num_impulses);
    cv.addRequiredValidator("sample_rate", output_sr);
    cv.addRequiredValidator("bit_depth", bit_depth);
    cv.addRequiredValidator("source_position", source);
    cv.addRequiredValidator("mic_position", mic);

    cv.addOptionalValidator("hipass", ray_hipass);
    cv.addOptionalValidator("normalize", do_normalize);
    cv.addOptionalValidator("volumme_scale", volume_scale);
    cv.addOptionalValidator("trim_predelay", trim_predelay);
    cv.addOptionalValidator("remove_direct", remove_direct);
    cv.addOptionalValidator("trim_tail", trim_tail);

    try {
        cv.run(document);
    } catch (...) {
        Logger::log_err("error reading config file");
        return EXIT_FAILURE;
    }

    try {
        SceneData scene_data(model_file, material_file);

        auto boundary = get_mesh_boundary(scene_data);
        auto waveguide_program =
            get_program<TetrahedralProgram>(context, device);
        IterativeTetrahedralWaveguide waveguide(
            waveguide_program, queue, boundary, divisions);
        auto mic_index = waveguide.get_index_for_coordinate(convert(mic));
        auto source_index = waveguide.get_index_for_coordinate(convert(source));

        auto corrected_mic = waveguide.get_coordinate_for_index(mic_index);
        auto corrected_source =
            waveguide.get_coordinate_for_index(source_index);

        auto raytrace_program = get_program<RayverbProgram>(context, device);
        Raytrace raytrace(raytrace_program, queue, num_impulses, scene_data);
        raytrace.raytrace(
            convert(corrected_mic), convert(corrected_source), directions);
        auto results = raytrace.getAllRaw(false);
        vector<Speaker> speakers{Speaker{cl_float3{{0, 0, 0}}, 0}};
        auto attenuated =
            Attenuate(raytrace_program, queue).attenuate(results, speakers);

        //  TODO ensure outputs are properly aligned
        //  fixPredelay(attenuated);

        auto flattened = flattenImpulses(attenuated, output_sr);
        auto raytrace_results = process(FILTER_TYPE_BIQUAD_ONEPASS,
                                        flattened,
                                        output_sr,
                                        true,
                                        ray_hipass,
                                        true,
                                        1.0);
        normalize(raytrace_results);

        write_sndfile(output_file + ".raytrace.full.wav",
                      raytrace_results,
                      output_sr,
                      depth,
                      format);

        LinkwitzRiley hipass;
        hipass.setParams(filter_freq, output_sr * 0.45, output_sr);
        for (auto & i : raytrace_results)
            hipass.filter(i);
        normalize(raytrace_results);

        write_sndfile(output_file + ".raytrace.hipass.wav",
                      raytrace_results,
                      output_sr,
                      depth,
                      format);

        auto decay_frames = rt60(raytrace_results.front());
        auto attenuation_factor = pow(db2a(-60), 1.0 / decay_frames);
        attenuation_factor = sqrt(attenuation_factor);
        Logger::log("attenuation factor: ", attenuation_factor);

#ifdef TESTING
        auto steps = 1 << 8;
#else
        auto steps = 1 << 13;
#endif

        auto w_results =
            waveguide.run_basic(corrected_source, mic_index, steps);

        normalize(w_results);

        vector<float> out_signal(output_sr * w_results.size() / sr);

        SRC_DATA sample_rate_info{w_results.data(),
                                  out_signal.data(),
                                  long(w_results.size()),
                                  long(out_signal.size()),
                                  0,
                                  0,
                                  0,
                                  output_sr / double(sr)};

        src_simple(&sample_rate_info, SRC_SINC_BEST_QUALITY, 1);

        auto envelope =
            exponential_decay_envelope(out_signal.size(), attenuation_factor);
        elementwise_multiply(out_signal, envelope);

        write_sndfile(output_file + ".waveguide.full.wav",
                      {out_signal},
                      output_sr,
                      depth,
                      format);

        LinkwitzRiley lopass;
        lopass.setParams(1, filter_freq, output_sr);
        lopass.filter(out_signal);

        normalize(out_signal);

        vector<vector<float>> waveguide_results = {out_signal};

        write_sndfile(output_file + ".waveguide.lopass.wav",
                      waveguide_results,
                      output_sr,
                      depth,
                      format);

        auto raytrace_amp = 0.95;
        auto waveguide_amp = 0.05;

        auto max_index = max(raytrace_results.front().size(),
                             waveguide_results.front().size());
        vector<float> summed_results(max_index, 0);
        for (auto i = 0u; i != max_index; ++i) {
            if (i < raytrace_results.front().size())
                summed_results[i] += raytrace_amp * raytrace_results.front()[i];
            if (i < waveguide_results.front().size())
                summed_results[i] +=
                    waveguide_amp * waveguide_results.front()[i];
        }
        normalize(summed_results);
        write_sndfile(output_file + ".summed.wav",
                      {summed_results},
                      output_sr,
                      depth,
                      format);

    } catch (const cl::Error & e) {
        Logger::log_err("critical cl error: ", e.what());
        return EXIT_FAILURE;
    } catch (const runtime_error & e) {
        Logger::log_err("critical runtime error: ", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        Logger::log_err("unknown error");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
