// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VALIDATION_H
#define BITCOIN_CONSENSUS_VALIDATION_H

#include <map>
#include <string>
#include <vector>

/** "reject" message codes */
static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
static const unsigned char REJECT_DUST = 0x41;
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;
static const unsigned char REJECT_WAITING = 0x44;

/** Capture information about block/transaction validation */
class CValidationState
{
private:
    enum mode_state
    {
        MODE_VALID, //! everything ok
        MODE_INVALID, //! network rule violation (DoS value may be set)
        MODE_ERROR, //! run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned int chRejectCode;
    bool corruptionPossible;
    std::string strDebugMessage;

public:
    CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(0), corruptionPossible(false) {}
    bool DoS(int level,
        bool ret = false,
        unsigned int chRejectCodeIn = 0,
        const std::string &strRejectReasonIn = "",
        bool corruptionIn = false,
        const std::string &strDebugMessageIn = "")
    {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        strDebugMessage = strDebugMessageIn;
        if (mode == MODE_ERROR)
            return ret;
        nDoS += level;
        mode = MODE_INVALID;
        return ret;
    }
    bool Invalid(bool ret = false,
        unsigned int _chRejectCode = 0,
        const std::string &_strRejectReason = "",
        const std::string &_strDebugMessage = "")
    {
        return DoS(0, ret, _chRejectCode, _strRejectReason, false, _strDebugMessage);
    }
    bool Error(const std::string &strRejectReasonIn)
    {
        if (mode == MODE_VALID)
            strRejectReason = strRejectReasonIn;
        mode = MODE_ERROR;
        return false;
    }
    bool IsValid() const { return mode == MODE_VALID; }
    bool IsInvalid() const { return mode == MODE_INVALID; }
    bool IsError() const { return mode == MODE_ERROR; }
    bool IsInvalid(int &nDoSOut) const
    {
        if (IsInvalid())
        {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }
    bool CorruptionPossible() const { return corruptionPossible; }
    unsigned int GetRejectCode() const { return chRejectCode; }
    std::string GetRejectReason() const { return strRejectReason; }
    std::string GetDebugMessage() const { return strDebugMessage; }
    void SetDebugMessage(const std::string &s) { strDebugMessage = s; }
};

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
    std::vector<std::string> strRejectReasons;
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
    void AddTxid(const std::string txidIn) { txid = txidIn; }
    std::string GetTxid() { return txid; }
    bool AddInvalidReason(const std::string &strRejectReasonIn)
    {
        strRejectReasons.push_back(strRejectReasonIn);
        mode = MODE_INVALID;
        return false;
    }
    void AddMetadata(const std::string &keyIn, const std::string &valueIn) { txMetadata.emplace(keyIn, valueIn); }
    bool GetMineable() { return mineable; }
    void SetMineable(bool state) { mineable = state; }
    bool GetFutureMineable() { return futureMineable; }
    void SetFutureMineable(bool state) { futureMineable = state; }
    bool GetStandard() { return standard; }
    void SetStandard(bool state) { standard = state; }
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
    void FinishCheckInputSession() { inputSession = inputSession + 1; }
    bool InputsCheck1IsValid() { return inputsCheck1.isValid; }
    bool InputsCheck2IsValid() { return inputsCheck2.isValid; }
    CInputDebugger GetInputCheck1() { return inputsCheck1; }
    CInputDebugger GetInputCheck2() { return inputsCheck2; }
    bool IsValid() const { return mode == MODE_VALID; }
    bool IsInvalid() const { return mode == MODE_INVALID; }
    std::vector<std::string> GetRejectReasons() const { return strRejectReasons; }
};

#endif // BITCOIN_CONSENSUS_VALIDATION_H
