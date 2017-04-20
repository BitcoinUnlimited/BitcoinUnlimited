// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FORKS_CSV
#define BITCOIN_FORKS_CSV

#include <fstream>

#include "consensus/params.h"
#include "versionbits.h"

extern const char * const FORKS_CSV_FILENAME;  // from util.cpp


/**
 * Reads the CSV file and updates data in the consensus params.
 * Returns true if the data validated correctly, or false if any validation errors.
 * Validation errors should result in caller aborting safely rather than
 * proceeding on possibly incomplete fork data.
 */
extern bool ReadForksCsv(std::string activeNetworkID, std::istream& csvInput, Consensus::Params& consensusParams);

/**
 * individual deployment line item validation functions.
 * Each function returns true if items checked are ok, false otherwise.
 */
const bool ValidateNetwork(const std::string& networkname);
const bool ValidateForkName(const std::string& forkname);
const bool ValidateGBTForce(const std::string& gbtforce);
const bool ValidateBit(const int bit);
const bool ValidateWindowSize(const int windowsize);
const bool ValidateThreshold(const int threshold, const int window);
const bool ValidateTimes(const int64_t starttime, const int64_t timeout);
const bool ValidateMinLockedBlocks(const int minlockedblocks);
const bool ValidateMinLockedTime(const int64_t minlockedtime);

/**
 * Validate the deployment parameters for the entire network altogether.
 * This can catch things like repeated name / bits, overlapping deployment times
 * etc.
 */
const bool ValidateOverallParams(const std::string& checkNetworkID);

/**
 * Print an error message if validation of a line fails on an item.
 * This is logged both to file and to stderr to alert the operator.
 */
void LineValidationError(std::string errmsg);

#endif
