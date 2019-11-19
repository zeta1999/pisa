#include <iostream>
#include <optional>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include "CLI/CLI.hpp"
#include "binary_freq_collection.hpp"
#include "mappable/mapper.hpp"
#include "mio/mmap.hpp"
#include "scorer/scorer.hpp"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#include "taily.hpp"
#include "wand_data.hpp"
#include "wand_data_raw.hpp"

using namespace pisa;
using namespace boost::accumulators;

int main(int argc, const char **argv)
{
    spdlog::drop("");
    spdlog::set_default_logger(spdlog::stderr_color_mt(""));

    std::string input_basename;
    std::string scorer_name;
    std::string wand_data_filename;

    std::string output_filename;

    CLI::App app{"A tool for extracting Taily statistics on an index."};
    app.add_option("-c,--collection", input_basename, "Collection basename")->required();
    app.add_option("-w,--wand", wand_data_filename, "Wand data filename")->required();
    app.add_option("-s,--scorer", scorer_name, "Scorer function")->required();
    app.add_option("-o,--output", output_filename, "Output filename")->required();
    CLI11_PARSE(app, argc, argv);

    mio::mmap_source md;
    std::error_code error;
    md.map(wand_data_filename, error);
    if (error) {
        spdlog::error("error mapping file: {}, exiting...", error.message());
        std::abort();
    }
    using wand_raw_index = wand_data<wand_data_raw>;
    wand_raw_index wdata;
    mapper::map(wdata, md, mapper::map_flags::warmup);

    auto scorer = scorer::from_name(scorer_name, wdata);

    binary_freq_collection coll(input_basename.c_str());

    size_t collection_size = coll.num_docs();
    std::vector<taily::Feature_Statistics> term_stats;
    {
        pisa::progress progress("Processing posting lists", coll.size());
        size_t term_id = 0;
        for (auto const &seq : coll) {
            std::vector<float> scores;
            int64_t size = seq.docs.size();
            scores.reserve(size);
            auto term_scorer = scorer->term_scorer(term_id);
            for (size_t i = 0; i < seq.docs.size(); ++i) {
                uint64_t docid = *(seq.docs.begin() + i);
                uint64_t freq = *(seq.freqs.begin() + i);
                float score = term_scorer(docid, freq);
                scores.push_back(score);
            }
            accumulator_set<float, stats<tag::mean, tag::variance>> acc;
            for_each(scores.begin(),
                     scores.end(),
                     std::bind<void>(std::ref(acc), std::placeholders::_1));
            double expected_value = mean(acc);
            constexpr float epsilon_score = 1.0E-6;
            double var = std::max(variance(acc), epsilon_score);
            term_stats.push_back(taily::Feature_Statistics{expected_value, var, size});
            term_id += 1;
            progress.update(1);
        }
    }

    std::ofstream ofs(output_filename);
    ofs.write(reinterpret_cast<const char *>(&collection_size), sizeof(collection_size));
    size_t num_terms = term_stats.size();
    ofs.write(reinterpret_cast<const char *>(&num_terms), sizeof(num_terms));
    for (auto &&ts : term_stats) {
        ts.to_stream(ofs);
    }
}