//
//  haplotype_phaser_tests.cpp
//  Octopus
//
//  Created by Daniel Cooke on 01/05/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "catch.hpp"

#include <iostream>
#include <string>

#include "test_common.h"
#include "reference_genome.h"
#include "mappable_algorithms.h"
#include "reference_genome_factory.h"
#include "test_common.h"
#include "read_manager.h"
#include "read_filter.h"
#include "read_filters.h"
#include "read_utils.h"
#include "allele.h"
#include "variant.h"
#include "candidate_variant_generator.h"
#include "alignment_candidate_variant_generator.h"
#include "haplotype.h"
#include "genotype.h"
#include "read_model.h"
#include "haplotype_prior_model.h"
#include "bayesian_genotype_model.h"
#include "variational_bayes_genotype_model.h"
#include "haplotype_phaser.h"

using std::cout;
using std::endl;

using Octopus::HaplotypeTree;
using Octopus::ReadModel;
using Octopus::HaplotypePhaser;
using Octopus::VariationalBayesGenotypeModel;

TEST_CASE("HaplotypePhaser phases when the data supports one phase", "[haplotype_phaser]")
{
    ReferenceGenomeFactory a_factory {};
    ReferenceGenome human {a_factory.make(human_reference_fasta)};
    
    ReadManager a_read_manager(std::vector<std::string> {human_1000g_bam2});
    
    auto samples = a_read_manager.get_sample_ids();
    
    auto a_region = parse_region("14:53674675-53674780", human);
    
    auto reads = a_read_manager.fetch_reads(samples, a_region);
    
    CandidateVariantGenerator candidate_generator {};
    candidate_generator.register_generator(std::make_unique<AlignmentCandidateVariantGenerator>(human, 10));
    
    for (const auto& sample_reads : reads) {
        candidate_generator.add_reads(sample_reads.second.cbegin(), sample_reads.second.cend());
    }
    
    auto candidates = candidate_generator.get_candidates(a_region);
    
    unsigned ploidy {2};
    ReadModel a_read_model {ploidy};
    VariationalBayesGenotypeModel the_model {a_read_model, ploidy};
    
    unsigned max_haplotypes {128};
    HaplotypePhaser phaser {human, the_model, ploidy, max_haplotypes};
    
    Octopus::HaplotypePhaser::ReadRanges<ReadManager::SampleIdType,
    std::move_iterator<decltype(reads)::mapped_type::iterator>> read_ranges {};
    for (const auto& sample : samples) {
        read_ranges.emplace(sample, std::make_pair(std::make_move_iterator(reads[sample].begin()),
                                                   std::make_move_iterator(reads[sample].end())));
    }
    
//    phaser.put_data(read_ranges, candidates.cbegin(), candidates.cend());
//    auto phased_regions = phaser.get_phased_regions(HaplotypePhaser::SteamingStatus::Finished);
//    
//    REQUIRE(phased_regions.size() == 1);
//    
//    const auto& posterior_counts = phased_regions.front().the_latent_posteriors.haplotype_pseudo_counts;
//    auto sorted_haplotypes = value_sorted_keys(posterior_counts);
//    
//    REQUIRE(is_reference(sorted_haplotypes[0], human));
//    REQUIRE(posterior_counts.at(sorted_haplotypes[1]) > 1);
//    REQUIRE(posterior_counts.at(sorted_haplotypes[2]) > 1);
//    REQUIRE(posterior_counts.at(sorted_haplotypes[3]) < 0.5);
}

TEST_CASE("HaplotypePhaser breaks haplotypes when the data does not support one phase", "[haplotype_phaser]")
{
    ReferenceGenomeFactory a_factory {};
    ReferenceGenome human {a_factory.make(human_reference_fasta)};
    
    ReadManager a_read_manager(std::vector<std::string> {human_1000g_bam1});
    
    auto samples = a_read_manager.get_sample_ids();
    
    auto a_region = parse_region("4:79282976-79283139", human);
    //auto a_region = parse_region("4:79283040-79283139", human);
    
    auto reads = a_read_manager.fetch_reads(samples, a_region);
    
    CandidateVariantGenerator candidate_generator {};
    candidate_generator.register_generator(std::make_unique<AlignmentCandidateVariantGenerator>(human, 10));
    
    for (const auto& sample_reads : reads) {
        candidate_generator.add_reads(sample_reads.second.cbegin(), sample_reads.second.cend());
    }
    
    auto candidates = candidate_generator.get_candidates(a_region);
    
    unsigned ploidy {2};
    ReadModel a_read_model {ploidy};
    VariationalBayesGenotypeModel the_model {a_read_model, ploidy};
    
    unsigned max_haplotypes {128};
    HaplotypePhaser phaser {human, the_model, ploidy, max_haplotypes};
    
//    phaser.put_data(read_ranges, candidates.cbegin(), candidates.cend());
//    auto phased_regions = phaser.get_phased_regions(HaplotypePhaser::SteamingStatus::Finished);
//    
//    auto genotype_posteriors = phased_regions.front().the_latent_posteriors.genotype_probabilities.at(samples.front());
//    
//    auto sorted_genotypes = value_sorted_keys(genotype_posteriors);
//    
//    for (unsigned i = 0; i < 2; ++i) {
//        sorted_genotypes.at(i).at(0).print_explicit_alleles();
//        cout << endl;
//        sorted_genotypes.at(i).at(1).print_explicit_alleles();
//        cout << genotype_posteriors.at(sorted_genotypes.at(i)) << endl;
//    }
}

TEST_CASE("HaplotypePhaser can use data from other samples to help phase uncertain phasing in other samples", "[haplotype_phaser]")
{
    ReferenceGenomeFactory a_factory {};
    ReferenceGenome human {a_factory.make(human_reference_fasta)};
    
    ReadManager a_read_manager(std::vector<std::string> {human_1000g_bam1, human_1000g_bam2});
    
    auto samples = a_read_manager.get_sample_ids();
    
    auto a_region = parse_region("14:53674675-53674780", human);
    
    auto reads = a_read_manager.fetch_reads(samples, a_region);
    
    CandidateVariantGenerator candidate_generator {};
    candidate_generator.register_generator(std::make_unique<AlignmentCandidateVariantGenerator>(human, 10));
    
    for (const auto& sample_reads : reads) {
        candidate_generator.add_reads(sample_reads.second.cbegin(), sample_reads.second.cend());
    }
    
    auto candidates = candidate_generator.get_candidates(a_region);
    
    unsigned ploidy {2};
    ReadModel a_read_model {ploidy};
    VariationalBayesGenotypeModel the_model {a_read_model, ploidy};
    
    unsigned max_haplotypes {128};
    HaplotypePhaser phaser {human, the_model, ploidy, max_haplotypes};
    
    Octopus::HaplotypePhaser::ReadRanges<ReadManager::SampleIdType,
    std::move_iterator<decltype(reads)::mapped_type::iterator>> read_ranges {};
    for (const auto& sample : samples) {
        read_ranges.emplace(sample, std::make_pair(std::make_move_iterator(reads[sample].begin()),
                                                   std::make_move_iterator(reads[sample].end())));
    }
    
//    phaser.put_data(read_ranges, candidates.cbegin(), candidates.cend());
//    auto phased_regions = phaser.get_phased_regions(HaplotypePhaser::SteamingStatus::Finished);
//    
//    REQUIRE(phased_regions.size() == 1);
//    
//    
}

TEST_CASE("can phase", "[haplotype_phaser]")
{
    ReferenceGenomeFactory a_factory {};
    ReferenceGenome human {a_factory.make(human_reference_fasta)};
    
    ReadManager a_read_manager(std::vector<std::string> {human_1000g_bam1});
    
    auto samples = a_read_manager.get_sample_ids();
    
    //auto a_region = parse_region("16:62646900-62647065", human);
    auto a_region = parse_region("6:29909020-29909269", human);
    
    auto reads = a_read_manager.fetch_reads(samples, a_region);
    
    using ReadIterator = std::vector<AlignedRead>::const_iterator;
    ReadFilter<ReadIterator> a_read_filter {};
    a_read_filter.register_filter([] (const AlignedRead& the_read) {
        return is_good_mapping_quality(the_read, 10);
    });
    
    auto good_reads = filter_reads(std::move(reads), a_read_filter).first;
    
    CandidateVariantGenerator candidate_generator {};
    candidate_generator.register_generator(std::make_unique<AlignmentCandidateVariantGenerator>(human, 10));
    
    for (const auto& sample_reads : good_reads) {
        candidate_generator.add_reads(sample_reads.second.cbegin(), sample_reads.second.cend());
    }
    
    auto candidates = candidate_generator.get_candidates(a_region);
    
    unsigned ploidy {2};
    ReadModel a_read_model {ploidy};
    VariationalBayesGenotypeModel the_model {a_read_model, ploidy};
    
    unsigned max_haplotypes {64};
    HaplotypePhaser phaser {human, the_model, ploidy, max_haplotypes};
    
    Octopus::HaplotypePhaser::ReadRanges<ReadManager::SampleIdType,
            std::move_iterator<decltype(good_reads)::mapped_type::iterator>> read_ranges {};
    for (const auto& sample : samples) {
        read_ranges.emplace(sample, std::make_pair(std::make_move_iterator(good_reads[sample].begin()),
                                                   std::make_move_iterator(good_reads[sample].end())));
    }
    
//    phaser.put_data(read_ranges, candidates.cbegin(), candidates.cend());
//    
//    auto phased_regions = phaser.get_phased_regions(HaplotypePhaser::SteamingStatus::Finished);
//    
//    cout << "phased into " << phased_regions.size() << " sections" << endl;
//    
//    for (const auto& haplotype_count : phased_regions.front().the_latent_posteriors.haplotype_pseudo_counts) {
//        if (haplotype_count.second > 0.5) {
//            cout << haplotype_count.first << endl;
//            haplotype_count.first.print_explicit_alleles();
//            cout << haplotype_count.second << endl;
//        }
//    }
}
