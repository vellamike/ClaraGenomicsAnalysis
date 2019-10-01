/*
* Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <chrono>
#include <getopt.h>
#include <iostream>
#include <string>

#include <claragenomics/logging/logging.hpp>

#include "cudamapper/index.hpp"
#include "matcher.hpp"
#include "overlapper_triggered.hpp"


static struct option options[] = {
        {"window-size", required_argument , 0, 'w'},
        {"kmer-size", required_argument, 0, 'k'},
        {"index-size", optional_argument, 0, 'i'},
        {"help", no_argument, 0, 'h'},
};

void help();

int main(int argc, char *argv[])
{
    claragenomics::logging::Init();

    uint32_t k = 15;
    uint32_t w = 15;
    size_t index_size = 10000;
    std::string optstring = "i:k:w:h";
    uint32_t argument;
    while ((argument = getopt_long(argc, argv, optstring.c_str(), options, nullptr)) != -1){
        switch (argument) {
            case 'k':
                k = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                break;
            case 'i':
                index_size = atoi(optarg);
                break;
            case 'h':
                help();
                exit(0);
            default:
                exit(1);
        }
    }

    CGA_LOG_INFO("Creating index");

    if (optind >= argc){
        help();
        exit(1);
    }

    if (k > claragenomics::Index::maximum_kmer_size()){
        std::cerr << "kmer of size " << k << " is not allowed, maximum k = " <<
            claragenomics::Index::maximum_kmer_size() << std::endl;
        exit(1);
    }

    //Now carry out all the looped polling
    size_t query_start = 0;
    size_t query_end = query_start + index_size;

    std::string input_filepath = std::string(argv[optind]);

    // Track overall time
    std::chrono::milliseconds index_time = std::chrono::duration_values<std::chrono::milliseconds>::zero();
    std::chrono::milliseconds matcher_time = std::chrono::duration_values<std::chrono::milliseconds>::zero();
    std::chrono::milliseconds overlapper_time = std::chrono::duration_values<std::chrono::milliseconds>::zero();
    std::chrono::milliseconds paf_time = std::chrono::duration_values<std::chrono::milliseconds>::zero();

    while(true) { // outer loop over query
        auto start_time = std::chrono::high_resolution_clock::now();

        //first generate a2a for query
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        std::pair<std::uint64_t, std::uint64_t> query_range {query_start, query_end};

        ranges.push_back(query_range);

        std::unique_ptr<claragenomics::Index> index = claragenomics::Index::create_index(input_filepath, k, w, ranges);

        CGA_LOG_INFO("Created index");
        std::cerr << "Index execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;

        index_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time);
        //auto num_reads = index.get()->number_of_reads();
        auto match_point = 0; // all to all

        start_time = std::chrono::high_resolution_clock::now();
        CGA_LOG_INFO("Started matcher");
        claragenomics::Matcher matcher(*index, match_point);
        CGA_LOG_INFO("Finished matcher");
        std::cerr << "Matcher execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
        matcher_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time);

        start_time = std::chrono::high_resolution_clock::now();
        CGA_LOG_INFO("Started overlap detector");
        auto overlapper = claragenomics::OverlapperTriggered();
        auto overlaps = overlapper.get_overlaps(matcher.anchors(), *index);

        CGA_LOG_INFO("Finished overlap detector");
        std::cerr << "Overlap detection execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
        overlapper_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time);

        start_time = std::chrono::high_resolution_clock::now();
        overlapper.print_paf(overlaps);
        std::cerr << "PAF output execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
        paf_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start_time);

        size_t target_start = query_end + 1;
        size_t target_end = target_start + index_size;

        if (index.get()->number_of_reads()  < index_size){ //reached the end of the reads
            break;
        }

        while(true){ //Now loop over the targets
            start_time = std::chrono::high_resolution_clock::now();
            //first generate a2a for query
            std::vector<std::pair<std::uint64_t, std::uint64_t>> target_ranges;
            std::pair<std::uint64_t, std::uint64_t> target_range {target_start, target_end};

            target_ranges.push_back(query_range);
            target_ranges.push_back(target_range);

            auto new_index = claragenomics::Index::create_index(input_filepath, k, w, target_ranges);

            CGA_LOG_INFO("Created index");
            std::cerr << "Index execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
            index_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time);

            match_point = (query_range.second - query_range.first);

            start_time = std::chrono::high_resolution_clock::now();
            CGA_LOG_INFO("Started matcher");
            claragenomics::Matcher qt_matcher(*new_index, match_point);
            CGA_LOG_INFO("Finished matcher");
            std::cerr << "Matcher execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
            matcher_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time);

            start_time = std::chrono::high_resolution_clock::now();
            CGA_LOG_INFO("Started overlap detector");
            //auto overlapper = claragenomics::OverlapperTriggered();
            overlaps = overlapper.get_overlaps(qt_matcher.anchors(), *new_index);

            CGA_LOG_INFO("Finished overlap detector");
            std::cerr << "Overlap detection execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
            overlapper_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time);

            start_time = std::chrono::high_resolution_clock::now();
            overlapper.print_paf(overlaps);
            std::cerr << "PAF output execution time: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count() << "ms" << std::endl;
            paf_time += std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - start_time);

            if (new_index.get()->number_of_reads()  < (index_size * 2)){ //reached the end of the reads
                break;
            }

            target_start = target_end + 1;
            target_end = target_start + index_size;
        }
        //update query positions
        query_start = query_end + 1;
        query_end = query_start + index_size;
    }
    std::cerr << "\n\n" << std::endl;
            std::cerr << "Index execution time: " << index_time.count() << "ms" << std::endl;
            std::cerr << "Matcher execution time: " << matcher_time.count() << "ms" << std::endl;
            std::cerr << "Overlap detection execution time: " << overlapper_time.count() << "ms" << std::endl;
            std::cerr << "PAF output execution time: " << paf_time.count() << "ms" << std::endl;
    return 0;
}

void help() {
    std::cout<<
    R"(Usage: cudamapper [options ...] <sequences>
     <sequences>
        Input file in FASTA/FASTQ format (can be compressed with gzip)
        containing sequences used for all-to-all overlapping
     options:
        -k, --kmer-size
            length of kmer to use for minimizers [15] (Max=)" << claragenomics::Index::maximum_kmer_size() << ")" << R"(
        -w, --window-size
            length of window to use for minimizers [15])" << R"(
        -i, --index-size
            length of index batch size to use [10000])" << std::endl;
}
