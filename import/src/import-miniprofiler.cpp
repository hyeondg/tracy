#include <algorithm>
#include <optional>
#include <cstdint>
#include <format>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unordered_map>
#include <nlohmann/json.hpp>

#include "../../server/TracyFileWrite.hpp"
#include "../../server/TracyMmap.hpp"
#include "../../server/TracyWorker.hpp"


void Usage()
{
    printf( "Usage: import-miniprofiler input.json output.tracy\n\n" );
    exit( 1 );
}


int main(int argc, char** argv)
{
    using json = nlohmann::json;
    if (argc != 3) Usage();

    const char* input = argv[1];
    const char* output = argv[2];

    printf( "Loading...\r" );
    fflush( stdout );

    json j;

    std::ifstream is(input);
    if (!is.is_open())
    {
        fprintf( stderr, "Cannot open input file!\n" );
        exit( 1 );
    }
    is >> j;
    is.close();

    std::vector<tracy::Worker::ImportEventTimeline> timeline;
    std::unordered_map<uint64_t, std::string> threadNames;

    for (auto& v : j) {
        const auto thread_id = v["thread_id"].get<uint64_t>();

        if (!v["thread_name"].is_null()) {
            threadNames[thread_id] = v["thread_name"].get<std::string>();
        }

        const auto timings = v["timings"];
        for (auto& timing : timings) {
            const auto location = timing["location"];

            const auto file = location["file"].get<std::string>();
            const auto line = location["line"].get<uint32_t>();
            const auto column = location["column"].get<uint32_t>();

            const auto name = std::format("{}:{}:{}", file, line, column);

            const auto start = timing["start"].get<uint64_t>();
            const auto duration = timing["duration"].get<uint64_t>();

            timeline.emplace_back(tracy::Worker::ImportEventTimeline {
               thread_id,
               start,
               name,
               name,
               false,
               file,
               line
            });

            timeline.emplace_back(tracy::Worker::ImportEventTimeline {
                thread_id,
                start + duration,
                "",
                "",
                true,
            });
        }
    }

    std::stable_sort(timeline.begin(), timeline.end(), [](const auto& l, const auto& r) { return l.timestamp < r.timestamp; });

    uint64_t mts = 0;
    if (!timeline.empty()) {
        mts = timeline[0].timestamp;
    }

    for (auto& v : timeline) v.timestamp -= mts;

    printf( "\33[2KProcessing...\r" );
    fflush( stdout );

    auto&& getFilename = [](const char* in) {
        auto out = in;
        while (*out) ++out;
        --out;
        while (out > in && (*out != '/' || *out != '\\')) out--;
        return out;
    };

    tracy::Worker worker( getFilename(output), getFilename(input), timeline, {}, {}, threadNames );

    tracy::FileCompression clev = tracy::FileCompression::Fast;

    auto w = std::unique_ptr<tracy::FileWrite>( tracy::FileWrite::Open( output, clev ) );
    if( !w )
    {
        fprintf( stderr, "Cannot open output file!\n" );
        exit( 1 );
    }
    printf( "\33[2KSaving...\r" );
    fflush( stdout );
    worker.Write( *w, false );

    printf( "\33[2KCleanup...\n" );
    fflush( stdout );

    return 0;
}
