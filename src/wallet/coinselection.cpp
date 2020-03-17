#include "../tweak.h"
#include "random.h"
#include "wallet.h"

// needed for tx validation
#include "../main.h"
#include "../script/sign.h"
#include "../txmempool.h"
const int MAX_SOLUTIONS = 2000; // The approximate maximum number of solutions we will find before giving up.
// What's the maximum number of TXOs we should use as inputs to a transaction (if we have a choice).
const int MAX_ELECTIVE_TXOS = 5;
const int MAX_TXOS = 100; // What's the maximum number of TXOs to put in a transaction
// how many satoshi's the coin selection is willing to be off by per iteration through the loop.  Makes it progressively
// easier to find a solution
// (now its a simple exponential) const long int LOOP_COST = 500;
const int P2PKH_INPUT_SIZE = 72; // <Sig> <PubKey> OP_DUP OP_HASH160 <PubkeyHash> OP_EQUALVERIFY OP_CHECKSIG

extern CTweak<unsigned int> maxCoinSelSearchTime;
extern CTweak<unsigned int> preferredNumUTXO;

// This holds a sorted map of coin amounts and the set of Txos required to get to that amount
typedef std::multimap<CAmount, TxoGroup> TxoGroupMap;

// How close do we want to get to targetValue (at first).  The longer it takes the more we add to this value.
CAmount reasonableExcess(CAmount targetValue) { return targetValue / 1024; }
// Create a group of Txos from a Txo index and another group.
TxoGroup makeGroup(SpendableTxos::iterator i, const TxoGroup *prev = nullptr)
{
    TxoGroup ret;
    if (prev != nullptr)
        ret = *prev;
    else
        ret.first = 0;
    // Don't add an element that already exists  -- allow this, bad solutions eliminated later
    // assert(ret.second.find(i) == ret.second.end());
    ret.first += i->first; // update the amount
    COutput outp = i->second;
    // verify that the output value is consistent -- TODO: Check pcoin and make sure it doesn't get deleted -- located
    // in mapWallet
    assert(outp.tx->vout[outp.i].nValue == i->first);
    ret.second.insert(i); // add this txo to the set
    return ret;
}

// Take a group that is not a solution and find a bunch of solutions by appending additional TXOs onto it.
// This is a recursive function that is limited by MAX_TXOs and MAX_ELECTIVE_TXOS
bool extendCoinSelectionSolution(const CAmount targetValue,
    /*const*/ SpendableTxos &available,
    TxoGroup grp,
    TxoGroupMap &solutions,
    int depth)
{
    if (depth >= MAX_TXOS)
        return false; // unambiguously break the recursion
    SpendableTxos::iterator aEnd = available.end(); // For speed, get these once
    SpendableTxos::iterator aBeg = available.begin();
    bool ret = false;
    SpendableTxos::iterator small;
    small = available.lower_bound(targetValue - grp.first); // Find a value at or above what we need

    {
        SpendableTxos::iterator i = small;
        // while not at the end and there's too little value or we already used this output then keep going.
        // while((i != aEnd) && ((i->first + grp.first < targetValue) || (grp.second.find(i) != grp.second.end()))  )
        // allow dups and eliminate later for performance
        while ((i != aEnd) && (i->first + grp.first < targetValue)) // iterate to make sure it is actually big enough.
        {
            ++i;
        }

        if (i != aEnd) // Ok we have a solution so add it.
        {
            TxoGroup g = makeGroup(i, &grp);
            solutions.insert(TxoGroupMap::value_type(g.first, g));
            ret = true;
        }
    }

    if (small == aEnd)
        --small;
    for (int count = 0; count < 5; count++) // check 5 more close values
    {
        // lower_bound returns an element >= the passed value, so get one that is smaller
        while ((small != aBeg) && (small->first + grp.first > targetValue))
            --small;
        // keep looking if there are no solutions or if the depth is low
        if ((solutions.empty() || (depth < MAX_ELECTIVE_TXOS)))
        {
            // Its better performance to accidentally add a double than to check every time
            // go to the prev if this one is already in the group.
            // while ((small != aBeg) && (grp.second.find(small) != grp.second.end())) --small;
            // if (small != aBeg)
            {
                TxoGroup newGrp = makeGroup(small, &grp);
                ret |= extendCoinSelectionSolution(targetValue, available, newGrp, solutions, depth + 1);
            }
        }

        if (small == aBeg)
            break;
    }
    return ret;
}

// Make sure that the group sums to >= the target value.
// Since the TxoGroup is a set, identical Txos are eliminated meaning that this may not sum, and that the
// TxoGroup->first may be inaccurate.
// There may be identical Txos because the search algorithm does not eliminate used txos from the search set for
// efficiency.
// Also check various other aspects that would block the transaction from being accepted into the mempool.
bool validate(const TxoGroup &grp, const CAmount targetValue)
{
    CAmount acc = 0;
    int nTx = grp.second.size();
    for (TxoItVec::iterator i = grp.second.begin(); i != grp.second.end(); ++i)
    {
        acc += (*i)->first;
    }
    if (acc < targetValue)
        return false;

    std::vector<CTxIn> txIn(nTx);
    int count = 0;
    for (TxoItVec::iterator i = grp.second.begin(); i != grp.second.end(); ++i, ++count)
    {
        COutput &tmp = (*i)->second;
        txIn[count] = CTxIn(tmp.tx->GetHash(), tmp.i);
    }

    size_t limitAncestorCount = GetArg("-limitancestorcount", BU_DEFAULT_ANCESTOR_LIMIT);
    size_t limitAncestorSize = GetArg("-limitancestorsize", BU_DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
    size_t limitDescendantsCount = GetArg("-limitdescendantcount", BU_DEFAULT_DESCENDANT_LIMIT);
    size_t limitDescendantSize = GetArg("-limitdescendantsize", BU_DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
    std::string errString;

    READLOCK(mempool.cs_txmempool);
    bool ret = mempool.ValidateMemPoolAncestors(
        txIn, limitAncestorCount, limitAncestorSize, limitDescendantsCount, limitDescendantSize, errString);
    if (!ret)
    {
        LOG(SELECTCOINS, "CoinSelection eliminated a solution, error: %s\n", errString.c_str());
    }
    return ret;
}

// Select coins.
TxoGroup CoinSelection(/*const*/ SpendableTxos &available,
    const CAmount targetValue,
    const CAmount &dust,
    CFeeRate feeRate,
    unsigned int changeLen)
{
    FastRandomContext insecureRand;
    uint64_t utxosInWallet = available.size();
    SpendableTxos::iterator aEnd = available.end();
    SpendableTxos::iterator aBegin = available.begin();
    TxoGroupMap solutions;
    TxoGroupMap candidates;

    LOG(SELECTCOINS, "CoinSelection: Target: %d, num available txos: %d\n", targetValue, available.size());

    // Find the smallest output > TargetValue & add it to solutions (if it exists).
    SpendableTxos::iterator large;
    large = available.lower_bound(targetValue); // Find the elem nearest the target but greater or =
    // The amount is bigger than our biggest output.  We'll create a simple solution from a set of our biggest outputs.
    if (large == aEnd)
    {
        if (large == aBegin) // beginning == ending, there is nothing
        {
            return TxoGroup(0, TxoItVec());
        }
        --large;
        SpendableTxos::iterator i = large;
        CAmount total = large->first;
        TxoGroup group = makeGroup(i);
        while ((total < targetValue) && (i != aBegin))
        {
            --i;
            total += i->first;
            group = makeGroup(i, &group);
        }
        if (total >= targetValue)
        {
            solutions.insert(TxoGroupMap::value_type(total, group));
        }
        else // We added every transaction and it did not sum to totalValue, so no solution.
        {
            LOG(SELECTCOINS, "Every available UTXO sums to %llu which is lower than the target %llu\n", total,
                targetValue);
            return TxoGroup(0, TxoItVec());
        }
    }
    else // otherwise, the next one could be a solution
    {
        SpendableTxos::iterator i = large;
        if (i != aEnd)
        {
            assert(i->first >= targetValue);
            if (i->first == targetValue) // If its equal then done
            {
                return makeGroup(i);
            }
            // If its greater then add it to the solutions list.
            solutions.insert(TxoGroupMap::value_type(i->first, makeGroup(i)));
        }
    }

    // Now iterate looking for better solutions.
    while ((large->first > targetValue) && (large != available.begin()))
        --large; // get something <= our target value
    bool done = (large == available.begin());
    long int loop = 0;
    long int excessModifier = 0;
    long int loopCost = 1;
    uint64_t startTime = GetTimeMillis();
    while (!done)
    {
        loopCost++; // for every loop, make it exponentially easier to find an acceptable solutions
        excessModifier += loopCost;

        // Calculate the size of this new input
        const CScript &scriptPubKey = large->second.tx->vout[large->second.i].scriptPubKey;
        CScript scriptSigRes; // = txNew.vin[nIn].scriptSig;
        CWallet dummyWallet;
        bool signSuccess = ProduceSignature(DummySignatureCreator(&dummyWallet), scriptPubKey, scriptSigRes);
        int inputLen = signSuccess ? scriptSigRes.size() : P2PKH_INPUT_SIZE;
        CAmount fee = feeRate.GetFee(inputLen);
        // We will take the "large" txo and decrement it each time through this loop.
        // If large ever hits the beginning, we have checked everything and can quit.
        TxoGroup grp = makeGroup(large); // Make a group out of our current txo
        // And attempt to extend it into solutions (automatically added to the "solutions" object)
        extendCoinSelectionSolution(targetValue + fee, available, grp, solutions, 0);

        TxoGroupMap::iterator i = solutions.begin();

        uint64_t nSolutions = solutions.size();

        // Have we looked for a long time?
        if ((GetTimeMillis() - startTime > maxCoinSelSearchTime.Value()) && (nSolutions >= 1))
        {
            LOG(SELECTCOINS, "CoinSelection searched for the alloted time and found %d solutions\n", nSolutions);
            done = true; // If yes then quit.
        }
        else if (i->first - targetValue <= dust / 2) // Did we find any good solutions?
        {
            LOG(SELECTCOINS, "CoinSelection found a close solution\n");
            done = true; // If yes then quit.
        }
        else if (solutions.size() > MAX_SOLUTIONS) // Did we find lots of solutions?
        {
            LOG(SELECTCOINS, "CoinSelection found many solutions\n");
            done = true; // If yes then quit.
        }
        else // No good solutions, so decrement large
        {
            CAmount a = large->first;
            // Skip all Txos whose value is the exact same as the Txo I just looked at.
            do
            {
                --large;
            } while ((large != available.begin()) && (large->first != a));

            done = (large == available.begin()); // We are done if we get to the beginning.
        }

        if (!done) // Now grab a random Txo and search for a solution near it
        {
            // Looking for a Txo > 1/4 of the needed value
            CAmount r = insecureRand.rand32() % (3 * targetValue / 4) + (targetValue / 4);
            SpendableTxos::iterator rit = available.lower_bound(r);
            if (rit != aEnd)
            {
                TxoGroup grp2 = makeGroup(rit); // Make a group out of it
                // And attempt to extend it for solutions.
                extendCoinSelectionSolution(targetValue, available, grp2, solutions, 0);
                // Did we find any good solutions?
                if (i->first - targetValue < reasonableExcess(targetValue) + excessModifier)
                {
                    done = true; // If yes then quit.
                }
            }
        }

        loop++;
    }

    // Let's see what solutions we found.
    TxoGroupMap::iterator i = solutions.begin();
    done = false;
    TxoGroupMap::iterator end = solutions.end();
    TxoGroupMap::iterator singleIn = end;
    TxoGroupMap::iterator noChange = end;
    int noChangeCount = 0;
    TxoGroupMap::iterator multiIn = end;
    int multiInCount = 0;
    for (i = solutions.begin(); (i != end) && !done; ++i)
    {
        // some bad solutions can occur (repeated elements), it is more efficient to eliminate them here than inside the
        // loops
        if (!validate(i->second, targetValue))
            continue;
        int ntxo = i->second.second.size();
        if (ntxo == 1)
            singleIn = i; // ok this solution works but does not reduce the utxo set
        // look for a solution that won't need to pay change.
        if ((i->first - targetValue <= dust) && (ntxo > noChangeCount))
        {
            noChange = i;
            noChangeCount = ntxo;
            LOG(SELECTCOINS, "CoinSelection found a nochange solution\n");
        }
        if ((ntxo > 1) && (ntxo > multiInCount))
        {
            multiIn = i;
            multiInCount = ntxo;
        }
    }

    if (noChange != end)
        i = noChange; // prefer the best no change solution
    else
    {
        if (utxosInWallet <= preferredNumUTXO.Value()) // Find the cheapest (shortest) solution
        {
            if (singleIn != end)
                i = singleIn;
            else
                i = multiIn;
        }
        else // Reduce UTXO
        {
            if (multiIn != end)
                i = multiIn; // or pick one the reduces the UTXO
            else
                i = singleIn; // ok take whatever I can get
        }
    }

    if (i == solutions.end())
    {
        LOG(SELECTCOINS, "%d solutions found, but none chosen\n", solutions.size());
        return TxoGroup(0, TxoItVec());
    }
    LOG(SELECTCOINS, "CoinSelection returns %d choices. Dust: %d, Target: %d, found: %d, txos: %d\n", solutions.size(),
        dust, targetValue, i->first, i->second.second.size());
    TxoGroup ret = i->second;
    return ret;
}
