#pragma once

#include <vector>

#include "query/queries.hpp"
#include "scorer/bm25.hpp"
#include "scorer/score_function.hpp"
#include "wand_data.hpp"

namespace pisa {

template <typename Index, typename Scorer>
struct scored_cursor {
    using enum_type = typename Index::document_enumerator;
    enum_type docs_enum;
    float q_weight;
    Scorer scorer;
};

template <typename Index, typename WandType, typename Scorer>
[[nodiscard]] auto make_scored_cursors(Index const &index,
                                       WandType const &wdata,
                                       Scorer const &scorer,
                                       Query query)
{
    auto terms = query.terms;
    auto query_term_freqs = query_freqs(terms);
    using term_scorer_type = std::decay_t<decltype(scorer.term_scorer(0))>;

    std::vector<scored_cursor<Index, term_scorer_type>> cursors;
    cursors.reserve(query_term_freqs.size());
    std::transform(query_term_freqs.begin(),
                   query_term_freqs.end(),
                   std::back_inserter(cursors),
                   [&](auto &&term) {
                       auto list = index[term.first];
                       float q_weight = term.second;
                       return scored_cursor<Index, term_scorer_type>{
                           std::move(list), q_weight, scorer.term_scorer(term.first)};
                   });
    return cursors;
}

} // namespace pisa
