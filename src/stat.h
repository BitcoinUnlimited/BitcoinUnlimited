// Copyright (c) 2016 The Bitcoin Unlimited developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once
#ifndef STAT_H
#define STAT_H

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
// c++11 #include <type_traits>
#include <univalue.h>

class CStatBase;

enum StatOperation
  {
    STAT_OP_SUM = 1,
    STAT_OP_AVE = 2,
    STAT_OP_MAX = 4,
    STAT_OP_MIN = 8,
    STAT_KEEP = 0x10,  // Do not clear the value when it it moved into history
    STAT_KEEP_COUNT = 0x20,  // do not reset the sample count when it it moved into history
  };

//typedef boost::reference_wrapper<std::string> CStatKey;
typedef std::string CStatKey;
typedef std::map<CStatKey, CStatBase*> CStatMap;

extern CStatMap statistics;
extern boost::asio::io_service stat_io_service;
extern boost::posix_time::milliseconds statMinInterval;

template<typename NUM> void statAverage(NUM& tally,const NUM& cur,unsigned int sampleCounts)
{
  tally = ((tally*((NUM) sampleCounts-1))+cur)/sampleCounts;
}

template void statAverage<uint16_t>(uint16_t& tally,const uint16_t& cur,unsigned int sampleCounts);
template void statAverage<unsigned int>(unsigned int& tally,const unsigned int& cur,unsigned int sampleCounts);
template void statAverage<uint64_t>(uint64_t& tally,const uint64_t& cur,unsigned int sampleCounts);
template void statAverage<int16_t>(int16_t& tally,const int16_t& cur,unsigned int sampleCounts);
template void statAverage<int>(int& tally,const int& cur,unsigned int sampleCounts);
template void statAverage<int64_t>(int64_t& tally,const int64_t& cur,unsigned int sampleCounts);
template void statAverage<float>(float& tally,const float& cur,unsigned int sampleCounts);
template void statAverage<double>(double& tally,const double& cur,unsigned int sampleCounts);
// template void statAverage<ZZ>(ZZ& tally,const ZZ& cur,unsigned int sampleCounts);

template<typename NUM> void statReset(NUM& tally,uint64_t flags)
{
  if (!(flags & STAT_KEEP))
    tally = NUM();
}


class CStatBase
{
public:
  CStatBase() {};
  virtual UniValue GetNow()=0;  // Returns the current value of this statistic
  virtual UniValue GetSeries(const std::string& name, int count)=0;  // Returns the historical or series data
};

template <class DataType,class RecordType=DataType> class CStat:public CStatBase
{
  
public:

protected:
  RecordType value;
  std::string name;
public:
  CStat() {}

  CStat(const char* namep):name(namep)
    {
      value = RecordType(); // = 0;
      statistics[CStatKey(name)] = this;
    }
  CStat(const std::string& namep):name(namep)
    {
      value = RecordType(); // = 0;
      statistics[CStatKey(name)] = this;
    }

void init(const char* namep)
{
  name = namep;
  value = RecordType(); // = 0;
  statistics[CStatKey(name)] = this;
}

void init(const std::string& namep)
{
  name = namep;
  value = RecordType(); // = 0;
  statistics[CStatKey(name)] = this;
}

void cleanup()
{
statistics.erase(CStatKey(name));
name = "";
}

  CStat& operator=(const DataType& arg) { value=arg; return *this;}
  
  CStat& operator+=(const DataType& rhs) { value+=rhs; return *this; }
  CStat& operator-=(const DataType& rhs) { value-=rhs; return *this; }

  RecordType& operator() () { return value; }

  virtual UniValue GetNow()
  {
    return UniValue(value);
  }

  virtual UniValue GetSeries(const std::string& name, int count)
  {
    return NullUniValue;  // Has no series data
  }

  ~CStat()
    {
      if (name.size())
        statistics.erase(CStatKey(name));
    }
};


extern const char* sampleNames[];
extern int operateSampleCount[];  // Even though there may be 1000 samples, it takes this many samples to produce an element in the next series.
extern int interruptIntervals[];  // When to calculate the next series, in multiples of the interrupt time.

//accumulate(accumulator,datapt);


enum
  {
    STATISTICS_NUM_RANGES = 5,
    STATISTICS_SAMPLES = 100,
  };

#if 0
template<typename T>
struct HasShiftLeft
{
    template<typename U, size_t (U::*)() const> struct SFINAE {};
    template<typename U> static char Test(SFINAE<U, &U::operator<< >*);
    template<typename U> static int Test(...);
    static const bool Has = sizeof(Test<T>(0)) == sizeof(char);
};

template<typename DataType>
void custom(DataType& lhs, const DataType& rhs, std::true_type)
{
  lhs <<= rhs;
}

template<typename DataType>
void custom(DataType& lhs, const DataType& rhs, std::false_type)
{
}
#endif

template <class DataType,class RecordType=DataType> class CStatHistory:public CStat<DataType,RecordType>
{

  protected:
  unsigned int op;
  boost::asio::deadline_timer timer;
  RecordType history[STATISTICS_NUM_RANGES][STATISTICS_SAMPLES];
  int loc[STATISTICS_NUM_RANGES];
  uint64_t timerCount;
  unsigned int sampleCount;

public:
CStatHistory():CStat<DataType,RecordType>(),timer(stat_io_service)
  {
  }

CStatHistory(const char* name, unsigned int operation=STAT_OP_SUM):CStat<DataType,RecordType>(name), op(operation), timer(stat_io_service)
  {
    Clear();
  }

CStatHistory(const std::string& name, unsigned int operation=STAT_OP_SUM):CStat<DataType,RecordType>(name), op(operation), timer(stat_io_service)
  {
    Clear();
  }

  void init(const char* name, unsigned int operation=STAT_OP_SUM)
  {
    CStat<DataType,RecordType>::init(name);
    op=operation;
    Clear();
  }

  void init(const std::string& name, unsigned int operation=STAT_OP_SUM)
  {
    CStat<DataType,RecordType>::init(name);
    op=operation;
    Clear();
  }

  void Clear(void)
    {
      timerCount=0;
      for (int i=0; i<STATISTICS_NUM_RANGES; i++) loc[i] = 0;
      for (int i=0; i<STATISTICS_NUM_RANGES; i++)
        for (int j=0; j<STATISTICS_SAMPLES; j++)
	  {
	    history[i][j] = RecordType();
	  }
      Start();
    }

  ~CStatHistory()
    {
      // TODO: statistics.erase(this);
    }

  CStatHistory& operator << (const DataType& rhs) 
    {

      if (op & STAT_OP_SUM) this->value += rhs;
      else if (op & STAT_OP_AVE) { unsigned int tmp = ++sampleCount; if (tmp==0) tmp=1; statAverage(this->value,rhs,tmp); }
      else if (op & STAT_OP_MAX) { if (this->value < rhs) this->value = rhs; }
      else if (op & STAT_OP_MIN) { if (this->value > rhs) this->value = rhs; }

    return *this; 
    }

  void Start()
  {
    wait();
  }

  void Stop()
  {
    timer.cancel();
  }

  int Series(int series, DataType* array, int len)
  {
    assert(series < STATISTICS_NUM_RANGES);
    if (len>STATISTICS_SAMPLES) len = STATISTICS_SAMPLES;

    int pos = loc[series] - STATISTICS_SAMPLES;
    if (pos < 0) pos += STATISTICS_SAMPLES;
    for (int i=0;i<len;i++,pos++)  // could be a lot more efficient with 2 memcpy
      {
	if (pos>=STATISTICS_SAMPLES) pos -= STATISTICS_SAMPLES;
        array[i] = history[series][pos];
      }

    return len;
  }


  virtual UniValue GetSeries(const std::string& name, int count)
  {
    for (int series = 0; series < STATISTICS_NUM_RANGES; series++)
      {
	if (name == sampleNames[series])
	  {
          UniValue ret(UniValue::VARR);
          if (count<0) count = 0;
          if (count>STATISTICS_SAMPLES) count = STATISTICS_SAMPLES;
          for (int i=-1*(count-1); i<=0;i++)
	    {
	      const RecordType& sample = History(series, i);
              ret.push_back((UniValue)sample);
	    }
          return ret;
	  }
      }
    return NullUniValue;  // No series of this name
  }

  // 0 is latest, then pass a negative number for prior
  const RecordType& History(int series, int ago)
  {
    assert(ago <= 0);
    assert(series < STATISTICS_NUM_RANGES);
    assert(-1*ago <= STATISTICS_SAMPLES);
    int pos = loc[series] - 1 + ago;
    if (pos < 0) pos += STATISTICS_SAMPLES;
    return history[series][pos];
  }

  void timeout(const boost::system::error_code &e) 
  {
    if (e) return;

    // To avoid taking a mutex, I sample and compare.  This sort of thing isn't perfect but acceptable for statistics calc.
    volatile RecordType* sampler = &this->value;
    RecordType samples[2];
    do
      {
	samples[0] = *sampler;
	samples[1] = *sampler;
      } while (samples[0] != samples[1]);

    statReset(this->value,op);
    if ((op & STAT_KEEP_COUNT)==0) sampleCount = 0;

    history[0][loc[0]] = samples[0];
    loc[0]++;
    if (loc[0] >= STATISTICS_SAMPLES) loc[0]=0;

    timerCount++;
    // flow the samples if its time
    for (int i=0;i<STATISTICS_NUM_RANGES-1;i++)
      {
	if ((timerCount%interruptIntervals[i])==0)
	  {
	    int start = loc[i];
            RecordType accumulator = RecordType();
            
            // First time in the loop we need to assign
	    start--;
            if (start<0) start+=STATISTICS_SAMPLES;  // Wrap around
            accumulator = history[i][start];
            // subsequent times we combine as per the operation
            for (int j=1;j<operateSampleCount[i];j++)
	      {
		start--;
                if (start<0) start+=STATISTICS_SAMPLES;  // Wrap around
                RecordType datapt = history[i][start];
                if ((op & STAT_OP_SUM)||(op & STAT_OP_AVE)) accumulator += datapt;
                else if (op & STAT_OP_MAX) { if (accumulator < datapt) accumulator = datapt; }
                else if (op & STAT_OP_MIN) { if (accumulator > datapt) accumulator = datapt; }
                
	      }
            // All done accumulating.  Now store the data in the proper history field -- its going in the next series.
            if (op == STAT_OP_AVE) accumulator /= ((DataType) operateSampleCount[i]);
            history[i+1][loc[i+1]] = accumulator;
            loc[i+1]++;
            if (loc[i+1] >= STATISTICS_SAMPLES) loc[i+1]=0;  // Wrap around                  
	  }
      }
    wait();
  }

protected:
  void wait() 
   {
      timer.expires_from_now(statMinInterval); 
      timer.async_wait(boost::bind(&CStatHistory::timeout, this, boost::asio::placeholders::error));
   }


};



template<class NUM> class MinValMax
{
public:
  NUM min;
  NUM val;
  NUM max;
  int samples;
  MinValMax():min(std::numeric_limits<NUM>::max()),val(0),max(std::numeric_limits<NUM>::min()),samples(0)
    {
    }

  MinValMax& operator=(const MinValMax& rhs)
    {
      min = rhs.min;
      val = rhs.val;
      max = rhs.max;
      samples = rhs.samples;
      return *this;
    }

  MinValMax& operator=(const volatile MinValMax& rhs)
    {
      min = rhs.min;
      val = rhs.val;
      max = rhs.max;
      samples = rhs.samples;
      return *this;
    }

  bool operator !=(const MinValMax& rhs) const
    {
      return !(*this == rhs);
    }

  bool operator ==(const MinValMax& rhs) const
    {
      if (min!=rhs.min) return false;
      if (val!=rhs.val) return false;
      if (max!=rhs.max) return false;
      if (samples != rhs.samples) return false;
      return true;  
    }

  MinValMax& operator=(const NUM& rhs)
    {
      if (max < rhs) max=rhs;
      if (min > rhs) min=rhs;
      val = rhs;
      samples++;
      return *this;
    }


  // Probably not meaningful just here to meet the template req
  bool operator > (const MinValMax& rhs) const
    {
      return (max > rhs.max);
    }
  // Probably not meaningful just here to meet the template req
  bool operator < (const MinValMax& rhs) const
    {
      return (min > rhs.min);
    }

  bool operator > (const NUM& rhs) const
    {
      return (val > rhs);
    }
  bool operator < (const NUM& rhs) const
    {
      return (val < rhs);
    }

  // happens when users adds a stat to the system
  MinValMax& operator+=(const NUM& rhs)
    {
      val += rhs;
      if (max < val) max=val;
      if (min > val) min=val;
      samples++;
      return *this;
    }
  // happens when users adds a stat to the system
  MinValMax& operator-=(const NUM& rhs)
    {
      val -= rhs;
      if (max < rhs) max=val;
      if (min > rhs) min=val;
      samples++;
      return *this;
    }

  // happens when results are moved from a faster series to a slower one.
  MinValMax& operator+=(const MinValMax& rhs)
    {
      //if (rhs.max > max) max=rhs.max;
      //if (rhs.min < min) min=rhs.min;
      max += rhs.max;
      min += rhs.min;
      val += rhs.val;
      samples += rhs.samples;
      return *this;
    }


  // used in the averaging
  MinValMax& operator/=(const NUM& rhs)
    {
      val /= rhs;
      min /= rhs;
      max /= rhs;
      return *this;
    }
   
  operator UniValue() const
  {
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("min", (UniValue)min));
    ret.push_back(Pair("val", (UniValue)val));
    ret.push_back(Pair("max", (UniValue)max));
    return ret;
  }

};

template<typename NUM> void statAverage(MinValMax<NUM>& tally,const NUM& cur,unsigned int sampleCounts)
{
  statAverage(tally.val,cur,sampleCounts);
  if (cur > tally.max) tally.max = cur;
  if (cur < tally.min) tally.min = cur;
}

template<typename NUM> void statReset(MinValMax<NUM>& tally,uint64_t flags)
{
  if (flags & STAT_KEEP)
    {
      tally.min = tally.val;
      tally.max = tally.val;
    }
  else
    {
      tally.min = tally.max = tally.val = NUM();
    }
}


template<class T, int NumBuckets> class LinearHistogram
{
protected:
  int buckets[NumBuckets];
  T start;
  T end; 
public:
LinearHistogram(T pstart, T pend):buckets(0), start(pstart), end(pend)
  {   
  }
};



// Get the named statistic.  Returns NULL if it does not exist
CStatBase* GetStat(char* name);


#endif
