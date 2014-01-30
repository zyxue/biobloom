/*
 * BioBloomClassifier.cpp
 *
 *  Created on: Oct 17, 2012
 *      Author: cjustin
 */

#include "BioBloomClassifier.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include "Common/Dynamicofstream.h"
#include "ResultsManager.h"
#if _OPENMP
# include <omp.h>
#endif

BioBloomClassifier::BioBloomClassifier(const vector<string> &filterFilePaths,
		double scoreThreshold, const string &prefix,
		const string &outputPostFix, uint16_t streakThreshold, uint16_t minHit,
		bool minHitOnly) :
		scoreThreshold(scoreThreshold), filterNum(filterFilePaths.size()), noMatch(
				"noMatch"), multiMatch("multiMatch"), prefix(prefix), postfix(
				outputPostFix), streakThreshold(streakThreshold), minHit(
				minHit), minHitOnly(minHitOnly)
{
	loadFilters(filterFilePaths);
}

/*
 * Generic filtering function (single end, no fa or fq file outputs)
 */
void BioBloomClassifier::filter(const vector<string> &inputFiles)
{

	//results summary object
	ResultsManager resSummary(hashSigs, filters, infoFiles, scoreThreshold);

	size_t totalReads = 0;

	//print out header info and initialize variables

	cerr << "Filtering Start" << endl;

	if (minHitOnly) {
		for (vector<string>::const_iterator it = inputFiles.begin();
				it != inputFiles.end(); ++it)
		{
			FastaReader sequence(it->c_str(), FastaReader::NO_FOLD_CASE);
			FastqRecord rec;

			//stored out of loop so reallocation does not have to be done
			unordered_map<string, double> hits(filterNum);
			while (sequence >> rec) {

				//track read progress
				++totalReads;
				if (totalReads % 1000000 == 0) {
					cerr << "Currently Reading Read Number: " << totalReads
							<< endl;
				}

				//initialize hits to zero
				initHits(hits);

				//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
				for (vector<string>::const_iterator j = hashSigs.begin();
						j != hashSigs.end(); ++j)
				{
					evaluateRead(rec, *j, hits);
				}

				//Evaluate hit data and record for summary
				resSummary.updateSummaryData(rec.seq.length(), hits);
			}
		}
	} else {
		for (vector<string>::const_iterator it = inputFiles.begin();
				it != inputFiles.end(); ++it)
		{
			FastaReader sequence(it->c_str(), FastaReader::NO_FOLD_CASE);
#pragma omp parallel
			for (FastqRecord rec;;) {
				bool good;
#pragma omp critical(sequence)
				{
					good = sequence >> rec;
					//track read progress
					++totalReads;
					if (totalReads % 1000000 == 0) {
						cerr << "Currently Reading Read Number: " << totalReads
								<< endl;
					}
				}

				if (good)
				{
					unordered_map<string, double> hits(filterNum);
					//initialize hits to zero
					initHits(hits);

					//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
					for (vector<string>::const_iterator j = hashSigs.begin();
							j != hashSigs.end(); ++j)
					{
						evaluateReadStd(rec, *j, hits);
					}

					//Evaluate hit data and record for summary
					resSummary.updateSummaryData(rec.seq.length(), hits);
				}
				else
					break;
			}
			assert(sequence.eof());
		}
	}

	cerr << "Total Reads:" << totalReads << endl;

	Dynamicofstream summaryOutput(prefix + "_summary.tsv");
	summaryOutput << resSummary.getResultsSummary(totalReads);
	summaryOutput.close();
}

/*
 * Filters reads
 * Assumes only one hash signature exists (load only filters with same
 * hash functions)
 * Prints reads into seperate files
 *
 * outputType must be fa or fq
 */
void BioBloomClassifier::filterPrint(const vector<string> &inputFiles,
		const string &outputType)
{

	//results summary object
	ResultsManager resSummary(hashSigs, filters, infoFiles, scoreThreshold);

	size_t totalReads = 0;

	unordered_map<string, shared_ptr<Dynamicofstream> > outputFiles;
	shared_ptr<Dynamicofstream> no_match(
			new Dynamicofstream(
					prefix + "_" + noMatch + "." + outputType + postfix));
	shared_ptr<Dynamicofstream> multi_match(
			new Dynamicofstream(
					prefix + "_" + multiMatch + "." + outputType + postfix));
	outputFiles[noMatch] = no_match;
	outputFiles[multiMatch] = multi_match;

	//initialize variables
	for (vector<string>::const_iterator j = hashSigs.begin();
			j != hashSigs.end(); ++j)
	{
		const vector<string> idsInFilter = (*filters[*j]).getFilterIds();
		for (vector<string>::const_iterator i = idsInFilter.begin();
				i != idsInFilter.end(); ++i)
		{
			shared_ptr<Dynamicofstream> temp(
					new Dynamicofstream(
							prefix + "_" + *i + "." + outputType + postfix));
			outputFiles[*i] = temp;
		}
	}

	//print out header info and initialize variables

	cerr << "Filtering Start" << endl;

	if (minHitOnly) {

		for (vector<string>::const_iterator it = inputFiles.begin();
				it != inputFiles.end(); ++it)
		{
			FastaReader sequence(it->c_str(), FastaReader::NO_FOLD_CASE);
			FastqRecord rec;
			//hits results stored in hashmap of filternames and hits
			unordered_map<string, double> hits(filterNum);
			while (sequence >> rec) {

				++totalReads;
				if (totalReads % 1000000 == 0) {
					cerr << "Currently Reading Read Number: " << totalReads
							<< endl;
				}

				//initialize hits to zero
				initHits(hits);

				//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
				for (vector<string>::const_iterator j = hashSigs.begin();
						j != hashSigs.end(); ++j)
				{
					evaluateRead(rec, *j, hits);
				}

				//Evaluate hit data and record for summary
				const string &outputFileName = resSummary.updateSummaryData(
						rec.seq.length(), hits);

				if (outputType == "fa") {
					(*outputFiles[outputFileName]) << ">" << rec.id << "\n"
							<< rec.seq << "\n";
				} else {
					(*outputFiles[outputFileName]) << "@" << rec.id << "\n"
							<< rec.seq << "\n+\n" << rec.qual << "\n";
				}
			}
		}
	} else {

		for (vector<string>::const_iterator it = inputFiles.begin();
				it != inputFiles.end(); ++it)
		{
			FastaReader sequence(it->c_str(), FastaReader::NO_FOLD_CASE);
			FastqRecord rec;
			//hits results stored in hashmap of filternames and hits
			unordered_map<string, double> hits(filterNum);
			while (sequence >> rec) {
				++totalReads;
				if (totalReads % 1000000 == 0) {
					cerr << "Currently Reading Read Number: " << totalReads
							<< endl;
				}

				//initialize hits to zero
				initHits(hits);

				//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
				for (vector<string>::const_iterator j = hashSigs.begin();
						j != hashSigs.end(); ++j)
				{
					evaluateReadStd(rec, *j, hits);
				}

				//Evaluate hit data and record for summary
				const string &outputFileName = resSummary.updateSummaryData(
						rec.seq.length(), hits);

				if (outputType == "fa") {
					(*outputFiles[outputFileName]) << ">" << rec.id << "\n"
							<< rec.seq << "\n";
				} else {
					(*outputFiles[outputFileName]) << "@" << rec.id << "\n"
							<< rec.seq << "\n+\n" << rec.qual << "\n";
				}
			}
		}
	}

	//close sorting files
	for (unordered_map<string, shared_ptr<Dynamicofstream> >::iterator j =
			outputFiles.begin(); j != outputFiles.end(); ++j)
	{
		j->second->close();
	}
	cerr << "Total Reads:" << totalReads << endl;

	Dynamicofstream summaryOutput(prefix + "_summary.tsv");
	summaryOutput << resSummary.getResultsSummary(totalReads);
	summaryOutput.close();
}

/*
 * Filters reads -> uses paired end information
 * Assumes only one hash signature exists (load only filters with same
 * hash functions)
 */
void BioBloomClassifier::filterPair(const string &file1, const string &file2)
{

	//results summary object
	ResultsManager resSummary(hashSigs, filters, infoFiles, scoreThreshold);

	size_t totalReads = 0;

	//print out header info and initialize variables for summary

	cerr << "Filtering Start" << "\n";

	FastaReader sequence1(file1.c_str(), FastaReader::NO_FOLD_CASE);
	FastaReader sequence2(file2.c_str(), FastaReader::NO_FOLD_CASE);
	FastqRecord rec1;
	FastqRecord rec2;
	//hits results stored in hashmap of filter names and hits
	unordered_map<string, double> hits1(filterNum);
	unordered_map<string, double> hits2(filterNum);

	if (minHitOnly) {
		while (sequence1 >> rec1 && sequence2 >> rec2) {
			++totalReads;
			if (totalReads % 1000000 == 0) {
				cerr << "Currently Reading Read Number: " << totalReads << endl;
			}

			//initialize hits to zero
			initHits(hits1);
			initHits(hits2);

			//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
			for (vector<string>::const_iterator j = hashSigs.begin();
					j != hashSigs.end(); ++j)
			{
				string tempStr1 = rec1.id.substr(0, rec1.id.find_last_of("/"));
				string tempStr2 = rec2.id.substr(0, rec2.id.find_last_of("/"));
				if (tempStr1 == tempStr2) {
					evaluateRead(rec1, *j, hits1);
					evaluateRead(rec2, *j, hits2);
				} else {
					cerr << "Read IDs do not match" << "\n" << tempStr1 << "\n"
							<< tempStr2 << endl;
					exit(1);
				}
			}

			string readID = rec1.id.substr(0, rec1.id.length() - 2);

			//Evaluate hit data and record for summary
			resSummary.updateSummaryData(rec1.seq.length(), rec2.seq.length(),
					hits1, hits2);
		}
	} else {

		while (sequence1 >> rec1 && sequence2 >> rec2) {
			++totalReads;
			if (totalReads % 1000000 == 0) {
				cerr << "Currently Reading Read Number: " << totalReads << endl;
			}

			//initialize hits to zero
			initHits(hits1);
			initHits(hits2);

			//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
			for (vector<string>::const_iterator j = hashSigs.begin();
					j != hashSigs.end(); ++j)
			{
				string tempStr1 = rec1.id.substr(0, rec1.id.find_last_of("/"));
				string tempStr2 = rec2.id.substr(0, rec2.id.find_last_of("/"));
				if (tempStr1 == tempStr2) {
					evaluateReadStd(rec1, *j, hits1);
					evaluateReadStd(rec2, *j, hits2);
				} else {
					cerr << "Read IDs do not match" << "\n" << tempStr1 << "\n"
							<< tempStr2 << endl;
					exit(1);
				}
			}

			string readID = rec1.id.substr(0, rec1.id.length() - 2);

			//Evaluate hit data and record for summary
			resSummary.updateSummaryData(rec1.seq.length(), rec2.seq.length(),
					hits1, hits2);
		}
	}

	if (sequence2 >> rec2 && sequence1.eof() && sequence2.eof()) {
		cerr
				<< "error: eof bit not flipped. Input files may be different lengths"
				<< endl;
	}

	cerr << "Total Reads:" << totalReads << endl;

	Dynamicofstream summaryOutput(prefix + "_summary.tsv");
	summaryOutput << resSummary.getResultsSummary(totalReads);
	summaryOutput.close();
}

/*
 * Filters reads -> uses paired end information
 * Assumes only one hash signature exists (load only filters with same
 * hash functions)
 * prints reads
 */
void BioBloomClassifier::filterPairPrint(const string &file1,
		const string &file2, const string &outputType)
{

	//results summary object
	ResultsManager resSummary(hashSigs, filters, infoFiles, scoreThreshold);

	size_t totalReads = 0;

	unordered_map<string, shared_ptr<Dynamicofstream> > outputFiles;
	shared_ptr<Dynamicofstream> noMatch1(
			new Dynamicofstream(
					prefix + "_" + noMatch + "_1." + outputType + postfix));
	shared_ptr<Dynamicofstream> noMatch2(
			new Dynamicofstream(
					prefix + "_" + noMatch + "_2." + outputType + postfix));
	shared_ptr<Dynamicofstream> multiMatch1(
			new Dynamicofstream(
					prefix + "_" + multiMatch + "_1." + outputType + postfix));
	shared_ptr<Dynamicofstream> multiMatch2(
			new Dynamicofstream(
					prefix + "_" + multiMatch + "_2." + outputType + postfix));
	outputFiles[noMatch + "1"] = noMatch1;
	outputFiles[noMatch + "2"] = noMatch2;
	outputFiles[multiMatch + "1"] = multiMatch1;
	outputFiles[multiMatch + "2"] = multiMatch2;

	//initialize variables and print filter ids
	for (vector<string>::const_iterator j = hashSigs.begin();
			j != hashSigs.end(); ++j)
	{
		const vector<string> idsInFilter = (*filters[*j]).getFilterIds();
		for (vector<string>::const_iterator i = idsInFilter.begin();
				i != idsInFilter.end(); ++i)
		{
			shared_ptr<Dynamicofstream> temp1(
					new Dynamicofstream(
							prefix + "_" + *i + "_1." + outputType + postfix));
			shared_ptr<Dynamicofstream> temp2(
					new Dynamicofstream(
							prefix + "_" + *i + "_2." + outputType + postfix));
			outputFiles[*i + "1"] = temp1;
			outputFiles[*i + "2"] = temp2;
		}
	}

	//print out header info and initialize variables for summary

	cerr << "Filtering Start" << "\n";

	FastaReader sequence1(file1.c_str(), FastaReader::NO_FOLD_CASE);
	FastaReader sequence2(file2.c_str(), FastaReader::NO_FOLD_CASE);
	FastqRecord rec1;
	FastqRecord rec2;
	//hits results stored in hashmap of filter names and hits
	unordered_map<string, double> hits1(filterNum);
	unordered_map<string, double> hits2(filterNum);

	if (minHitOnly) {
		while (sequence1 >> rec1 && sequence2 >> rec2) {
			++totalReads;
			if (totalReads % 1000000 == 0) {
				cerr << "Currently Reading Read Number: " << totalReads << endl;
			}

			//initialize hits to zero
			initHits(hits1);
			initHits(hits2);

			//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
			for (vector<string>::const_iterator j = hashSigs.begin();
					j != hashSigs.end(); ++j)
			{
				string tempStr1 = rec1.id.substr(0, rec1.id.find_last_of("/"));
				string tempStr2 = rec2.id.substr(0, rec2.id.find_last_of("/"));
				if (tempStr1 == tempStr2) {
					evaluateRead(rec1, *j, hits1);
					evaluateRead(rec2, *j, hits2);
				} else {
					cerr << "Read IDs do not match" << "\n" << tempStr1 << "\n"
							<< tempStr2 << endl;
					exit(1);
				}
			}

			string readID = rec1.id.substr(0, rec1.id.length() - 2);

			//Evaluate hit data and record for summary
			const string &outputFileName = resSummary.updateSummaryData(
					rec1.seq.length(), rec2.seq.length(), hits1, hits2);

			if (outputType == "fa") {
				(*outputFiles[outputFileName + "1"]) << ">" << rec1.id << "\n"
						<< rec1.seq << "\n";
				(*outputFiles[outputFileName + "2"]) << ">" << rec2.id << "\n"
						<< rec2.seq << "\n";
			} else {
				(*outputFiles[outputFileName + "1"]) << "@" << rec1.id << "\n"
						<< rec1.seq << "\n+\n" << rec1.qual << "\n";
				(*outputFiles[outputFileName + "2"]) << "@" << rec2.id << "\n"
						<< rec2.seq << "\n+\n" << rec2.qual << "\n";
			}
		}
	} else {

		while (sequence1 >> rec1 && sequence2 >> rec2) {
			++totalReads;
			if (totalReads % 1000000 == 0) {
				cerr << "Currently Reading Read Number: " << totalReads << endl;
			}

			//initialize hits to zero
			initHits(hits1);
			initHits(hits2);

			//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
			for (vector<string>::const_iterator j = hashSigs.begin();
					j != hashSigs.end(); ++j)
			{
				string tempStr1 = rec1.id.substr(0, rec1.id.find_last_of("/"));
				string tempStr2 = rec2.id.substr(0, rec2.id.find_last_of("/"));
				if (tempStr1 == tempStr2) {
					evaluateReadStd(rec1, *j, hits1);
					evaluateReadStd(rec2, *j, hits2);
				} else {
					cerr << "Read IDs do not match" << "\n" << tempStr1 << "\n"
							<< tempStr2 << endl;
					exit(1);
				}
			}

			string readID = rec1.id.substr(0, rec1.id.length() - 2);

			//Evaluate hit data and record for summary
			const string &outputFileName = resSummary.updateSummaryData(
					rec1.seq.length(), rec2.seq.length(), hits1, hits2);

			if (outputType == "fa") {
				(*outputFiles[outputFileName + "1"]) << ">" << rec1.id << "\n"
						<< rec1.seq << "\n";
				(*outputFiles[outputFileName + "2"]) << ">" << rec2.id << "\n"
						<< rec2.seq << "\n";
			} else {
				(*outputFiles[outputFileName + "1"]) << "@" << rec1.id << "\n"
						<< rec1.seq << "\n+\n" << rec1.qual << "\n";
				(*outputFiles[outputFileName + "2"]) << "@" << rec2.id << "\n"
						<< rec2.seq << "\n+\n" << rec2.qual << "\n";
			}
		}
	}
	if (sequence2 >> rec2 && sequence1.eof() && sequence2.eof()) {
		cerr
				<< "error: eof bit not flipped. Input files may be different lengths"
				<< endl;
	}

	//close sorting files
	for (unordered_map<string, shared_ptr<Dynamicofstream> >::iterator j =
			outputFiles.begin(); j != outputFiles.end(); ++j)
	{
		j->second->close();
	}

	cerr << "Total Reads:" << totalReads << endl;

	Dynamicofstream summaryOutput(prefix + "_summary.tsv");
	summaryOutput << resSummary.getResultsSummary(totalReads);
	summaryOutput.close();
}

/*
 * Filters reads -> uses paired end information
 * Assumes only one hash signature exists (load only filters with same
 * hash functions)
 */
void BioBloomClassifier::filterPairBAM(const string &file)
{

	//results summary object
	ResultsManager resSummary(hashSigs, filters, infoFiles, scoreThreshold);

	unordered_map<string, FastqRecord> unPairedReads;

	size_t totalReads = 0;

	//print out header info and initialize variables for summary

	cerr << "Filtering Start" << "\n";

	FastaReader sequence(file.c_str(), FastaReader::NO_FOLD_CASE);
	//hits results stored in hashmap of filter names and hits
	unordered_map<string, double> hits1(filterNum);
	unordered_map<string, double> hits2(filterNum);

	if (minHitOnly) {
		while (!sequence.eof()) {
			FastqRecord rec;
			if (sequence >> rec) {
				string readID = rec.id.substr(0, rec.id.length() - 2);
				if (unPairedReads.find(readID) != unPairedReads.end()) {

					const FastqRecord &rec1 =
							rec.id.at(rec.id.length() - 1) == '1' ?
									rec : unPairedReads[readID];
					const FastqRecord &rec2 =
							rec.id.at(rec.id.length() - 1) == '2' ?
									rec : unPairedReads[readID];

					++totalReads;
					if (totalReads % 1000000 == 0) {
						cerr << "Currently Reading Read Number: " << totalReads
								<< endl;
					}

					//initialize hits to zero
					initHits(hits1);
					initHits(hits2);

					//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
					for (vector<string>::const_iterator j = hashSigs.begin();
							j != hashSigs.end(); ++j)
					{
						string tempStr1 = rec1.id.substr(0,
								rec1.id.find_last_of("/"));
						string tempStr2 = rec2.id.substr(0,
								rec2.id.find_last_of("/"));
						if (tempStr1 == tempStr2) {
							evaluateRead(rec1, *j, hits1);
							evaluateRead(rec2, *j, hits2);
						} else {
							cerr << "Read IDs do not match" << "\n" << tempStr1
									<< "\n" << tempStr2 << endl;
							exit(1);
						}
					}

					//Evaluate hit data and record for summary
					resSummary.updateSummaryData(rec1.seq.length(),
							rec2.seq.length(), hits1, hits2);

					//clean up reads
					unPairedReads.erase(readID);

				} else {
					unPairedReads[readID] = rec;
				}
			}
		}
	} else {
		while (!sequence.eof()) {
			FastqRecord rec;
			if (sequence >> rec) {
				string readID = rec.id.substr(0, rec.id.length() - 2);
				if (unPairedReads.find(readID) != unPairedReads.end()) {

					const FastqRecord &rec1 =
							rec.id.at(rec.id.length() - 1) == '1' ?
									rec : unPairedReads[readID];
					const FastqRecord &rec2 =
							rec.id.at(rec.id.length() - 1) == '2' ?
									rec : unPairedReads[readID];

					++totalReads;
					if (totalReads % 1000000 == 0) {
						cerr << "Currently Reading Read Number: " << totalReads
								<< endl;
					}

					//initialize hits to zero
					initHits(hits1);
					initHits(hits2);

					//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
					for (vector<string>::const_iterator j = hashSigs.begin();
							j != hashSigs.end(); ++j)
					{
						string tempStr1 = rec1.id.substr(0,
								rec1.id.find_last_of("/"));
						string tempStr2 = rec2.id.substr(0,
								rec2.id.find_last_of("/"));
						if (tempStr1 == tempStr2) {
							evaluateReadStd(rec1, *j, hits1);
							evaluateReadStd(rec2, *j, hits2);
						} else {
							cerr << "Read IDs do not match" << "\n" << tempStr1
									<< "\n" << tempStr2 << endl;
							exit(1);
						}
					}

					//Evaluate hit data and record for summary
					resSummary.updateSummaryData(rec1.seq.length(),
							rec2.seq.length(), hits1, hits2);

					//clean up reads
					unPairedReads.erase(readID);

				} else {
					unPairedReads[readID] = rec;
				}
			}
		}
	}

	Dynamicofstream summaryOutput(prefix + "_summary.tsv");
	summaryOutput << resSummary.getResultsSummary(totalReads);
	summaryOutput.close();
}

/*
 * Filters reads -> uses paired end information
 * Assumes only one hash signature exists (load only filters with same
 * hash functions)
 * Prints reads into seperate files
 */
void BioBloomClassifier::filterPairBAMPrint(const string &file,
		const string &outputType)
{

	//results summary object
	ResultsManager resSummary(hashSigs, filters, infoFiles, scoreThreshold);

	unordered_map<string, FastqRecord> unPairedReads;

	size_t totalReads = 0;

	unordered_map<string, shared_ptr<Dynamicofstream> > outputFiles;
	shared_ptr<Dynamicofstream> noMatch1(
			new Dynamicofstream(
					prefix + "_" + noMatch + "_1." + outputType + postfix));
	shared_ptr<Dynamicofstream> noMatch2(
			new Dynamicofstream(
					prefix + "_" + noMatch + "_2." + outputType + postfix));
	shared_ptr<Dynamicofstream> multiMatch1(
			new Dynamicofstream(
					prefix + "_" + multiMatch + "_1." + outputType + postfix));
	shared_ptr<Dynamicofstream> multiMatch2(
			new Dynamicofstream(
					prefix + "_" + multiMatch + "_2." + outputType + postfix));
	outputFiles[noMatch + "1"] = noMatch1;
	outputFiles[noMatch + "2"] = noMatch2;
	outputFiles[multiMatch + "1"] = multiMatch1;
	outputFiles[multiMatch + "2"] = multiMatch2;

	//initialize variables and print filter ids
	for (vector<string>::const_iterator j = hashSigs.begin();
			j != hashSigs.end(); ++j)
	{
		const vector<string> idsInFilter = (*filters[*j]).getFilterIds();
		for (vector<string>::const_iterator i = idsInFilter.begin();
				i != idsInFilter.end(); ++i)
		{
			shared_ptr<Dynamicofstream> temp1(
					new Dynamicofstream(
							prefix + "_" + *i + "_1." + outputType + postfix));
			shared_ptr<Dynamicofstream> temp2(
					new Dynamicofstream(
							prefix + "_" + *i + "_2." + outputType + postfix));
			outputFiles[*i + "1"] = temp1;
			outputFiles[*i + "2"] = temp2;
		}
	}

	//print out header info and initialize variables for summary
	cerr << "Filtering Start" << "\n";

	FastaReader sequence(file.c_str(), FastaReader::NO_FOLD_CASE);
	//hits results stored in hashmap of filter names and hits
	unordered_map<string, double> hits1(filterNum);
	unordered_map<string, double> hits2(filterNum);

	if (minHitOnly) {
		while (!sequence.eof()) {
			FastqRecord rec;
			if (sequence >> rec) {
				string readID = rec.id.substr(0, rec.id.length() - 2);
				if (unPairedReads.find(readID) != unPairedReads.end()) {

					const FastqRecord &rec1 =
							rec.id.at(rec.id.length() - 1) == '1' ?
									rec : unPairedReads[readID];
					const FastqRecord &rec2 =
							rec.id.at(rec.id.length() - 1) == '2' ?
									rec : unPairedReads[readID];

					++totalReads;
					if (totalReads % 1000000 == 0) {
						cerr << "Currently Reading Read Number: " << totalReads
								<< endl;
					}

					//initialize hits to zero
					initHits(hits1);
					initHits(hits2);

					//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
					for (vector<string>::const_iterator j = hashSigs.begin();
							j != hashSigs.end(); ++j)
					{
						string tempStr1 = rec1.id.substr(0,
								rec1.id.find_last_of("/"));
						string tempStr2 = rec2.id.substr(0,
								rec2.id.find_last_of("/"));
						if (tempStr1 == tempStr2) {
							evaluateRead(rec1, *j, hits1);
							evaluateRead(rec2, *j, hits2);
						} else {
							cerr << "Read IDs do not match" << "\n" << tempStr1
									<< "\n" << tempStr2 << endl;
							exit(1);
						}
					}

					//Evaluate hit data and record for summary
					const string &outputFileName = resSummary.updateSummaryData(
							rec1.seq.length(), rec2.seq.length(), hits1, hits2);

					if (outputType == "fa") {
						(*outputFiles[outputFileName + "1"]) << ">" << rec1.id
								<< "\n" << rec1.seq << "\n";
						(*outputFiles[outputFileName + "2"]) << ">" << rec2.id
								<< "\n" << rec2.seq << "\n";
					} else {
						(*outputFiles[outputFileName + "1"]) << "@" << rec1.id
								<< "\n" << rec1.seq << "\n+\n" << rec1.qual
								<< "\n";
						(*outputFiles[outputFileName + "2"]) << "@" << rec2.id
								<< "\n" << rec2.seq << "\n+\n" << rec2.qual
								<< "\n";
					}

					//clean up reads
					unPairedReads.erase(readID);

				} else {
					unPairedReads[readID] = rec;
				}
			}
		}
	} else {
		while (!sequence.eof()) {
			FastqRecord rec;
			if (sequence >> rec) {
				string readID = rec.id.substr(0, rec.id.length() - 2);
				if (unPairedReads.find(readID) != unPairedReads.end()) {

					const FastqRecord &rec1 =
							rec.id.at(rec.id.length() - 1) == '1' ?
									rec : unPairedReads[readID];
					const FastqRecord &rec2 =
							rec.id.at(rec.id.length() - 1) == '2' ?
									rec : unPairedReads[readID];

					++totalReads;
					if (totalReads % 1000000 == 0) {
						cerr << "Currently Reading Read Number: " << totalReads
								<< endl;
					}

					//initialize hits to zero
					initHits(hits1);
					initHits(hits2);

					//for each hashSigniture/kmer combo multi, cut up read into kmer sized used
					for (vector<string>::const_iterator j = hashSigs.begin();
							j != hashSigs.end(); ++j)
					{
						string tempStr1 = rec1.id.substr(0,
								rec1.id.find_last_of("/"));
						string tempStr2 = rec2.id.substr(0,
								rec2.id.find_last_of("/"));
						if (tempStr1 == tempStr2) {
							evaluateReadStd(rec1, *j, hits1);
							evaluateReadStd(rec2, *j, hits2);
						} else {
							cerr << "Read IDs do not match" << "\n" << tempStr1
									<< "\n" << tempStr2 << endl;
							exit(1);
						}
					}

					//Evaluate hit data and record for summary
					const string &outputFileName = resSummary.updateSummaryData(
							rec1.seq.length(), rec2.seq.length(), hits1, hits2);

					if (outputType == "fa") {
						(*outputFiles[outputFileName + "1"]) << ">" << rec1.id
								<< "\n" << rec1.seq << "\n";
						(*outputFiles[outputFileName + "2"]) << ">" << rec2.id
								<< "\n" << rec2.seq << "\n";
					} else {
						(*outputFiles[outputFileName + "1"]) << "@" << rec1.id
								<< "\n" << rec1.seq << "\n+\n" << rec1.qual
								<< "\n";
						(*outputFiles[outputFileName + "2"]) << "@" << rec2.id
								<< "\n" << rec2.seq << "\n+\n" << rec2.qual
								<< "\n";
					}

					//clean up reads
					unPairedReads.erase(readID);

				} else {
					unPairedReads[readID] = rec;
				}
			}
		}
	}

	//close sorting files
	for (unordered_map<string, shared_ptr<Dynamicofstream> >::iterator j =
			outputFiles.begin(); j != outputFiles.end(); ++j)
	{
		j->second->close();
	}

	Dynamicofstream summaryOutput(prefix + "_summary.tsv");
	summaryOutput << resSummary.getResultsSummary(totalReads);
	summaryOutput.close();
}

//helper methods

/*
 * Loads list of filters into memory
 * todo: Implement non-block I/O when loading multiple filters at once
 */
void BioBloomClassifier::loadFilters(const vector<string> &filterFilePaths)
{
	cerr << "Starting to Load Filters." << endl;
	//load up files
	for (vector<string>::const_iterator it = filterFilePaths.begin();
			it != filterFilePaths.end(); ++it)
	{
		//check if files exist
		if (!fexists(*it)) {
			cerr << "Error: " + (*it) + " File cannot be opened" << endl;
			exit(1);
		}
		string infoFileName = (*it).substr(0, (*it).length() - 2) + "txt";
		if (!fexists(infoFileName)) {
			cerr
					<< "Error: " + (infoFileName)
							+ " File cannot be opened. A corresponding info file is needed."
					<< endl;
			exit(1);
		}

		//info file creation
		shared_ptr<BloomFilterInfo> info(new BloomFilterInfo(infoFileName));
		//append kmer size to hash signature to insure correct kmer size is used
		stringstream hashSig;
		hashSig << info->getHashNum() << info->getKmerSize();

		//if hashSig exists add filter to list
		if (infoFiles.count(hashSig.str()) != 1) {
			hashSigs.push_back(hashSig.str());
			vector<shared_ptr<BloomFilterInfo> > tempVect;
			shared_ptr<MultiFilter> temp(
					new MultiFilter(info->getHashNum(), info->getKmerSize()));
			filters[hashSig.str()] = temp;
			infoFiles[hashSig.str()] = tempVect;
		}
		infoFiles[hashSig.str()].push_back(info);
		boost::shared_ptr<BloomFilter> filter(
				new BloomFilter(info->getCalcuatedFilterSize(),
						info->getHashNum(), info->getKmerSize(), *it));
		filters[hashSig.str()]->addFilter(info->getFilterID(), filter);
		filtersSingle[info->getFilterID()] = filter;
		cerr << "Loaded Filter: " + info->getFilterID() << endl;
	}
	cerr << "Filter Loading Complete." << endl;
}

/*
 * checks if file exists
 */
const bool BioBloomClassifier::fexists(const string &filename) const
{
	ifstream ifile(filename.c_str());
	return ifile;
}

/*
 * For a single read evaluate hits for a single hash signature
 * Sections with ambiguity bases are treated as misses
 * Updates hits value to number of hits (hashSig is used to as key)
 * Faster variant that assume there a redundant tile of 0
 */
void BioBloomClassifier::evaluateRead(const FastqRecord &rec,
		const string &hashSig, unordered_map<string, double> &hits)
{
	//get filterIDs to iterate through has in a consistent order
	const vector<string> &idsInFilter = (*filters[hashSig]).getFilterIds();

	//get kmersize for set of info files
	uint16_t kmerSize = infoFiles.at(hashSig).front()->getKmerSize();

	//Establish tiling pattern
	uint16_t startModifier1 = (rec.seq.length() % kmerSize) / 2;
	size_t currentKmerNum = 0;

	ReadsProcessor proc(kmerSize);
	//cut read into kmer size given
	while (rec.seq.length() >= (currentKmerNum + 1) * kmerSize) {

		const unsigned char* currentKmer = proc.prepSeq(rec.seq,
				currentKmerNum * kmerSize + startModifier1);

		//check to see if string is invalid
		if (currentKmer != NULL) {

			const unordered_map<string, bool> &results =
					filters[hashSig]->multiContains(currentKmer);

			//record hit number in order
			for (vector<string>::const_iterator i = idsInFilter.begin();
					i != idsInFilter.end(); ++i)
			{
				if (results.find(*i)->second) {
					++hits[*i];
				}
			}
		}
		++currentKmerNum;
	}
}

/*
 * For a single read evaluate hits for a single hash signature
 * Sections with ambiguity bases are treated as misses
 * Updates hits value to number of hits (hashSig is used to as key)
 */
void BioBloomClassifier::evaluateReadStd(const FastqRecord &rec,
		const string &hashSig, unordered_map<string, double> &hits)
{

	//get filterIDs to iterate through has in a consistent order
	const vector<string> &idsInFilter = (*filters[hashSig]).getFilterIds();

	uint16_t kmerSize = infoFiles.at(hashSig).front()->getKmerSize();

	ReadsProcessor proc(kmerSize);

	double normalizationValue = rec.seq.length() - kmerSize + 1;
	double threshold = scoreThreshold * normalizationValue;
	double threshold_miss = 1.0 - threshold;

	for (vector<string>::const_iterator i = idsInFilter.begin();
			i != idsInFilter.end(); ++i)
	{
		bool pass = false;
		if (minHit > 0) {
			uint16_t screeningHits = 0;
			size_t screeningLoc = rec.seq.length() % kmerSize / 2;
			//First pass filtering
			while (rec.seq.length() >= screeningLoc + kmerSize) {
				const unsigned char* currentKmer = proc.prepSeq(rec.seq,
						screeningLoc);
				if (currentKmer != NULL) {
					if (filtersSingle.at(*i)->contains(currentKmer)) {
						screeningHits++;
						if (screeningHits >= minHit) {
							pass = true;
							break;
						}
					}
				}
				screeningLoc += kmerSize;
			}
		} else {
			pass = true;
		}
		if (pass) {
			size_t currentLoc = 0;
			double score = 0;
			uint16_t streak = 0;
			while (rec.seq.length() >= currentLoc + kmerSize) {
				const unsigned char* currentKmer = proc.prepSeq(rec.seq,
						currentLoc);
				if (streak == 0) {
					if (currentKmer != NULL) {
						if (filtersSingle.at(*i)->contains(currentKmer)) {
							score += 0.5;
							++streak;
						}
						++currentLoc;
					} else {
						currentLoc += kmerSize + 1;
					}
				} else {
					if (currentKmer != NULL) {
						if (filtersSingle.at(*i)->contains(currentKmer)) {
							++streak;
							score += 1 - 1 / (2 * streak);
							++currentLoc;
							if (threshold <= score) {
								break;
							}
							continue;
						}
					} else {
						currentLoc += kmerSize + 1;
					}
					if (streak < streakThreshold) {
						++currentLoc;
					} else {
						currentLoc += kmerSize;
					}
					streak = 0;
				}
			}
			hits[*i] = score / normalizationValue;
		}
	}
}

///*
// * Initializes Summary Variables. Also prints headers for read status.
// */
//const string BioBloomClassifier::getReadSummaryHeader(
//		const vector<string> &hashSigs)
//{
//	stringstream readStatusOutput;
//	readStatusOutput << "readID\tseqSize";
//
//	//initialize variables and print filter ids
//	for (vector<string>::const_iterator j = hashSigs.begin();
//			j != hashSigs.end(); ++j)
//	{
//		vector<string> idsInFilter = (*filters[*j]).getFilterIds();
//		for (vector<string>::const_iterator i = idsInFilter.begin();
//				i != idsInFilter.end(); ++i)
//		{
//			readStatusOutput << "\t" << *i << "_"
//					<< (*(infoFiles[*j].front())).getKmerSize();
//		}
//	}
//	readStatusOutput << "\n";
//	return readStatusOutput.str();
//}

/*
 * Initializes hits results to zero
 */
void BioBloomClassifier::initHits(unordered_map<string, double> &hits)
{
	//initialize hits to zero
	for (vector<string>::const_iterator j = hashSigs.begin();
			j != hashSigs.end(); ++j)
	{
		const vector<string> &idsInFilter = (*filters[*j]).getFilterIds();
		for (vector<string>::const_iterator i = idsInFilter.begin();
				i != idsInFilter.end(); ++i)
		{
			hits[*i] = 0;
		}
	}
}

///*
// * return results of hits to be output for read status
// */
//const string BioBloomClassifier::getReadStatStr(string const &readID,
//		size_t readLength, unordered_map<string, double> &hits)
//{
//	stringstream str;
//	str << readID << "\t" << readLength;
//	//print readID
//	for (vector<string>::const_iterator j = hashSigs.begin();
//			j != hashSigs.end(); ++j)
//	{
//		//update summary
//		const vector<string> &idsInFilter = (*filters[*j]).getFilterIds();
//		for (vector<string>::const_iterator i = idsInFilter.begin();
//				i != idsInFilter.end(); ++i)
//		{
//			//print to file
//			str << "\t" << hits[*i];
//		}
//	}
//	str << "\n";
//	return str.str();
//}
//
///*
// * return results of hits to be output for read status for paired end mode
// */
//const string BioBloomClassifier::getReadStatStrPair(string const &readID,
//		size_t readLength1, size_t readLength2,
//		unordered_map<string, double> &hits1,
//		unordered_map<string, double> &hits2)
//{
//	stringstream str;
//	str << readID << "\t" << readLength1 << "|" << readLength2;
//	//print readID
//	for (vector<string>::const_iterator j = hashSigs.begin();
//			j != hashSigs.end(); ++j)
//	{
//		//update summary
//		const vector<string> &idsInFilter = (*filters[*j]).getFilterIds();
//		for (vector<string>::const_iterator i = idsInFilter.begin();
//				i != idsInFilter.end(); ++i)
//		{
//			//print to file
//			str << "\t" << hits1[*i] << "|" << hits2[*i];
//		}
//	}
//	str << "\n";
//	return str.str();
//}

BioBloomClassifier::~BioBloomClassifier()
{
}

