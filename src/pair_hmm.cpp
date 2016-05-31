//
//  pair_hmm.cpp
//  pair_hmm
//
//  Created by Daniel Cooke on 14/12/2015.
//  Copyright © 2015 Oxford University. All rights reserved.
//

#include "pair_hmm.hpp"

#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <cmath>
#include <functional>
#include <type_traits>
#include <limits>
#include <array>
#include <cassert>

#include <boost/thread/tss.hpp>

#include "align.hpp"
#include "banded_simd_viterbi.hpp"

#include <iostream> // DEBUG

namespace PairHMM
{
static constexpr double ln_10_div_10 {0.230258509299404568401799145468436420760110148862877297603};

template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
constexpr auto num_values() noexcept
{
    return std::numeric_limits<T>::max() - std::numeric_limits<T>::min() + 1;
}

template <std::size_t N>
constexpr auto make_phred_to_ln_prob_lookup() noexcept
{
    std::array<double, N> result {};
    
    for (std::size_t i {0}; i < N; ++i) {
        // This const_cast mess is because std::array::operator[] is not marked constexpr (until C++17)
        const_cast<double&>(static_cast<std::array<double, N> const&>(result)[i]) = -ln_10_div_10 * i;
    }
    
    return result;
}

template <typename T>
constexpr auto make_phred_to_ln_prob_lookup() noexcept
{
    return make_phred_to_ln_prob_lookup<num_values<T>()>();
}

bool is_target_in_truth_flank(const std::string& truth, const std::string& target,
                              const std::size_t target_offset, const Model& model)
{
    return target_offset < model.lhs_flank_size
            || (target_offset + target.size()) > (truth.size() - model.rhs_flank_size);
}
    
namespace debug
{
    void print_alignment(const std::vector<char>& align1, const std::vector<char>& align2)
    {
        const auto isnt_null = [] (const char c) { return c != '\0'; };
        std::copy_if(std::cbegin(align1), std::cend(align1), std::ostreambuf_iterator<char>(std::cout),
                     isnt_null);
        std::cout << '\n';
        std::copy_if(std::cbegin(align2), std::cend(align2), std::ostreambuf_iterator<char>(std::cout),
                     isnt_null);
        std::cout << '\n';
    }
} // namespace debug

auto align(const std::string& truth, const std::string& target,
           const std::vector<std::uint8_t>& target_qualities,
           const std::vector<std::int8_t>& truth_gap_open_penalties,
           const std::size_t target_offset, const Model& model)
{
    const auto truth_alignment_size = static_cast<int>(target.size() + 15);
    
    const auto alignement_offset = static_cast<std::size_t>(std::max(0, static_cast<int>(target_offset) - 8));
    
    if (alignement_offset + truth_alignment_size > truth.size()) {
        return std::numeric_limits<double>::lowest();
    }
    
    if (!is_target_in_truth_flank(truth, target, target_offset, model)) {
        const auto score = fastAlignmentRoutine(truth.data() + alignement_offset, target.data(),
                                                reinterpret_cast<const std::int8_t*>(target_qualities.data()),
                                                truth_alignment_size,
                                                static_cast<int>(target.size()),
                                                static_cast<int>(model.gapextend),
                                                static_cast<int>(model.nucprior),
                                                truth_gap_open_penalties.data() + alignement_offset);
        
        return -ln_10_div_10 * static_cast<double>(score);
    }
    
    // Ideally we'd just use thread_local but doesn't seem to work on Clang yet.
    // Keep an eye on https://forums.developer.apple.com/thread/17437
    static boost::thread_specific_ptr<std::vector<char>> align1, align2;
    if (!align1.get()) {
        align1.reset(new std::vector<char>(1024));
        align2.reset(new std::vector<char>(1024));
    }
    
    const auto max_alignment_size = 2 * (target.size() + 8);
    
    if (align1->size() < max_alignment_size) {
        align1->resize(max_alignment_size, '\0');
        align2->resize(max_alignment_size, '\0');
    } else {
        std::fill_n(std::begin(*align1), max_alignment_size, '\0');
        std::fill_n(std::begin(*align2), max_alignment_size, '\0');
    }
    
    int first_pos;
    
    const auto score = fastAlignmentRoutine(truth.data() + alignement_offset, target.data(),
                                            reinterpret_cast<const std::int8_t*>(target_qualities.data()),
                                            truth_alignment_size,
                                            static_cast<int>(target.size()),
                                            static_cast<int>(model.gapextend),
                                            static_cast<int>(model.nucprior),
                                            truth_gap_open_penalties.data() + alignement_offset,
                                            align1->data(), align2->data(), &first_pos);
    
    //debug::print_alignment(*align1, *align2);
    
    const auto truth_size = static_cast<int>(truth.size());
    
    auto lhs_flank_size = static_cast<int>(model.lhs_flank_size);
    
    if (lhs_flank_size < alignement_offset) {
        lhs_flank_size = 0;
    } else {
        lhs_flank_size -= alignement_offset;
    }
    
    auto rhs_flank_size = static_cast<int>(model.rhs_flank_size);
    
    if (alignement_offset + truth_alignment_size < (truth.size() - model.rhs_flank_size)) {
        rhs_flank_size = 0;
    } else {
        rhs_flank_size += alignement_offset + truth_alignment_size;
        rhs_flank_size -= truth_size;
    }
    
    assert(lhs_flank_size >= 0);
    assert(rhs_flank_size >= 0);
    
    const auto flank_score = calculateFlankScore(truth_size, lhs_flank_size, rhs_flank_size,
                                                 reinterpret_cast<const std::int8_t*>(target_qualities.data()),
                                                 truth_gap_open_penalties.data(),
                                                 static_cast<int>(model.gapextend),
                                                 static_cast<int>(model.nucprior),
                                                 static_cast<int>(first_pos + alignement_offset),
                                                 align1->data(), align2->data());
    
    return -ln_10_div_10 * static_cast<double>(score - flank_score);
}
    
//auto align(const std::string& truth, const std::string& target,
//           const std::vector<std::uint8_t>& target_qualities,
//           const std::vector<std::int8_t>& truth_gap_open_penalties,
//           const std::size_t target_offset, const std::size_t truth_offset,
//           const Model& model)
//{
//    const auto truth_alignment_size = static_cast<int>(target.size() + 15 - truth_offset);
//    
//    const auto alignement_offset = static_cast<std::size_t>(std::max(0, static_cast<int>(target_offset + truth_offset - 8)));
//    
//    if (!is_target_in_truth_flank(truth, target, alignement_offset, model)) {
//        const auto score = fastAlignmentRoutine(truth.data() + alignement_offset,
//                                                target.data() + truth_offset,
//                                                reinterpret_cast<const std::int8_t*>(target_qualities.data() + truth_offset),
//                                                truth_alignment_size,
//                                                static_cast<int>(target.size() - truth_offset),
//                                                static_cast<int>(model.gapextend),
//                                                static_cast<int>(model.nucprior),
//                                                truth_gap_open_penalties.data() + alignement_offset);
//        
//        return -ln_10_div_10 * static_cast<double>(score);
//    }
//    
//    int first_pos;
//    std::vector<char> align1(2 * (target.size() + 8) - truth_offset), align2(2 * (target.size() + 8) - truth_offset);
//    
//    const auto score = fastAlignmentRoutine(truth.data() + alignement_offset,
//                                            target.data() + truth_offset,
//                                            reinterpret_cast<const std::int8_t*>(target_qualities.data() + truth_offset),
//                                            truth_alignment_size,
//                                            static_cast<int>(target.size() - truth_offset),
//                                            static_cast<int>(model.gapextend),
//                                            static_cast<int>(model.nucprior),
//                                            truth_gap_open_penalties.data() + alignement_offset,
//                                            align1.data(), align2.data(), &first_pos);
//    
//    const auto truth_size     = static_cast<int>(truth.size() - truth_offset);
//    const auto lhs_flank_size = 0;
//    const auto rhs_flank_size = static_cast<int>(model.rhs_flank_size);
//    
//    const auto flank_score = calculateFlankScore(truth_size, lhs_flank_size, rhs_flank_size,
//                                                 reinterpret_cast<const std::int8_t*>(target_qualities.data() + truth_offset),
//                                                 truth_gap_open_penalties.data() + truth_offset,
//                                                 static_cast<int>(model.gapextend),
//                                                 static_cast<int>(model.nucprior),
//                                                 static_cast<int>(first_pos + alignement_offset),
//                                                 align1.data(), align2.data());
//    
//    return -ln_10_div_10 * static_cast<double>(score - flank_score);
//}

double align_around_offset(const std::string& truth, const std::string& target,
                           const std::vector<std::uint8_t>& target_qualities,
                           const std::vector<std::int8_t>& truth_gap_open_penalties,
                           const std::size_t target_offset, const Model& model)
{
    using std::cbegin; using std::cend; using std::next;
    
    static constexpr auto Ln_probability = make_phred_to_ln_prob_lookup<std::uint8_t>();
    
    assert(target.size() == target_qualities.size());
    assert(truth.size() == truth_gap_open_penalties.size());
    assert(std::max(truth.size(), target.size()) > target_offset);
    
    if (target_offset + target.size() > truth.size()) {
        return std::numeric_limits<double>::lowest();
    }
    
    const auto offsetted_truth_begin_itr = next(cbegin(truth), target_offset);
    
    const auto m1 = std::mismatch(cbegin(target), cend(target), offsetted_truth_begin_itr);
    
    if (m1.first == cend(target)) {
        return 0; // Sequences are equal, can't do better than this
    }
    
    const auto m2 = std::mismatch(next(m1.first), cend(target), next(m1.second));
    
    if (m2.first == cend(target)) {
        // Then there is only a single base difference between the sequences
        const auto mismatch_index = std::distance(offsetted_truth_begin_itr, m1.second);
        
        if (target_qualities[mismatch_index] <= truth_gap_open_penalties[mismatch_index]
            || !std::equal(next(m1.first), cend(target), m1.second)) {
            return Ln_probability[target_qualities[mismatch_index]];
        }
        
        return Ln_probability[truth_gap_open_penalties[mismatch_index]];
    }
    
    // TODO: we should be able to optimise the alignement based of the first mismatch postition
    
    return align(truth, target, target_qualities, truth_gap_open_penalties, target_offset, model);
}
    
} // namespace PairHMM
