// Copyright (c) 2019 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDEBUGGER_H
#define BITCOIN_TXDEBUGGER_H

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

// this class should not be used directly, it should only be managed by an
// instance of CValidationDebugger
class CInputData
{
public:
    // is this input valid
    bool isValid;
    // vector of errors for this input
    std::vector<std::string> errors;
    // none error data about this input
    std::map<std::string, std::string> metadata;

public:
    CInputData() { SetNull(); }
    void SetNull()
    {
        isValid = false;
        errors.clear();
        metadata.clear();
    }
};

// this class should not be used directly, it should only be managed by an
// instance of CValidationDebugger
class CInputDebugger
{
public:
    // are the inputs valid with this set of flags
    bool isValid;
    // internal input traking number
    uint32_t index;
    // vector of inputs
    std::vector<CInputData> vData;

public:
    CInputDebugger() { SetNull(); }
    void SetNull()
    {
        isValid = true;
        index = 0;
        vData.clear();
    }
    /**
     * move internal input tracking number to next input
     *
     * @param none
     * @return none
     */
    void IncrementIndex() { index = index + 1; }
    /**
     * Add a reason the mempool would reject the input at vData[index]
     *
     * @param string
     * @return none
     */
    void AddError(const std::string &strRejectReasonIn)
    {
        if (index >= vData.size())
        {
            vData.push_back(CInputData());
        }
        vData[index].errors.push_back(strRejectReasonIn);
    }
    /**
     * Add metadata (key, value) for the input at Vdata[index]
     *
     * @param string, string
     * @return none
     */
    void AddMetadata(const std::string &keyIn, const std::string &valueIn)
    {
        if (index >= vData.size())
        {
            vData.push_back(CInputData());
        }
        vData[index].metadata.emplace(keyIn, valueIn);
    }
    /**
     * set the validity of the input at vData[index]
     *
     * @param bool
     * @return none
     */
    void SetInputDataValidity(bool state) { vData[index].isValid = state; }
};

/** Capture information about block/transaction validation */
class CValidationDebugger
{
public:
    // is the tx mineable right now
    bool mineable;

    // is the tx mineable at some point in the future
    bool futureMineable;

    // is the tx standard if it needs to be?
    bool standard;

    // a map for tx any tx data that isnt directly related to its inputs
    std::map<std::string, std::string> txMetadata;

    // the tx's hash
    std::string txid;

private:
    enum mode_state
    {
        MODE_VALID, //! everything ok
        MODE_INVALID, //! network rule violation (DoS value may be set)
    } mode;

    // internal counter used to keep track of which input check is being performed
    uint8_t inputSession;

    // information about the inputs
    CInputDebugger inputsCheck1; // using standard flags
    CInputDebugger inputsCheck2; // using mandatory flags

    // reasons this tx would be rejected,
    // this is private because the method to add a reason modifies mode
    std::vector<std::string> strRejectReasons;

public:
    CValidationDebugger()
    {
        mode = MODE_VALID;
        strRejectReasons.clear();
        txMetadata.clear();
        txid = "";
        mineable = true;
        futureMineable = true;
        standard = true;
        inputSession = 1;
        inputsCheck1.SetNull();
        inputsCheck2.SetNull();
    }

    /**
     * Add a reason why this tx would be rejected by the mempool,
     * set mode to invalid
     *
     * @param a string stating why the tx would be rejected by the mempool
     * @return none
     */
    void AddInvalidReason(const std::string &strRejectReasonIn)
    {
        strRejectReasons.push_back(strRejectReasonIn);
        mode = MODE_INVALID;
    }

    /**
     * get all of the reject reasons
     *
     * @param none
     * @return vector of strings where each string is a reason the tx would be rejected
     */
    std::vector<std::string> GetRejectReasons() const { return strRejectReasons; }
    /**
     * Returns a bool stating if the tx is valid
     *
     * @param none
     * @return bool
     */
    bool IsValid() const { return mode == MODE_VALID; }
    /**
     * Returns a bool stating if the tx is invalid
     *
     * @param none
     * @return bool
     */
    bool IsInvalid() const { return mode == MODE_INVALID; }
    /**
     * sets isValid for a set of input checks
     *
     * @param bool
     * @return none
     */
    void SetInputCheckResult(bool state)
    {
        DbgAssert(inputSession < 3, );
        if (inputSession == 1)
        {
            inputsCheck1.isValid = state;
        }
        if (inputSession == 2)
        {
            inputsCheck2.isValid = state;
        }
    }

    /**
     * Adds an error stating why the tx inputs are invalid
     *
     * @param string
     * @return none
     */
    void AddInputCheckError(const std::string &strRejectReasonIn)
    {
        DbgAssert(inputSession < 3, );
        if (inputSession == 1)
        {
            inputsCheck1.AddError(strRejectReasonIn);
        }
        if (inputSession == 2)
        {
            inputsCheck2.AddError(strRejectReasonIn);
        }
    }

    /**
     * Adds metadata information about the transactions inputs
     *
     * @param string, string
     * @return none
     */
    void AddInputCheckMetadata(const std::string &keyIn, const std::string &valueIn)
    {
        DbgAssert(inputSession < 3, );
        if (inputSession == 1)
        {
            inputsCheck1.AddMetadata(keyIn, valueIn);
        }
        if (inputSession == 2)
        {
            inputsCheck2.AddMetadata(keyIn, valueIn);
        }
    }

    /**
     * Sets validity for a single tx input
     *
     * @param bool
     * @return none
     */
    void SetInputCheckValidity(bool state)
    {
        DbgAssert(inputSession < 3, );
        if (inputSession == 1)
        {
            inputsCheck1.SetInputDataValidity(state);
        }
        if (inputSession == 2)
        {
            inputsCheck2.SetInputDataValidity(state);
        }
    }

    /**
     * Increments internal counter for which input is being checked in the tx's
     * set of inputs
     *
     * @param none
     * @return none
     */
    void IncrementCheckIndex()
    {
        DbgAssert(inputSession < 3, );
        if (inputSession == 1)
        {
            inputsCheck1.IncrementIndex();
        }
        if (inputSession == 2)
        {
            inputsCheck2.IncrementIndex();
        }
    }

    /**
     * Increment internal input check counter
     *
     * @param none
     * @return none
     */
    void FinishCheckInputSession() { inputSession = inputSession + 1; }
    /**
     * Gets validity for first round of input checks
     *
     * @param none
     * @return bool
     */
    bool InputsCheck1IsValid() { return inputsCheck1.isValid; }
    /**
     * Gets validity for second round of input checks
     *
     * @param none
     * @return bool
     */
    bool InputsCheck2IsValid() { return inputsCheck2.isValid; }
    /**
     * Gets first round of input check results, used only for printing the information
     *
     * @param none
     * @return CInputDebugger
     */
    CInputDebugger GetInputCheck1() { return inputsCheck1; }
    /**
     * Gets second round of input check results, used only for printing the information
     *
     * @param none
     * @return CInputDebugger
     */
    CInputDebugger GetInputCheck2() { return inputsCheck2; }
};

#endif
