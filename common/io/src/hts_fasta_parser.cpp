/*
* Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "hts_fasta_parser.hpp"

#include <string>

namespace claragenomics
{

FastaParserHTS::FastaParserHTS(const std::string& fasta_file)
{
    fasta_index_ = fai_load3(fasta_file.c_str(), NULL, NULL, FAI_CREATE);
    if (fasta_index_ == NULL)
    {
        throw std::runtime_error("Could not load fasta index!");
    }

    num_seqequences_ = faidx_nseq(fasta_index_);
    if (num_seqequences_ == 0)
    {
        throw std::runtime_error("FASTA file has 0 sequences");
    }
}

FastaParserHTS::~FastaParserHTS()
{
    fai_destroy(fasta_index_);
}

int32_t FastaParserHTS::get_num_seqences() const
{
    return num_seqequences_;
}

FastaSequence FastaParserHTS::get_sequence(int32_t i) const
{
    const char* name = faidx_iseq(fasta_index_, i);
    if (name == NULL)
    {
        throw std::runtime_error("No sequence found for ID " + std::to_string(i));
    }

    int32_t length;
    const char* seq = fai_fetch(fasta_index_, name, &length);
    if (length < 0)
    {
        throw std::runtime_error("Error in reading sequence information for seq ID " + std::to_string(i));
    }

    FastaSequence s{};
    s.name = std::string(name);
    s.seq  = std::string(seq);

    return s;
}

} // namespace claragenomics
