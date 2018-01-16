// Copyright (c) 2017 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "haplotype_likelihood_model.hpp"

#include <utility>
#include <cmath>
#include <limits>
#include <cassert>

#include "mutation/error_model_factory.hpp"
#include "concepts/mappable.hpp"
#include "basics/aligned_read.hpp"
#include "utils/maths.hpp"

namespace octopus {

HaplotypeLikelihoodModel::ShortHaplotypeError::ShortHaplotypeError(const Haplotype& haplotype,
                                                                   Length required_extension)
: std::runtime_error {"Haplotype is too short for alignment"}
, haplotype_ {haplotype}
, required_extension_ {required_extension}
{}

const Haplotype& HaplotypeLikelihoodModel::ShortHaplotypeError::haplotype() const noexcept
{
    return haplotype_;
}

HaplotypeLikelihoodModel::ShortHaplotypeError::Length
HaplotypeLikelihoodModel::ShortHaplotypeError::required_extension() const noexcept
{
    return required_extension_;
}

// public methods

unsigned HaplotypeLikelihoodModel::pad_requirement() noexcept
{
    return hmm::min_flank_pad();
}

void HaplotypeLikelihoodModel::reset(const Haplotype& haplotype, boost::optional<FlankState> flank_state)
{
    haplotype_ = std::addressof(haplotype);
    haplotype_flank_state_ = std::move(flank_state);
    if (snv_error_model_) {
        snv_error_model_->evaluate(haplotype,
                                   haplotype_snv_forward_mask_, haplotype_snv_forward_priors_,
                                   haplotype_snv_reverse_mask_, haplotype_snv_reverse_priors_);
    } else {
        // TODO: refactor HaplotypeLikelihoodModel to use another HMM evaluate overload without SNV model
        haplotype_snv_forward_priors_.assign(sequence_size(haplotype), 100);
        haplotype_snv_forward_mask_.assign(std::cbegin(haplotype.sequence()), std::cend(haplotype.sequence()));
        haplotype_snv_reverse_priors_.assign(sequence_size(haplotype), 100);
        haplotype_snv_reverse_mask_.assign(std::cbegin(haplotype.sequence()), std::cend(haplotype.sequence()));
    }
    if (indel_error_model_) {
        haplotype_gap_extension_penalty_ = indel_error_model_->evaluate(haplotype, haplotype_gap_open_penalities_);
    }
}

void HaplotypeLikelihoodModel::clear() noexcept
{
    haplotype_ = nullptr;
    haplotype_flank_state_ = boost::none;
}

HaplotypeLikelihoodModel::HaplotypeLikelihoodModel()
: HaplotypeLikelihoodModel {make_snv_error_model(), make_indel_error_model()}
{}

HaplotypeLikelihoodModel::HaplotypeLikelihoodModel(bool use_mapping_quality)
: HaplotypeLikelihoodModel {make_snv_error_model(), make_indel_error_model(), use_mapping_quality}
{}

HaplotypeLikelihoodModel::HaplotypeLikelihoodModel(std::unique_ptr<SnvErrorModel> snv_model,
                                                   std::unique_ptr<IndelErrorModel> indel_model,
                                                   bool use_mapping_quality)
: snv_error_model_ {std::move(snv_model)}
, indel_error_model_ {std::move(indel_model)}
, haplotype_ {nullptr}
, haplotype_flank_state_ {}
, haplotype_gap_open_penalities_ {}
, haplotype_gap_extension_penalty_ {}
, use_mapping_quality_ {use_mapping_quality}
{}

HaplotypeLikelihoodModel::HaplotypeLikelihoodModel(std::unique_ptr<SnvErrorModel> snv_model,
                                                   std::unique_ptr<IndelErrorModel> indel_model,
                                                   const Haplotype& haplotype,
                                                   boost::optional<FlankState> flank_state,
                                                   bool use_mapping_quality)
: HaplotypeLikelihoodModel {std::move(snv_model), std::move(indel_model), use_mapping_quality}
{
    this->reset(haplotype, std::move(flank_state));
}

double HaplotypeLikelihoodModel::evaluate(const AlignedRead& read) const
{
    const static MappingPositionVector empty {};
    return this->evaluate(read, std::cbegin(empty), std::cend(empty));
}

double HaplotypeLikelihoodModel::evaluate(const AlignedRead& read,
                                          const MappingPositionVector& mapping_positions) const
{
    return this->evaluate(read, std::cbegin(mapping_positions), std::cend(mapping_positions));
}

namespace {

int num_out_of_range_bases(const std::size_t mapping_position, const AlignedRead& read, const Haplotype& haplotype) noexcept
{
    if (mapping_position < hmm::min_flank_pad()) {
        return hmm::min_flank_pad() - mapping_position;
    }
    const auto mapping_end = mapping_position + sequence_size(read) + hmm::min_flank_pad();
    if (mapping_end > sequence_size(haplotype)) {
        return static_cast<int>(sequence_size(haplotype)) - static_cast<int>(mapping_end);
    } else {
        return 0;
    }
}

bool is_in_range(const std::size_t mapping_position, const AlignedRead& read, const Haplotype& haplotype) noexcept
{
    return num_out_of_range_bases(mapping_position, read, haplotype) == 0;
}

} // namespace

template <typename InputIt>
double max_score(const AlignedRead& read, const Haplotype& haplotype,
                 InputIt first_mapping_position, InputIt last_mapping_position,
                 const hmm::MutationModel& model)
{
    assert(contains(haplotype, read));
    using PositionType = typename std::iterator_traits<InputIt>::value_type;
    const auto original_mapping_position = static_cast<PositionType>(begin_distance(haplotype, read));
    auto max_log_probability = std::numeric_limits<double>::lowest();
    bool is_original_position_mapped {false}, has_in_range_mapping_position {false};
    std::for_each(first_mapping_position, last_mapping_position, [&] (const auto position) {
        if (position == original_mapping_position) {
            is_original_position_mapped = true;
        }
        if (is_in_range(position, read, haplotype)) {
            has_in_range_mapping_position = true;
            auto p = hmm::evaluate(read.sequence(), haplotype.sequence(), read.base_qualities(), position, model);
            max_log_probability = std::max(p, max_log_probability);
        }
    });
    if (!is_original_position_mapped && is_in_range(original_mapping_position, read, haplotype)) {
        has_in_range_mapping_position = true;
        auto p = hmm::evaluate(read.sequence(), haplotype.sequence(), read.base_qualities(),
                               original_mapping_position, model);
        max_log_probability = std::max(p, max_log_probability);
    }
    if (!has_in_range_mapping_position) {
        const auto min_shift = num_out_of_range_bases(original_mapping_position, read, haplotype);
        auto final_mapping_position = original_mapping_position;
        if (min_shift > 0) {
            final_mapping_position += min_shift;
            if (!is_in_range(final_mapping_position, read, haplotype)) {
                throw HaplotypeLikelihoodModel::ShortHaplotypeError {haplotype, static_cast<unsigned>(min_shift)};
            }
        } else {
            const auto min_left_shift = static_cast<unsigned>(min_shift);
            if (original_mapping_position >= min_left_shift) {
                final_mapping_position -= min_left_shift;
            } else {
                auto required_extension = min_left_shift - original_mapping_position;
                throw HaplotypeLikelihoodModel::ShortHaplotypeError {haplotype, required_extension};
            }
        }
        max_log_probability = hmm::evaluate(read.sequence(), haplotype.sequence(), read.base_qualities(),
                                            final_mapping_position, model);
    }
    assert(max_log_probability > std::numeric_limits<double>::lowest() && max_log_probability <= 0);
    return max_log_probability;
}

double HaplotypeLikelihoodModel::evaluate(const AlignedRead& read,
                                          MappingPositionItr first_mapping_position,
                                          MappingPositionItr last_mapping_position) const
{
    if (haplotype_ == nullptr) {
        throw std::runtime_error {"HaplotypeLikelihoodModel: no buffered Haplotype"};
    }
    const auto is_forward = !read.is_marked_reverse_mapped();
    hmm::MutationModel model {
        is_forward ? haplotype_snv_forward_mask_ : haplotype_snv_reverse_mask_,
        is_forward ? haplotype_snv_forward_priors_ : haplotype_snv_reverse_priors_,
        haplotype_gap_open_penalities_,
        haplotype_gap_extension_penalty_
    };
    if (haplotype_flank_state_) {
        model.lhs_flank_size = haplotype_flank_state_->lhs_flank;
        model.rhs_flank_size = haplotype_flank_state_->rhs_flank;
    } else {
        model.lhs_flank_size = 0;
        model.rhs_flank_size = 0;
    }
    const auto ln_prob_given_mapped = max_score(read, *haplotype_, first_mapping_position, last_mapping_position, model);
    if (use_mapping_quality_) {
        // This calculation is approximately
        // p(read | hap) = p(read missmapped) p(read | hap, missmapped)
        //                  + p(read correctly mapped) p(read | hap, correctly mapped)
        // = p(read correctly mapped) p(read | hap, correctly mapped)
        //      + p(read missmapped)
        // assuming p(read | hap, missmapped) = 1
        using octopus::maths::constants::ln10Div10;
        const auto ln_prob_missmapped = -ln10Div10<> * read.mapping_quality();
        const auto ln_prob_mapped = std::log(1.0 - std::exp(ln_prob_missmapped));
        const auto result = maths::log_sum_exp(ln_prob_mapped + ln_prob_given_mapped, ln_prob_missmapped);
        return result > -1e-15 ? 0.0 : result;
    } else {
        return ln_prob_given_mapped  > -1e-15 ? 0.0 : ln_prob_given_mapped;
    }
}

HaplotypeLikelihoodModel::Alignment
HaplotypeLikelihoodModel::align(const AlignedRead& read) const
{
    const static MappingPositionVector empty {};
    return this->align(read, std::cbegin(empty), std::cend(empty));
}

HaplotypeLikelihoodModel::Alignment
HaplotypeLikelihoodModel::align(const AlignedRead& read, const MappingPositionVector& mapping_positions) const
{
    return this->align(read, std::cbegin(mapping_positions), std::cend(mapping_positions));
}

template <typename InputIt>
HaplotypeLikelihoodModel::Alignment
compute_optimal_alignment(const AlignedRead& read, const Haplotype& haplotype,
                          InputIt first_mapping_position, InputIt last_mapping_position,
                          const hmm::MutationModel& model)
{
    assert(contains(haplotype, read));
    using PositionType = typename std::iterator_traits<InputIt>::value_type;
    const auto original_mapping_position = static_cast<PositionType>(begin_distance(haplotype, read));
    HaplotypeLikelihoodModel::Alignment result {};
    result.likelihood = std::numeric_limits<double>::lowest();
    bool is_original_position_mapped {false}, has_in_range_mapping_position {false};
    std::for_each(first_mapping_position, last_mapping_position, [&] (const auto position) {
        if (position == original_mapping_position) {
            is_original_position_mapped = true;
        }
        if (is_in_range(position, read, haplotype)) {
            has_in_range_mapping_position = true;
            auto p = hmm::align(read.sequence(), haplotype.sequence(), read.base_qualities(), position, model);
            if (p.second > result.likelihood) {
                result.mapping_position = position;
                result.likelihood = p.second;
                result.cigar = std::move(p.first);
            }
        }
    });
    if (!is_original_position_mapped && is_in_range(original_mapping_position, read, haplotype)) {
        has_in_range_mapping_position = true;
        auto p = hmm::align(read.sequence(), haplotype.sequence(), read.base_qualities(),
                            original_mapping_position, model);
        if (p.second > result.likelihood) {
            result.mapping_position = original_mapping_position;
            result.likelihood = p.second;
            result.cigar = std::move(p.first);
        }
    }
    if (!has_in_range_mapping_position) {
        const auto min_shift = num_out_of_range_bases(original_mapping_position, read, haplotype);
        auto final_mapping_position = original_mapping_position;
        if (min_shift > 0) {
            final_mapping_position += min_shift;
            if (!is_in_range(final_mapping_position, read, haplotype)) {
                throw HaplotypeLikelihoodModel::ShortHaplotypeError {haplotype, static_cast<unsigned>(min_shift)};
            }
        } else {
            const auto min_left_shift = static_cast<unsigned>(min_shift);
            if (original_mapping_position >= min_left_shift) {
                final_mapping_position -= min_left_shift;
            } else {
                auto required_extension = min_left_shift - original_mapping_position;
                throw HaplotypeLikelihoodModel::ShortHaplotypeError {haplotype, required_extension};
            }
        }
        result.mapping_position = final_mapping_position;
        std::tie(result.cigar, result.likelihood) = hmm::align(read.sequence(), haplotype.sequence(), read.base_qualities(),
                                                               final_mapping_position, model);
    }
    assert(result.likelihood > std::numeric_limits<double>::lowest() && result.likelihood <= 0);
    return result;
}

HaplotypeLikelihoodModel::Alignment
HaplotypeLikelihoodModel::align(const AlignedRead& read, MappingPositionItr first_mapping_position, MappingPositionItr last_mapping_position) const
{
    if (haplotype_ == nullptr) {
        throw std::runtime_error {"HaplotypeLikelihoodModel: no buffered Haplotype"};
    }
    const auto is_forward = !read.is_marked_reverse_mapped();
    hmm::MutationModel model {is_forward ? haplotype_snv_forward_mask_ : haplotype_snv_reverse_mask_,
                              is_forward ? haplotype_snv_forward_priors_ : haplotype_snv_reverse_priors_,
                              haplotype_gap_open_penalities_, haplotype_gap_extension_penalty_};
    if (haplotype_flank_state_) {
        model.lhs_flank_size = haplotype_flank_state_->lhs_flank;
        model.rhs_flank_size = haplotype_flank_state_->rhs_flank;
    } else {
        model.lhs_flank_size = 0;
        model.rhs_flank_size = 0;
    }
    auto result = compute_optimal_alignment(read, *haplotype_, first_mapping_position, last_mapping_position, model);
    if (use_mapping_quality_) {
        using octopus::maths::constants::ln10Div10;
        const auto ln_prob_missmapped = -ln10Div10<> * read.mapping_quality();
        const auto ln_prob_mapped = std::log(1.0 - std::exp(ln_prob_missmapped));
        result.likelihood = maths::log_sum_exp(ln_prob_mapped + result.likelihood, ln_prob_missmapped);
        result.likelihood = result.likelihood > -1e-15 ? 0.0 : result.likelihood;
    } else {
        result.likelihood = result.likelihood > -1e-15 ? 0.0 : result.likelihood;
    }
    return result;
}

HaplotypeLikelihoodModel make_haplotype_likelihood_model(const std::string sequencer, bool use_mapping_quality)
{
    return HaplotypeLikelihoodModel {make_snv_error_model(sequencer), make_indel_error_model(sequencer), use_mapping_quality};
}

} // namespace octopus
