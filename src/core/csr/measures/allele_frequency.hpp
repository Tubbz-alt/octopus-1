// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef allele_frequency
#define allele_frequency

#include <string>

#include "measure.hpp"

namespace octopus {

class VcfRecord;

namespace csr {

class AlleleFrequency : public Measure
{
    ResultType do_evaluate(const VcfRecord& call, const FacetMap& facets) const override;
    std::string do_name() const override;
    std::vector<std::string> do_requirements() const override;
};

} // namespace csr
} // namespace octopus

#endif
