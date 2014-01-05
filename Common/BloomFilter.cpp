/*
 * BloomFilter.cpp
 *
 *  Created on: Aug 10, 2012
 *      Author: cjustin
 */
//@TODO: experiment with hash concepts by Adam Kirsch and Michael Mitzenmacher in Building a Better Bloom Filter
#include "BloomFilter.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <stdio.h>
#include <cstring>

/* De novo filter constructor.
 *
 * preconditions:
 * filterSize must be a multiple of 64
 * kmerSize refers to the number of bases the kmer has
 * k-mers supplied to this object should be binary (2 bits per base)
 */
BloomFilter::BloomFilter(size_t filterSize, uint8_t hashNum, uint16_t kmerSize) :
		size(filterSize), hashNum(hashNum), kmerSize(kmerSize), kmerSizeInBytes(
				(kmerSize + 4 - 1) / 4) {
	initSize(size);
	memset(filter, 0, sizeInBytes);
}

/*
 * Loads the filter (file is a .bf file) from path specified
 */
BloomFilter::BloomFilter(size_t filterSize, uint8_t hashNum, uint16_t kmerSize,
		string const &filterFilePath) :
		size(filterSize), hashNum(hashNum), kmerSize(kmerSize), kmerSizeInBytes(
				(kmerSize + 4 - 1) / 4) {
	initSize(size);

	//Check file size is correct size
	struct stat sb;
	stat(filterFilePath.c_str(), &sb);
	if (sb.st_size != filterSize / 8) {
		cerr << "Error: " << filterFilePath
				<< " does not match size given by its information file. Size: "
				<< sb.st_size << "/" << filterSize / 8 << " bytes." << endl;
		exit(1);
	}

	//load in blocks to a vector
	ifstream binaryFile(filterFilePath.c_str(), ios::binary);
	if (binaryFile.is_open()) {
		binaryFile.read(reinterpret_cast<char*>(filter), size);
	} else {
		cerr << "file \"" << filterFilePath << "\" could not be read." << endl;
		binaryFile.close();
		exit(1);
	}
	binaryFile.close();
}

/*
 * Checks filter size and initializes filter
 */
void BloomFilter::initSize(size_t size) {
	if (size % 8 != 0) {
		cerr << "ERROR: Filter Size \"" << size << "\" is not a multiple of 8."
				<< endl;
		exit(1);
	}
	sizeInBytes = size / bitsPerChar;
	filter = new unsigned char[sizeInBytes];
}

/*
 * Accepts a list of precomputed hash values. Faster than rehashing each time.
 */
void BloomFilter::insert(vector<size_t> const &precomputed) {

	//iterates through hashed values adding it to the filter
	for (size_t i = 0; i < hashNum; ++i) {
		size_t normalizedValue = precomputed.at(i) % size;

//		cout << normalizedValue << endl;
//		exit(1);

		filter[normalizedValue / bitsPerChar] |= bitMask[normalizedValue
				% bitsPerChar];
	}
}

void BloomFilter::insert(const unsigned char* kmer) {
	//iterates through hashed values adding it to the filter
	for (size_t i = 0; i < hashNum ; ++i) {
		size_t normalizedValue = CityHash64WithSeed(
				reinterpret_cast<const char*>(kmer), kmerSizeInBytes, i) % size;
		filter[normalizedValue / bitsPerChar] |= bitMask[normalizedValue
				% bitsPerChar];
	}
}

/*
 * Accepts a list of precomputed hash values. Faster than rehashing each time.
 */
const bool BloomFilter::contains(vector<size_t> const &values) const {
	for (size_t i = 0; i < hashNum; ++i) {
		size_t normalizedValue = values.at(i) % size;
		unsigned char bit = bitMask[normalizedValue % bitsPerChar];
		if ((filter[normalizedValue / bitsPerChar] & bit) != bit) {
			return false;
		}
	}
	return true;
}

/*
 * Single pass filtering, computes hash values on the fly
 */
const bool BloomFilter::contains(const unsigned char* kmer) const {
	for (int i = 0; i < hashNum; ++i) {
		size_t normalizedValue = CityHash64WithSeed(
				reinterpret_cast<const char*>(kmer), kmerSizeInBytes, i) % size;
		unsigned char bit = bitMask[normalizedValue % bitsPerChar];
		if ((filter[normalizedValue / bitsPerChar] & bit) != bit) {
			return false;
		}
	}
	return true;
}

/*
 * Stores the filter as a binary file to the path specified
 * Stores uncompressed because the random data tend to
 * compress poorly anyway
 */
void BloomFilter::storeFilter(string const &filterFilePath) const {
	ofstream myFile(filterFilePath.c_str(), ios::out | ios::binary);

	assert(myFile.good());
	//write out each block
	for (int i = 0; i < sizeInBytes; i++) {
		myFile << filter[i];
	}
	assert(myFile.good());
	myFile.close();
}

uint8_t BloomFilter::getHashNum() const {
	return hashNum;
}

uint8_t BloomFilter::getKmerSize() const {
	return kmerSize;
}

BloomFilter::~BloomFilter() {
	delete[] filter;
}
