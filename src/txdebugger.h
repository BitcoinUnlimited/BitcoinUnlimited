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
    bool isValid;
    std::vector<std::string> errors;
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
    bool isValid;
    uint32_t index;
    std::vector<CInputData> vData;

public:
    CInputDebugger() { SetNull(); }
    void SetNull()
    {
        isValid = true;
        index = 0;
        vData.clear();
    }
    void IncrementIndex() { index = index + 1; }
    void AddError(const std::string &strRejectReasonIn) { vData[index].errors.push_back(strRejectReasonIn); }
    void AddMetadata(const std::string &keyIn, const std::string &valueIn)
    {
        vData[index].metadata.emplace(keyIn, valueIn);
    }
    void SetInputDataValidity(bool state) { vData[index].isValid = state; }
};

/** Capture information about block/transaction validation */
class CValidationDebugger
{
public:
    bool mineable;
    bool futureMineable;
    bool standard;
    std::map<std::string, std::string> txMetadata;
    std::string txid;

private:
    enum mode_state
    {
        MODE_VALID, //! everything ok
        MODE_INVALID, //! network rule violation (DoS value may be set)
    } mode;
    uint8_t inputSession;
    CInputDebugger inputsCheck1;
    CInputDebugger inputsCheck2;
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
    void AddInvalidReason(const std::string &strRejectReasonIn)
    {
        strRejectReasons.push_back(strRejectReasonIn);
        mode = MODE_INVALID;
    }
    std::vector<std::string> GetRejectReasons() const { return strRejectReasons; }
    bool IsValid() const { return mode == MODE_VALID; }
    bool IsInvalid() const { return mode == MODE_INVALID; }
    void SetInputCheckResult(bool state)
    {
        if (inputSession == 1)
        {
            inputsCheck1.isValid = state;
        }
        if (inputSession == 2)
        {
            inputsCheck2.isValid = state;
        }
    }
    void AddInputCheckError(const std::string &strRejectReasonIn)
    {
        if (inputSession == 1)
        {
            inputsCheck1.AddError(strRejectReasonIn);
        }
        if (inputSession == 2)
        {
            inputsCheck2.AddError(strRejectReasonIn);
        }
    }
    void AddInputCheckMetadata(const std::string &keyIn, const std::string &valueIn)
    {
        if (inputSession == 1)
        {
            inputsCheck1.AddMetadata(keyIn, valueIn);
        }
        if (inputSession == 2)
        {
            inputsCheck2.AddMetadata(keyIn, valueIn);
        }
    }
    void SetInputCheckValidity(bool state)
    {
        if (inputSession == 1)
        {
            inputsCheck1.SetInputDataValidity(state);
        }
        if (inputSession == 2)
        {
            inputsCheck2.SetInputDataValidity(state);
        }
    }
    void IncrementCheckIndex()
    {
        if (inputSession == 1)
        {
            inputsCheck1.IncrementIndex();
        }
        if (inputSession == 2)
        {
            inputsCheck2.IncrementIndex();
        }
    }
    void FinishCheckInputSession()
    {
        inputSession = inputSession + 1;
        DbgAssert(inputSession < 3, );
    }
    bool InputsCheck1IsValid() { return inputsCheck1.isValid; }
    bool InputsCheck2IsValid() { return inputsCheck2.isValid; }
    CInputDebugger GetInputCheck1() { return inputsCheck1; }
    CInputDebugger GetInputCheck2() { return inputsCheck2; }
};

#endif
