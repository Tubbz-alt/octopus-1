// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "vcf_header_factory.hpp"

#include <typeinfo>
#include <string>

#include "exceptions/program_error.hpp"
#include "call_types.hpp"

namespace octopus {

VcfHeaderFactory::AnnotatorMap VcfHeaderFactory::annotators_ =
{
    {std::type_index(typeid(GermlineVariantCall)), [] (auto& hb) {
        hb.add_info("DMP", "1", "Float", "Dummy model posterior");
    }},
    {std::type_index(typeid(ReferenceCall)), [] (auto& hb) {
        hb.add_info("DMP", "1", "Float", "Dummy model posterior");
    }},
    {std::type_index(typeid(SomaticCall)), [] (auto& hb) {
        hb.add_format("SCR", "2", "Float", "99% credible region of the somatic allele frequency");
        hb.add_info("DMP", "1", "Float", "Dummy model posterior");
    }},
    {std::type_index(typeid(DenovoCall)), [] (auto& hb) {
        hb.add_info("DMP", "1", "Float", "Dummy model posterior");
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
