// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef __Octopus__vcf_parser__
#define __Octopus__vcf_parser__

#include <vector>
#include <string>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>

#include <boost/filesystem/path.hpp>

#include "vcf_reader_impl.hpp"
#include "vcf_header.hpp"
#include "vcf_record.hpp"

namespace octopus {

namespace fs = boost::filesystem;

class GenomicRegion;

class VcfParser : public IVcfReaderImpl
{
public:
    using IVcfReaderImpl::RecordContainer;
    
    class RecordIterator;
    
    using IVcfReaderImpl::RecordIteratorPtrPair;
    
    VcfParser() = delete;
    
    VcfParser(const fs::path& file_path);
    
    VcfParser(const VcfParser&)            = delete;
    VcfParser& operator=(const VcfParser&) = delete;
    VcfParser(VcfParser&&)                 = default;
    VcfParser& operator=(VcfParser&&)      = default;
    
    ~VcfParser() = default;
    
    bool is_header_written() const noexcept override;
    
    VcfHeader fetch_header() const override;
    
    std::size_t count_records() const override;
    std::size_t count_records(const std::string& contig) const override;
    std::size_t count_records(const GenomicRegion& region) const override;
    
    RecordIteratorPtrPair iterate(UnpackPolicy level) const override;
    
    RecordContainer fetch_records(UnpackPolicy level) const override;
    RecordContainer fetch_records(const std::string& contig, UnpackPolicy level) const override;
    RecordContainer fetch_records(const GenomicRegion& region, UnpackPolicy level) const override;
    
    friend RecordIterator;
    friend bool operator==(const RecordIterator& lhs, const RecordIterator& rhs);
private:
    fs::path file_path_;
    
    mutable std::ifstream file_;
    
    VcfHeader header_;
    
    const std::vector<std::string> samples_;
    const std::streampos first_record_pos_; // must go after header_!
    
    void reset_vcf() const; // logically const
};

class VcfParser::RecordIterator : public IVcfReaderImpl::RecordIterator
{
public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = VcfRecord;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const VcfRecord*;
    using reference         = const VcfRecord&;
    
    RecordIterator() = default;
    
    RecordIterator(const VcfParser& vcf, UnpackPolicy unpack);
    
    ~RecordIterator() = default;
    
    RecordIterator(const RecordIterator&)            = default;
    RecordIterator& operator=(const RecordIterator&) = default;
    RecordIterator(RecordIterator&&)                 = default;
    RecordIterator& operator=(RecordIterator&&)      = default;
    
    reference operator*() const override;
    pointer operator->() const override;
    
    void next() override;
    RecordIterator& operator++();
    
    friend bool operator==(const RecordIterator& lhs, const RecordIterator& rhs);
private:
    std::shared_ptr<VcfRecord> record_;
    const VcfParser* parent_vcf_;
    UnpackPolicy unpack_;
    mutable std::ifstream local_;
    mutable std::string line_;
};

bool operator!=(const VcfParser::RecordIterator& lhs, const VcfParser::RecordIterator& rhs);

} // namespace octopus

#endif /* defined(__Octopus__vcf_parser__) */