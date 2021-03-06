/*
 * BioBloomCategorizer.cpp
 *
 *  Created on: Sep 7, 2012
 *      Author: cjustin
 */
#include <sstream>
#include <string>
#include <getopt.h>
#include <iostream>
#include "boost/unordered/unordered_map.hpp"
#include <vector>
#include <sys/stat.h>
#include "BioBloomClassifier.h"
#include "DataLayer/Options.h"
#include "config.h"
#include "Common/Options.h"
#if _OPENMP
# include <omp.h>
#endif

using namespace std;

#define PROGRAM "biobloomcategorizer"

namespace opt {
/** The number of parallel threads. */
static unsigned threads = 1;
}

void printVersion()
{
	const char VERSION_MESSAGE[] = PROGRAM " (" PACKAGE_NAME ") " VERSION "\n"
	"Written by Justin Chu.\n"
	"\n"
	"Copyright 2013 Canada's Michael Smith Genome Science Centre\n";
	cerr << VERSION_MESSAGE << endl;
	exit(EXIT_SUCCESS);
}

/*
 * Parses input string into separate strings, returning a vector.
 */
vector<string> convertInputString(const string &inputString)
{
	vector<string> currentInfoFile;
	string temp;
	stringstream converter(inputString);
	while (converter >> temp) {
		currentInfoFile.push_back(temp);
	}
	return currentInfoFile;
}

void folderCheck(const string &path)
{
	struct stat sb;

	if (stat(path.c_str(), &sb) == 0) {
		if (!S_ISDIR(sb.st_mode)) {
			cerr << "Error: Output folder - file exists with this name. "
					<< path << endl;
			exit(1);
		}
	} else {
		cerr << "Error: Output folder does not exist. " << path << endl;
		exit(1);
	}
}

void printHelpDialog()
{
	const char dialog[] =
	"Usage: biobloomcategorizer [OPTION]... -f \"[FILTER1]...\" [FILE]...\n"
	"biobloomcategorizer [OPTION]... -e -f \"[FILTER1]...\" [FILE1.fq] [FILE2.fq]\n"
	"Categorize Sequences. The input format may be FASTA, FASTQ, qseq, export, SAM or\n"
	"BAM format and compressed with gz, bz2 or xz and may be tarred.\n"
	"\n"
	"  -p, --prefix=N         Output prefix to use. Otherwise will output to current\n"
	"                         directory.\n"
	"  -f, --filter_files=N   List of filter files to use. Required option. \n"
	"                         eg. \"filter1.bf filter2.bf\"\n"
	"  -e, --paired_mode      Uses paired-end information. For BAM or SAM files, if\n"
	"                         they are poorly ordered, the memory usage will be much\n"
	"                         larger than normal. Sorting by read name may be needed.\n"
	"  -i, --inclusive        If one paired read matches, both reads will be included\n"
	"                         in the filter. \n"
	"  -s, --score=N          Score threshold for matching. Maximum threshold is 1\n"
	"                         (highest specificity), minimum is 0 (highest\n"
	"                         sensitivity). A lower score threshold will decrease run\n"
	"                         time. If set to 1, best hit is used rather than\n"
	"                         threshold and score will appended to the header of the\n"
	"                         output read.[0.15]\n"
	"  -w, --with_score       Output multimatches with scores in the order of filter.\n"
	"  -t, --threads=N        The number of threads to use. [1]\n"
	"  -g, --gz_output        Outputs all output files in compressed gzip.\n"
	"      --fa               Output categorized reads in Fasta files.\n"
	"      --fq               Output categorized reads in Fastq files.\n"
	"      --chastity         Discard and do not evaluate unchaste reads.\n"
	"      --no-chastity      Do not discard unchaste reads. [default]\n"
	"  -l  --length_cutoff=N  Discard reads shorter that the cutoff N. [0]\n"
	"  -v  --version          Display version information.\n"
	"  -h, --help             Display this dialog.\n"
	"Advanced options:\n"
	"  -m, --min_hit=N        Minimum Hit Threshold Value. The absolute hit number\n"
	"                         needed over initial tiling of read to continue. Higher\n"
	"                         values decrease runtime but lower sensitivity.[0]\n"
	"  -r, --streak=N         The number of hit tiling in second pass needed to jump\n"
	"                         Several tiles upon a miss. Small values decrease\n"
	"                         runtime but decrease sensitivity. [3]\n"
	"  -o, --min_hit_only     Use only initial pass filtering to evaluate reads. Fast\n"
	"                         but low specificity, use only on long reads (>100bp).\n"
	"  -c, --ordered          Use ordered filtering. Order of filters matters\n"
	"                         (filters listed first have higher priority). Only taken\n"
	"                         advantage of when k-mer sizes and number of hash\n"
	"                         functions are the same.\n"
	"  -d, --stdout_filter=N  Outputs all matching reads to stdout for the specified\n"
	"                         filter. N is the filter ID without file extension.\n"
	"                         Reads are outputed in fastq, and if paired will output\n"
	"                         will be interlaced.\n"
	"Report bugs to <cjustin@bcgsc.ca>.";

	cerr << dialog << endl;
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	opt::chastityFilter = 0;
	opt::trimMasked = 0;

	//switch statement variable
	int c;

	//control variables
	bool die = false;

	//command line variables
	string rawInputFiles = "";
	string outputPrefix = "";
	string filtersFile = "";
	string outputReadType = "";
	bool paired = false;
	bool inclusive = false;

	int fastq = 0;
	int fasta = 0;
	string filePostfix = "";
	double score = 0.15;
	bool withScore = false;

	//advanced options
	unsigned minHit = 0;
	bool minHitOnly = false;
	bool collab = false;

	string mainFilter = "";

	//long form arguments
	static struct option long_options[] = { {
		"prefix", optional_argument, NULL, 'p' }, {
		"filter_files", required_argument, NULL, 'f' }, {
		"paired_mode", no_argument, NULL, 'e' }, {
		"inclusive", no_argument, NULL, 'i' }, {
		"score", no_argument, NULL, 's' }, {
		"help", no_argument, NULL, 'h' }, {
		"threads", required_argument, NULL, 't' }, {
		"gz_output", no_argument, NULL, 'g' }, {
		"chastity", no_argument, &opt::chastityFilter, 1 }, {
		"no-chastity", no_argument, &opt::chastityFilter, 0 }, {
		"fq", no_argument, &fastq, 1 }, {
		"fa", no_argument, &fasta, 1 }, {
		"length_cutoff", required_argument, NULL, 'l' }, {
		"version", no_argument, NULL, 'v' }, {
		"min_hit_thr", required_argument, NULL, 'm' }, {
		"streak", optional_argument, NULL, 'r' }, {
		"min_hit_only", no_argument, NULL, 'o' }, {
		"ordered", no_argument, NULL, 'c' }, {
		"stdout_filter", required_argument, NULL, 'd' }, {
		"with_score", no_argument, NULL, 'w' }, {
		NULL, 0, NULL, 0 } };

	//actual checking step
	//Todo: add checks for duplicate options being set
	int option_index = 0;
	while ((c = getopt_long(argc, argv, "f:m:p:hegl:vs:or:t:cd:iw", long_options,
			&option_index)) != -1)
	{
		istringstream arg(optarg != NULL ? optarg : "");
		switch (c) {
		case 'm': {
			stringstream convert(optarg);
			if (!(convert >> minHit)) {
				cerr << "Error - Invalid parameter! m: " << optarg << endl;
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 's': {
			stringstream convert(optarg);
			if (!(convert >> score)) {
				cerr << "Error - Invalid parameter! s: " << optarg << endl;
				exit(EXIT_FAILURE);
			}
			if (score < 0 || score > 1) {
				cerr << "Error - s must be between 0 and 1, input given:"
						<< optarg << endl;
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'f': {
			filtersFile = optarg;
			break;
		}
		case 'p': {
			outputPrefix = optarg;
			break;
		}
		case 'h': {
			printHelpDialog();
			break;
		}
		case 'e': {
			paired = true;
			break;
		}
		case 'i': {
			inclusive = true;
			break;
		}
		case 't': {
			stringstream convert(optarg);
			if (!(convert >> opt::threads)) {
				cerr << "Error - Invalid parameter! t: " << optarg << endl;
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'g': {
			filePostfix = ".gz";
			break;
		}
		case 'l': {
			stringstream convert(optarg);
			if (!(convert >> opt::minLength)) {
				cerr << "Error - Invalid parameter! l: " << optarg << endl;
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'v': {
			printVersion();
			break;
		}
		case 'o': {
			minHitOnly = true;
			break;
		}
		case 'r': {
			stringstream convert(optarg);
			if (!(convert >> opt::streakThreshold)) {
				cerr << "Error - Invalid parameter! r: " << optarg << endl;
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'c': {
			collab = true;
			break;
		}
		case 'd': {
			mainFilter = optarg;
			break;
		}
		case 'w': {
			withScore = true;
			break;
		}
		case '?': {
			die = true;
			break;
		}
		}
	}

#if defined(_OPENMP)
	if (opt::threads > 0)
	omp_set_num_threads(opt::threads);
#endif

	vector<string> filterFilePaths = convertInputString(filtersFile);
	vector<string> inputFiles = convertInputString(rawInputFiles);

	while (optind < argc) {
		inputFiles.push_back(argv[optind]);
		optind++;
	}

	bool pairedBAMSAM = false;

	//check validity of inputs for paired end mode
	if (paired) {
		if (inputFiles.size() == 1
				&& (inputFiles[0].substr(inputFiles[0].size() - 4) == ".bam"
						|| inputFiles[0].substr(inputFiles[0].size() - 4)
								== ".sam"))
		{
			pairedBAMSAM = true;
		} else if (inputFiles.size() == 2) {
			pairedBAMSAM = false;
		} else {
			cerr << "Usage of paired end mode:\n"
					<< "BioBloomCategorizer [OPTION]... -f \"[FILTER1]...\" [FILEPAIR1] [FILEPAIR2]\n"
					<< "or BioBloomCategorizer [OPTION]... -f \"[FILTER1]...\" [PAIREDBAMSAM]\n"
					<< endl;
			exit(1);
		}
	}

	//Check needed options
	if (inputFiles.size() == 0) {
		cerr << "Error: Need Input File" << endl;
		die = true;
	}
	if (filterFilePaths.size() == 0) {
		cerr << "Error: Need Filter File (-f)" << endl;
		die = true;
	}
	if (die) {
		cerr << "Try '--help' for more information.\n";
		exit(EXIT_FAILURE);
	}
	//check if output folder exists
	if (outputPrefix.find('/') != string::npos) {
		string tempStr = outputPrefix.substr(0, outputPrefix.find_last_of("/"));
		folderCheck(tempStr);
	}

	//set file output type
	if (fastq && fasta) {
		cerr
				<< "Error: fasta (--fa) and fastq (--fq) outputs types cannot be both set"
				<< endl;
		exit(1);
	} else if (fastq) {
		outputReadType = "fq";
	} else if (fasta) {
		outputReadType = "fa";
	}

	//-w option cannot be used without output method
	if (withScore && (outputReadType == "")) {
		cerr << "Error: -w option cannot be used without output method" << endl;
		exit(1);
	}

	//load filters
	BioBloomClassifier BBC(filterFilePaths, score, outputPrefix, filePostfix,
			minHit, minHitOnly, withScore);

	if (collab && minHit) {
		cerr << "Error: -m -c outputs types cannot be both set" << endl;
		exit(1);
	} else if (collab) {
		BBC.setCollabFilter();
	}

	if (mainFilter != "") {
		BBC.setMainFilter(mainFilter);
	}

	//filtering step
	//create directory structure if it does not exist
	if (paired) {
		if (inclusive) {
			BBC.setInclusive();
		}
		if (outputReadType != "") {
			if (pairedBAMSAM) {
				BBC.filterPairBAMPrint(inputFiles[0], outputReadType);
			} else {
				BBC.filterPairPrint(inputFiles[0], inputFiles[1],
						outputReadType);
			}
		} else {
			if (pairedBAMSAM) {
				BBC.filterPairBAM(inputFiles[0]);
			} else {
				BBC.filterPair(inputFiles[0], inputFiles[1]);
			}
		}
	} else {
		if (outputReadType != "") {
			BBC.filterPrint(inputFiles, outputReadType);
		} else {
			BBC.filter(inputFiles);
		}
	}
}
