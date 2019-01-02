// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "vcf_header_factory.hpp"

#include <typeinfo>
#include <string>

#include "exceptions/program_error.hpp"
#include "core/types/calls/call_types.hpp"

namespace octopus {

VcfHeaderFactory::AnnotatorMap VcfHeaderFactory::annotators_ =
{
    {std::type_index(typeid(GermlineVariantCall)), [] (auto& hb) {
        hb.add_info("MP", "1", "Float", "Model posterior");
    }},
    {std::type_index(typeid(ReferenceCall)), [] (auto& hb) {
        hb.add_info("MP", "1", "Float", "Model posterior");
    }},
    {std::type_index(typeid(SomaticCall)), [] (auto& hb) {
        hb.add_info("SOMATIC", "0", "Flag", "Indicates that the record is a somatic mutation, for cancer genomics");
        hb.add_info("PP", "1", "Float", "Posterior probability for assertions made in ALT and FORMAT (Phred scale)");
        hb.add_info("MP", "1", "Float", "Model posterior");
        hb.add_format("MAP_VAF", "1", "Float", "Maximum a posteriori Variant Allele Frequency");
        hb.add_format("VAF_CR", "2", "Float", "Credible region for the Variant Allele Frequency");
    }},
    {std::type_index(typeid(DenovoCall)), [] (auto& hb) {
        hb.add_info("DENOVO", "0", "Flag", "Indicates that the record is a de novo mutation");
        hb.add_info("PP", "1", "Float", "Posterior probability for assertions made in ALT and FORMAT (Phred scale)");
        hb.add_info("MP", "1", "Float", "Model posterior");
    }},
    {std::type_index(typeid(DenovoReferenceReversionCall)), [] (auto& hb) {
        hb.add_info("REVERSION", "0", "Flag", "Indicates that the record contains a reference reversion");
    }},
    {std::type_index(typeid(CellVariantCall)), [] (auto& hb) {
        hb.add_info("SOMATIC", "0", "Flag", "Indicates that the record is a somatic mutation, for cancer genomics");
    }}
};

void VcfHeaderFactory::register_call_type(std::type_index type)
{
    call_types_.insert(type);
}

class UnregisteredCallType : public ProgramError
{
private:
    std::string do_where() const override
    {
        return "VcfHeaderFactory::annotate";
    }
    
    std::string do_why() const override
    {
        return "Call type not in annotation map";
    }
    
    std::string do_help() const override
    {
        return "Add type to map";
    }
};

void VcfHeaderFactory::annotate(VcfHeader::Builder &hb) const
{
    for (const auto& type : call_types_) {
        if (annotators_.count(type) == 0) {
            throw UnregisteredCallType {};
        }
        annotators_.at(type)(hb);
    }
}

} // namespace octopus
