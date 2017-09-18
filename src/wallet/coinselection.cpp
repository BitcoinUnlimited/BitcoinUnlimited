#include "wallet.h"
#include "random.h"
#include "../tweak.h"

// needed for tx validation
#include "../main.h"  
#include "../txmempool.h"
#include "../script/sign.h"
const int MAX_SOLUTIONS = 10000;   // The approximate maximum number of solutions we will find before giving up.
const int MAX_ELECTIVE_TXOS = 5;  // What's the maximum number of TXOs we should use as inputs to a transaction (if we have a choice).
const int MAX_TXOS = 100;         // What's the maximum number of TXOs to put in a transaction 
// (now its a simple exponential) const long int LOOP_COST = 500;   // how many satoshi's the coin selection is willing to be off by per iteration through the loop.  Makes it progressively easier to find a solution
const int P2PKH_INPUT_SIZE = 72;    // <Sig> <PubKey> OP_DUP OP_HASH160 <PubkeyHash> OP_EQUALVERIFY OP_CHECKSIG

extern CTweak<unsigned int> maxCoinSelSearchTime;
extern CTweak<unsigned int> preferredNumUTXO;

#if 0 // for reference. Defined in wallet.h
typedef std::multimap<CAmount, COutput> SpendableTxos;
typedef std::set<std::pair<const CWalletTx*,unsigned int> > CoinSet;
typedef std::set<SpendableTxos::const_iterator> TxoItVec;
typedef std::pair<CAmount, TxoItVec > TxoGroup;  // A set of coins and how much they sum to.

struct TxoIterLess // : binary_function <T,T,bool> 
  {
    typedef SpendableTxos::const_iterator T;
    bool operator() (const T& x, const T& y) const {return x->first < y->first;}
  };

typedef std::set<SpendableTxos::iterator,TxoIterLess> TxoItVec;
typedef std::pair<CAmount, TxoItVec > TxoGroup;  // A set of coins and how much they sum to.
#endif

// This holds a sorted map of coin amounts and the set of Txos required to get to that amount
typedef std::multimap<CAmount, TxoGroup> TxoGroupMap;

// How close do we want to get to targetValue (at first).  The longer it takes the more we add to this value.
CAmount reasonableExcess(CAmount targetValue)
{
  return targetValue/1024;
}

// Create a group of Txos from a Txo index and another group.
TxoGroup makeGroup(SpendableTxos::iterator i, const TxoGroup& prev = *(TxoGroup*)NULL)
{
  TxoGroup ret;
  if (&prev) ret = prev;
  else ret.first = 0;
  // assert(ret.second.find(i) == ret.second.end());  // Don't add an element that already exists  -- allow this, bad solutions eliminated later
  ret.first += i->first;  // update the amount 
  COutput outp = i->second;
  assert(outp.tx->vout[outp.i].nValue == i->first);  // verify that the output value is consistent -- TODO: Check pcoin and make sure it doesn't get deleted -- located in mapWallet
  ret.second.insert(i);  // add this txo to the set
  return ret;
}

// Take a group that is not a solution and find a bunch of solutions by appending additional TXOs onto it.
// This is a recursive function that is limited by MAX_TXOs and MAX_ELECTIVE_TXOS
bool extend(const CAmount targetValue, /*const*/ SpendableTxos& available, TxoGroup grp, TxoGroupMap& solutions, int depth)
{
  if (depth >= MAX_TXOS) return false;  // unambiguously break the recursion
  SpendableTxos::iterator aEnd = available.end();  // For speed, get these once
  SpendableTxos::iterator aBeg = available.begin();
  bool ret = false;
  SpendableTxos::iterator small;
  small = available.lower_bound(targetValue - grp.first);  // Find a value at or above what we need
  if (1)
    {
      SpendableTxos::iterator i = small;
      // while not at the end and there's too little value or we already used this output then keep going.
      //while((i != aEnd) && ((i->first + grp.first < targetValue) || (grp.second.find(i) != grp.second.end()))  )  allow dups and eliminate later for performance
      while((i != aEnd) && (i->first + grp.first < targetValue))  // iterate to make sure it is actually big enough.
        {
        ++i;
	}

      if (i!=aEnd)  // Ok we have a solution so add it.
	{
 	  TxoGroup g = makeGroup(i,grp);
  	  solutions.insert(TxoGroupMap::value_type(g.first,g));
          //LogPrint("wallet","CoinSelection solution found: excess %ld, txos: %d\n", g.first - targetValue, g.second.size());
          ret = true;
	}
    }
  for (int i=0; i<5; i++) // check 5 more close values
    {
      if (small == aBeg) break;
      while ((small!=aBeg)&&(small->first + grp.first > targetValue)) --small; // lower_bound returns an element >= the passed value, so get one that is smaller
      if (small == aEnd) break;
      if ((solutions.empty() || (depth < MAX_ELECTIVE_TXOS))) // keep looking if there are no solutions or if the depth is low
        {
          // Its better performance to accidentally add a double than to check every time
          //while ((small != aBeg) && (grp.second.find(small) != grp.second.end())) --small;  // go to the prev if this one is already in the group.
          //if (small != aBeg)
          {
            TxoGroup newGrp = makeGroup(small, grp);
            //LogPrint("wallet","CoinSelection recursive extend: need %ld, txos: %d\n", targetValue - newGrp.first, newGrp.second.size());
            ret |= extend(targetValue, available, newGrp, solutions, depth+1);
          }
        }
    }
  return ret;
}

// Make sure that the group sums to >= the target value.
// Since the TxoGroup is a set, identical Txos are eliminated meaning that this may not sum, and that the TxoGroup->first may be inaccurate.
// There may be identical Txos because the search algorithm does not eliminate used txos from the search set for efficiency.
// Also check various other aspects that would block the transaction from being accepted into the mempool.
bool validate(const TxoGroup& grp,const CAmount targetValue)
{
  CAmount acc = 0;
  int nTx = grp.second.size();
  for (TxoItVec::iterator i = grp.second.begin(); i != grp.second.end(); ++i)
    {
      acc += (*i)->first;
    }
  if (acc < targetValue) return false;

  std::vector<CTxIn> txIn(nTx);
  int count=0;
  for (TxoItVec::iterator i = grp.second.begin(); i != grp.second.end(); ++i,++count)
    {
      COutput& tmp = (*i)->second;
      txIn[count] = CTxIn(tmp.tx->GetHash(),tmp.i);
    }

  size_t limitAncestorCount = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
  size_t limitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
  size_t limitDescendantsCount = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
  size_t limitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
  std::string errString;
  
  READLOCK(mempool.cs);
  bool ret = mempool.ValidateMemPoolAncestors(txIn, limitAncestorCount, limitAncestorSize, limitDescendantsCount, limitDescendantSize, errString);
  if (!ret)
    {
      LogPrint("wallet","CoinSelection eliminated a solution, error: %s\n", errString.c_str());
    }
  return ret;
}

// Select coins.
TxoGroup CoinSelection(/*const*/ SpendableTxos& available, const CAmount targetValue,const CAmount &dust,CFeeRate feeRate, unsigned int changeLen)
{
  uint utxosInWallet = available.size();
  SpendableTxos::iterator aEnd = available.end();
  TxoGroupMap solutions;
  TxoGroupMap candidates;

  LogPrint("wallet","num available txos: %d\n", available.size());

  // Find the smallest output > TargetValue & add it to solutions (if it exists).
  SpendableTxos::iterator large;
  large = available.lower_bound(targetValue);         // Find the elem nearest the target but greater or =
  if (large == aEnd) // The amount is bigger than our biggest output.  We'll create a simple solution from a set of our biggest outputs.
    {
      if (large == available.begin())  // beginning == ending, there is nothing
        {
          return TxoGroup(0,TxoItVec());
        }
      --large;
      SpendableTxos::iterator i = large;
      CAmount total = large->first;
      TxoGroup group = makeGroup(i);
      while (total < targetValue)
	{
	  --i;
          if (i==aEnd) break;
          total += i->first;
          group = makeGroup(i,group);
	}
      if (total >= targetValue)
	{
        solutions.insert(TxoGroupMap::value_type(total,group));
	}
      else // We added every transaction and it did not sum to totalValue, so no solution.
	{
        return TxoGroup(0,TxoItVec());
	}
    }  
  else  // otherwise, the next one could be a solution
    {
      SpendableTxos::iterator i = large;
      if (i != aEnd)
	{
        assert(i->first >= targetValue);
        if (i->first == targetValue)  // If its equal then done
	  {
	    return makeGroup(i);
	  }
        // If its greater then add it to the solutions list.
        solutions.insert(TxoGroupMap::value_type(i->first,makeGroup(i)));
        //bestValue = i->first;
	}      
    }

  // Now iterate looking for better solutions.
  while ((large->first > targetValue)&&(large!=available.begin())) --large; // get something <= our target value 
  bool done = (large == available.begin());
  long int loop = 0;
  long int excessModifier = 0;
  long int loopCost = 1;
  uint64_t startTime = GetTimeMillis();
  while(!done)
    {
      loopCost++;  // for every loop, make it exponentially easier to find an acceptable solutions
      excessModifier += loopCost;

      // Calculate the size of this new input
      large->second.i;
      const CScript& scriptPubKey = large->second.tx->vout[large->second.i].scriptPubKey;
      CScript scriptSigRes; // = txNew.vin[nIn].scriptSig;
      CWallet dummyWallet;
      bool signSuccess = ProduceSignature(DummySignatureCreator(&dummyWallet), scriptPubKey, scriptSigRes);
      int inputLen = signSuccess ? scriptSigRes.size(): P2PKH_INPUT_SIZE;
      CAmount fee = feeRate.GetFee(inputLen);
      // We will take the "large" txo and decrement it each time through this loop.
      // If large ever hits the beginning, we have checked everything and can quit.
      TxoGroup grp = makeGroup(large);                    // Make a group out of our current txo
      extend(targetValue+fee, available, grp, solutions, 0);  // And attempt to extend it into solutions (automatically added to the "solutions" object)

      TxoGroupMap::iterator i = solutions.begin();
#if 0 
      if ((i->first - targetValue < reasonableExcess(targetValue)+excessModifier)||(solutions.size()>MAX_SOLUTIONS))  // Did we find any good solutions?  
	{
          LogPrint("wallet","CoinSelection done 1\n");
	  done = true;  // If yes then quit.
	}
#endif

      uint nSolutions = solutions.size();
      if ((GetTimeMillis() - startTime > maxCoinSelSearchTime.value)&&(nSolutions >= 1)) // Have we looked for a long time
        {
          LogPrint("wallet","CoinSelection searched for the alloted time and found %d solutions\n",nSolutions);
	  done = true;  // If yes then quit.          
        }
      else if (i->first - targetValue <= dust/2)  // Did we find any good solutions?  
	{
          LogPrint("wallet","CoinSelection found a close solution\n");
	  done = true;  // If yes then quit.
	}              
      else if (solutions.size()>MAX_SOLUTIONS)  // Did we find lots of solutions?  
	{
          LogPrint("wallet","CoinSelection found many solutions\n");
	  done = true;  // If yes then quit.
	}      
      else  // No good solutions, so decrement large
	{
          CAmount a = large->first;
          do { --large; } while ((large != available.begin())&&(large->first != a));  // Skip all Txos whose value is the exact same as the Txo I just looked at.

          done = (large == available.begin()); // We are done if we get to the beginning.
          if (done) LogPrint("wallet","CoinSelection done 2\n");
	}

      if (!done)  // Now grab a random Txo and search for a solution near it
	{
	  CAmount r = GetRand(3*targetValue/4)+(targetValue/4);  // Looking for a Txo > 1/4 of the needed value
          SpendableTxos::iterator rit = available.lower_bound(r); 
          if (rit != aEnd) 
	    {
	      TxoGroup grp = makeGroup(rit);  // Make a group out of it
	      extend(targetValue, available, grp, solutions, 0);  // And attempt to extend it for solutions.
              if (i->first - targetValue < reasonableExcess(targetValue)+excessModifier)  // Did we find any good solutions?  
                {
	        done = true;  // If yes then quit.
	        }
            }
	}

      loop++;
    }

  // Let's see what solutions we found.
  TxoGroupMap::iterator i = solutions.begin();
  done=false;
  TxoGroupMap::iterator end = solutions.end();
  TxoGroupMap::iterator singleIn = end;
  TxoGroupMap::iterator noChange = end;
  int noChangeCount = 0;
  TxoGroupMap::iterator multiIn = end;
  int multiInCount = 0;
  for(i=solutions.begin(); (i != end) && !done; ++i)
    {
      if (!validate(i->second,targetValue)) continue;  // some bad solutions can occur (repeated elements), it is more efficient to eliminate them here than inside the loops
      int ntxo = i->second.second.size();
      if (ntxo == 1) singleIn = i; // ok this solution works but does not reduce the utxo set
      if ((i->first - targetValue <= dust)&&(ntxo > noChangeCount)) // look for a solution that won't need to pay change.
        {
          noChange = i;
          noChangeCount = ntxo;
          LogPrint("wallet","CoinSelection found a nochange solution\n");
        }
      if ((ntxo > 1)&&(ntxo > multiInCount)) { multiIn = i; multiInCount = ntxo; }
    }

  if (noChange != end) i = noChange;  // prefer the best no change solution
  else
    {
      if (utxosInWallet <= preferredNumUTXO.value)  // Find the cheapest (shortest) solution
        {
          if (singleIn != end) i = singleIn;
          else i = multiIn;
        }
      else  // Reduce UTXO
        {
        if (multiIn != end) i = multiIn;    // or pick one the reduces the UTXO
        else i = singleIn;  // ok take whatever I can get          
        }
    }

  if (i == solutions.end())
    {
      return TxoGroup(0,TxoItVec());
    }  
  LogPrint("wallet","CoinSelection returns %d choices. Dust: %d, Target: %d, found: %d, txos: %d\n", solutions.size(), dust, targetValue, i->first, i->second.second.size());
  TxoGroup ret = i->second;
  return ret;
}
