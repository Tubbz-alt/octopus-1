// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "trio_caller.hpp"

#include <typeinfo>
#include <functional>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <map>
#include <utility>

#include "basics/genomic_region.hpp"
#include "concepts/mappable.hpp"
#include "core/types/allele.hpp"
#include "core/types/variant.hpp"
#include "containers/probability_matrix.hpp"
#include "utils/germline_variant_call.hpp"
#include "utils/denovo_call.hpp"
#include "utils/reference_call.hpp"
#include "utils/map_utils.hpp"

namespace octopus {

TrioCaller::TrioCaller(Caller::Components&& components,
                       Caller::Parameters general_parameters,
                       Parameters specific_parameters)
: Caller {std::move(components), std::move(general_parameters)}
, parameters_ {std::move(specific_parameters)}
{
    if (parameters_.maternal_ploidy == 0) {
        throw std::logic_error {"IndividualCaller: ploidy must be > 0"};
    }
}

Caller::CallTypeSet TrioCaller::do_get_call_types() const
{
    return {std::type_index(typeid(GermlineVariantCall)),
            std::type_index(typeid(DenovoCall))};
}

namespace {

using model::TrioModel;
using JointProbability = TrioModel::Latents::JointProbability;

using GenotypeReference = std::reference_wrapper<const Genotype<Haplotype>>;

struct GenotypeReferenceLess
{
    bool operator()(GenotypeReference lhs, GenotypeReference rhs) const
    {
        return GenotypeLess{}(lhs.get(), rhs.get());
    }
};
struct GenotypeReferenceEqual
{
    bool operator()(GenotypeReference lhs, GenotypeReference rhs) const
    {
        return lhs.get() == rhs.get();
    }
};

struct GenotypeProbabilityPair
{
    GenotypeReference genotype;
    double probability;
};

template <typename Function>
auto marginalise(std::vector<JointProbability>& joint_posteriors, Function who)
{
    std::sort(std::begin(joint_posteriors), std::end(joint_posteriors),
              [who] (const auto& lhs, const auto& rhs) {
                  return GenotypeReferenceLess{}(who(lhs), who(rhs));
              });
    std::vector<GenotypeProbabilityPair> result {};
    result.reserve(joint_posteriors.size());
    for (auto iter = std::cbegin(joint_posteriors), end = std::cend(joint_posteriors); iter != end;) {
        const auto next = std::find_if_not(std::next(iter), end,
                                           [iter, who] (const auto& p) {
                                               return GenotypeReferenceEqual{}(who(p), who(*iter));
                                           });
        result.push_back({who(*iter),
                          std::accumulate(iter, next, 0.0,
                                          [] (const auto curr, const auto& p) {
                                              return curr + p.probability;
                                          })});
        iter = next;
    }
    return result;
}

auto marginalise_mother(std::vector<JointProbability>& joint_posteriors)
{
    return marginalise(joint_posteriors, [] (const JointProbability& p) -> GenotypeReference { return p.maternal; });
}

auto marginalise_father(std::vector<JointProbability>& joint_posteriors)
{
    return marginalise(joint_posteriors, [] (const JointProbability& p) -> GenotypeReference { return p.paternal; });
}

auto marginalise_child(std::vector<JointProbability>& joint_posteriors)
{
    return marginalise(joint_posteriors, [] (const JointProbability& p) -> GenotypeReference { return p.child; });
}

struct GenotypePairLess
{
    bool operator()(const GenotypeReference lhs, const GenotypeProbabilityPair& rhs) const
    {
        return GenotypeReferenceLess{}(lhs, rhs.genotype);
    }
    bool operator()(const GenotypeProbabilityPair& lhs, const GenotypeReference rhs) const
    {
        return GenotypeReferenceLess{}(lhs.genotype, rhs);
    }
};

// genotypes is required to be sorted
void fill_missing_genotypes(std::vector<GenotypeProbabilityPair>& posteriors,
                            const std::vector<Genotype<Haplotype>>& genotypes)
{
    std::sort(std::begin(posteriors), std::end(posteriors),
              [] (const auto& lhs, const auto& rhs) {
                  return GenotypeReferenceLess{}(lhs.genotype, rhs.genotype);
              });
    std::vector<GenotypeReference> missing {};
    missing.reserve(genotypes.size());
    std::set_difference(std::cbegin(genotypes), std::cend(genotypes),
                        std::cbegin(posteriors), std::cend(posteriors),
                        std::back_inserter(missing), GenotypePairLess {});
    std::transform(std::cbegin(missing), std::cend(missing),
                   std::back_inserter(posteriors),
                   [] (auto genotype) -> GenotypeProbabilityPair {
                       return {genotype, 0.0};
                   });
    posteriors.shrink_to_fit();
}

auto sort_copy(std::vector<Genotype<Haplotype>> genotypes)
{
    std::sort(std::begin(genotypes), std::end(genotypes), GenotypeLess {});
    return genotypes;
}

using JointProbability      = TrioModel::Latents::JointProbability;
using TrioProbabilityVector = std::vector<JointProbability>;

bool contains(const JointProbability& trio, const Haplotype& haplotype)
{
    return contains(trio.maternal.get(), haplotype)
           || contains(trio.paternal.get(), haplotype)
           || contains(trio.child.get(), haplotype);
}

auto compute_posterior(const Haplotype& haplotype,
                       const TrioProbabilityVector& trio_posteriors)
{
    return std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors), 0.0,
                           [&haplotype] (const auto curr, const auto& p) {
                               return curr + (contains(p, haplotype) ? p.probability : 0.0);
                           });
}

auto extract_probabilities(const std::vector<GenotypeProbabilityPair>& ps)
{
    std::vector<double> result(ps.size());
    std::transform(std::cbegin(ps), std::cend(ps), std::begin(result),
                   [] (const auto& p) { return p.probability; });
    return result;
}

} // namespace

TrioCaller::Latents::Latents(const std::vector<Haplotype>& haplotypes,
                             std::vector<Genotype<Haplotype>>&& genotypes,
                             model::TrioModel::InferredLatents&& latents,
                             const Trio& trio)
: maternal {std::move(genotypes)}
, model_latents {std::move(latents)}
{
    auto maternal_posteriors = marginalise_mother(model_latents.posteriors.joint_genotype_probabilities);
    auto paternal_posteriors = marginalise_father(model_latents.posteriors.joint_genotype_probabilities);
    auto child_posteriors = marginalise_child(model_latents.posteriors.joint_genotype_probabilities);
    const auto sorted_genotypes = sort_copy(maternal);
    fill_missing_genotypes(maternal_posteriors, sorted_genotypes);
    fill_missing_genotypes(paternal_posteriors, sorted_genotypes);
    fill_missing_genotypes(child_posteriors, sorted_genotypes);
    
    // TODO: Current GenotypeProbabilityMap only allows one Genotype set, but it should
    // at least support sets of different ploidy. So for now we just generate
    // one set and assume all contigs are autosomes.
    GenotypeProbabilityMap genotype_posteriors {std::begin(maternal), std::end(maternal)};
    insert_sample(trio.child(), extract_probabilities(child_posteriors), genotype_posteriors);
    insert_sample(trio.mother(), extract_probabilities(maternal_posteriors), genotype_posteriors);
    insert_sample(trio.father(), extract_probabilities(paternal_posteriors), genotype_posteriors);
    marginal_genotype_posteriors = std::make_shared<GenotypeProbabilityMap>(std::move(genotype_posteriors));
    
    HaplotypeProbabilityMap haplotype_posteriors {haplotypes.size()};
    for (const auto& haplotype : haplotypes) {
        haplotype_posteriors.emplace(haplotype, compute_posterior(haplotype, model_latents.posteriors.joint_genotype_probabilities));
    }
    marginal_haplotype_posteriors = std::make_shared<HaplotypeProbabilityMap>(haplotype_posteriors);
}

std::shared_ptr<TrioCaller::Latents::HaplotypeProbabilityMap>
TrioCaller::Latents::haplotype_posteriors() const noexcept
{
    return marginal_haplotype_posteriors;
}

std::shared_ptr<TrioCaller::Latents::GenotypeProbabilityMap>
TrioCaller::Latents::genotype_posteriors() const noexcept
{
    return marginal_genotype_posteriors;
}

std::unique_ptr<Caller::Latents>
TrioCaller::infer_latents(const std::vector<Haplotype>& haplotypes,
                          const HaplotypeLikelihoodCache& haplotype_likelihoods) const
{
    const CoalescentModel germline_prior_model {
        Haplotype {mapped_region(haplotypes.front()), reference_},
        parameters_.germline_prior_model_params
    };
    const DeNovoModel denovo_model {parameters_.denovo_model_params};
    const model::TrioModel model {parameters_.trio, germline_prior_model, denovo_model};
    auto genotypes = generate_all_genotypes(haplotypes, parameters_.maternal_ploidy);
    auto latents = model.evaluate(genotypes, genotypes, genotypes, haplotype_likelihoods);
    return std::make_unique<Latents>(haplotypes, std::move(genotypes), std::move(latents), parameters_.trio);
}

boost::optional<double>
TrioCaller::calculate_model_posterior(const std::vector<Haplotype>& haplotypes,
                                      const HaplotypeLikelihoodCache& haplotype_likelihoods,
                                      const Caller::Latents& latents) const
{
    return calculate_model_posterior(haplotypes, haplotype_likelihoods, dynamic_cast<const Latents&>(latents));
}

boost::optional<double>
TrioCaller::calculate_model_posterior(const std::vector<Haplotype>& haplotypes,
                                      const HaplotypeLikelihoodCache& haplotype_likelihoods,
                                      const Latents& latents) const
{
    return boost::none;
}

std::vector<std::unique_ptr<VariantCall>>
TrioCaller::call_variants(const std::vector<Variant>& candidates, const Caller::Latents& latents) const
{
    return call_variants(candidates, dynamic_cast<const Latents&>(latents));
}

namespace {

bool contains(const JointProbability& trio, const Allele& allele)
{
    return contains(trio.maternal, allele)
           || contains(trio.paternal, allele)
           || contains(trio.child, allele);
}

auto compute_posterior(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    auto p = std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                             0.0, [&allele] (const auto curr, const auto& p) {
        return curr + (contains(p, allele) ? 0.0 : p.probability);
    });
    return probability_to_phred(p);
}

using AllelePosteriorMap = std::map<Allele, Phred<double>>;

auto compute_posteriors(const std::vector<Allele>& alleles, const TrioProbabilityVector& trio_posteriors)
{
    AllelePosteriorMap result {};
    for (const auto& allele : alleles) {
        result.emplace(allele, compute_posterior(allele, trio_posteriors));
    }
    return result;
}

struct TrioCall
{
    Genotype<Haplotype> mother, father, child;
};

auto call_trio(const TrioProbabilityVector& trio_posteriors)
{
    auto iter = std::max_element(std::cbegin(trio_posteriors), std::cbegin(trio_posteriors),
                            [] (const auto& lhs, const auto& rhs) {
                                return lhs.probability > rhs.probability;
                            });
    return TrioCall {iter->maternal, iter->paternal, iter->child};
}

bool includes(const TrioCall& trio, const Allele& allele)
{
    return includes(trio.mother, allele)
           || includes(trio.father, allele)
           || includes(trio.child, allele);
}

auto call_alleles(const AllelePosteriorMap& allele_posteriors,
                  const TrioCall& called_trio,
                  const Phred<double> min_posterior)
{
    AllelePosteriorMap result {};
    
    std::copy_if(std::cbegin(allele_posteriors), std::cend(allele_posteriors),
                 std::inserter(result, std::begin(result)),
                 [&called_trio, min_posterior] (const auto& p) {
                     return p.second >= min_posterior && includes(called_trio, p.first);
                 });
    
    return result;
}

bool is_denovo(const Allele& allele, const JointProbability& trio)
{
    return contains(trio.child, allele) && !(contains(trio.maternal, allele) || contains(trio.paternal, allele));
}

auto compute_denovo_posterior(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    auto p = std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                             0.0, [&allele] (const auto curr, const auto& p) {
        return curr + (is_denovo(allele, p) ? 0.0 : p.probability);
    });
    return probability_to_phred(p);
}

auto compute_denovo_posteriors(const AllelePosteriorMap& called_alleles,
                               const TrioProbabilityVector& trio_posteriors)
{
    AllelePosteriorMap result {};
    
    for (const auto& p : called_alleles) {
        result.emplace(p.first, compute_denovo_posterior(p.first, trio_posteriors));
    }
    
    return result;
}

struct CalledDenovo : public Mappable<CalledDenovo>
{
    Allele allele;
    Phred<double> posterior;
    CalledDenovo(Allele allele, Phred<double> posterior)
    : allele {std::move(allele)}
    , posterior {posterior}
    {}
    const GenomicRegion& mapped_region() const noexcept { return allele.mapped_region(); }
};

auto call_denovos(const AllelePosteriorMap& denovo_posteriors,
                  const Genotype<Haplotype>& called_child,
                  const Phred<double> min_posterior)
{
    std::vector<CalledDenovo> result {};
    result.reserve(denovo_posteriors.size());
    
    for (const auto& p : denovo_posteriors) {
        if (p.second >= min_posterior && includes(called_child, p.first)) {
            result.emplace_back(p.first, p.second);
        }
    }
    
    return result;
}

using GenotypeProbabilityMap = ProbabilityMatrix<Genotype<Haplotype>>;

auto compute_posterior(const Genotype<Allele>& genotype,
                       const GenotypeProbabilityMap::InnerMap& posteriors)
{
    auto p = std::accumulate(std::cbegin(posteriors), std::cend(posteriors), 0.0,
                             [&genotype] (const double curr, const auto& p) {
                                 return curr + (contains(p.first, genotype) ? 0.0 : p.second);
                             });
    return probability_to_phred(p);
}

struct GenotypePosterior
{
    Genotype<Allele> genotype;
    Phred<double> posterior;
};

struct GenotypedTrio
{
    GenotypePosterior mother, father, child;
};

auto call_genotypes(const Trio& trio, const TrioCall& called_trio,
                    const GenotypeProbabilityMap& trio_posteriors,
                    const std::vector<GenomicRegion>& regions)
{
    std::vector<GenotypedTrio> result {};
    result.reserve(regions.size());
    
    for (const auto& region : regions) {
        auto mother_genotype = splice<Allele>(called_trio.mother, region);
        auto mother_posterior = compute_posterior(mother_genotype, trio_posteriors[trio.mother()]);
        auto father_genotype = splice<Allele>(called_trio.father, region);
        auto father_posterior = compute_posterior(father_genotype, trio_posteriors[trio.father()]);
        auto child_genotype = splice<Allele>(called_trio.child, region);
        auto child_posterior = compute_posterior(child_genotype, trio_posteriors[trio.child()]);
        result.push_back({{std::move(mother_genotype), mother_posterior},
                          {std::move(father_genotype), father_posterior},
                          {std::move(child_genotype), child_posterior}});
    }
    
    return result;
}

auto make_variant(const CalledDenovo& call, const std::map<Allele, Allele>& reference_alleles)
{
    return Variant {reference_alleles.at(call.allele), call.allele};
}

auto make_genotype_calls(GenotypedTrio call, const Trio& trio)
{
    return std::vector<std::pair<SampleName, Call::GenotypeCall>> {
        {trio.mother(), {call.mother.genotype, call.mother.posterior}},
        {trio.father(), {call.father.genotype, call.father.posterior}},
        {trio.child(), {call.child.genotype, call.child.posterior}}
    };
}

} // namespace

std::vector<std::unique_ptr<VariantCall>>
TrioCaller::call_variants(const std::vector<Variant>& candidates, const Latents& latents) const
{
    const auto alleles = decompose(candidates);
    const auto& trio_posteriors = latents.model_latents.posteriors.joint_genotype_probabilities;
    const auto allele_posteriors = compute_posteriors(alleles, trio_posteriors);
    const auto called_trio = call_trio(trio_posteriors);
    const auto called_alleles = call_alleles(allele_posteriors, called_trio, parameters_.min_variant_posterior);
    const auto denovo_posteriors = compute_denovo_posteriors(called_alleles, trio_posteriors);
    const auto called_denovos = call_denovos(denovo_posteriors, called_trio.child, parameters_.min_variant_posterior);
    const auto denovo_regions = extract_regions(called_denovos);
    auto denovo_genotypes = call_genotypes(parameters_.trio, called_trio,
                                           *latents.genotype_posteriors(),
                                           denovo_regions);
    
//    if (!called_denovos.empty()) {
//        std::cout << "called trio" << std::endl;
//        debug::print_variant_alleles(called_trio.mother); std::cout << '\n';
//        debug::print_variant_alleles(called_trio.father); std::cout << '\n';
//        debug::print_variant_alleles(called_trio.child); std::cout << '\n';
//        std::cout << "De Novo!" << std::endl;
//        for (const auto& p : called_denovos) {
//            std::cout << p.allele << " " << p.posterior << std::endl;
//        }
//        //exit(0);
//    }
    
    std::map<Allele, Allele> reference_alleles {};
    for (const auto& denovo : called_denovos) {
        auto iter = std::find_if(std::cbegin(candidates), std::cend(candidates),
                                 [denovo] (const auto& c) { return is_same_region(c, denovo); });
        reference_alleles.emplace(denovo.allele, iter->ref_allele());
    }
    
    std::vector<std::unique_ptr<VariantCall>> result {};
    result.reserve(called_denovos.size());
    std::transform(std::begin(called_denovos), std::end(called_denovos),
                   std::begin(denovo_genotypes),
                   std::back_inserter(result),
                   [this, &reference_alleles] (auto&& allele, auto&& genotype) {
                       return std::make_unique<DenovoCall>(make_variant(allele, reference_alleles),
                                                           make_genotype_calls(genotype, parameters_.trio),
                                                           allele.posterior);
                   });
    return result;
}

std::vector<std::unique_ptr<ReferenceCall>>
TrioCaller::call_reference(const std::vector<Allele>& alleles, const Caller::Latents& latents,
                           const ReadMap& reads) const
{
    return call_reference(alleles, dynamic_cast<const Latents&>(latents), reads);
}

std::vector<std::unique_ptr<ReferenceCall>>
TrioCaller::call_reference(const std::vector<Allele>& alleles, const Latents& latents,
                            const ReadMap& reads) const
{
    return {};
}

} // namespace octopus
