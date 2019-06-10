# Detailed Description and Network Format for the Invertible Bloom Lookup Table (IBLT)

## Summary

Invertible Bloom Lookup Tables (IBLTs) are a data structure [originally described](https://arxiv.org/abs/1101.2245) by Goodrich and Mitzenmacher. Similar to a Bloom filter, IBLTs provide a succinct representation of the items in a set `S`. Unlike a Bloom filter, which only represents set membership, the items in an IBLT are encoded in such a way that the symmetric difference between `S` and another set `R` can be recovered with high probability provided that its size does not exceed a tunable threshold. IBLTs are a critical data structure used in the [Graphene block propagation protocol](https://github.com/BitcoinUnlimited/BUIP/blob/master/093.mediawiki).

## High Level Design and Operation

Fundamentally, IBLTs store items as key-value pairs. Each IBLT is comprised of multiple cells with each cell containing four fields: `count`, `keySum`, `keyCheck`, and `valueSum`. Cells are divided into `c` groups of equal size, and to each group is assigned a distinct hash function. Items can be either inserted or erased from the IBLT. Insertion involves mapping key `k` (and corresponding value `v`) to a single cell from each of the `c` groups by way of the associated hash function. For each cell corresponding to the key, the fields are updated with the following values: `k` XORed with the `keySum`, a separate independent hash of `k` XORed with the keyCheck, `v` XORed with the `valueSum`, and the `count` incremented by one. Items are erased with the same set of operations as insertion except that the `count` field is decremented by one. IBLT `R` can be *subtracted* from IBLT `S` provided that the two share the same number of cells and the same hash function for each cell group. This difference, `S-R`, results in a new IBLT `D`, which is calculated by XORing each of the fields `keySum`, `valueSum`, and `keyCheck` from `S` with the same field in the corresponding cell of `R`, and the `count` field from each cell in `R` is subtracted from the `count` field in the corresponding cell of `S`.

## Design Concepts

The following concepts are fundamental to the operation of IBLTs, particularly in the context of the Graphene protocol.

### Symmetric Difference

In the last section we described the subtraction process for IBLTs `S` and `R` that contain the same number of cells and are constructed such that each of their cell groups use the same hash function. Because all fields except the count are XORed, subtraction is an entirely symmetric operation. That is to say `S-R` is equivalent to `R-S` except that the count fields are negated. Thus, we can regard `D = S-R` as the symmetric difference between the sets represented by `S` and `R` (see [Eppstein et al.](https://dl.acm.org/citation.cfm?id=2018462) for theoretical details). Any cell in `D` with count 1 or -1 and whose `keyCheck` is equal to the hash of its `keySum` field is considered *pure*, i.e. it contains a single element equal to the `valueSum`. The idea is that if the cell in `S` contains for example keys `a`, `b`, and `c` and the cell in `R` contains keys `a` and `b`, then the `keySum` field in `D` will contain `c` and the `count` field will be 1 (note that the `valueSum` field in `D` should also contain the value corresponding to the key represented by `keySum`). Similarly, if the `count` field has value -1, then we can deduce that the `keySum` field contains an element from `R`. In either case, the additional `keyCheck` validation is needed to ensure that the elements in the cell from `R` are a subset of the elements from the corresponding cell in `S`, e.g. if `R` instead contained keys `a` and `x`, then the `count` field in `D` would still be 1, but the `keySum` and `valueSum` fields would be bogus. 

### Peeling Process

Consider again the difference `D = S - R` between two equal sized IBLTs `S` and `R`, having the same set of hash functions. In the last section we argued that any pure cell in `D` contains an element from the symmetric difference between the sets represented by `S` and `R`. Unfortunately, other elements from the symmetric difference remain locked in any impure cells (those whose `count` field magnitude is not equal to 1 or whose `keyCheck` field is not equal to the hash of the `keySum`). For cells whose `count` magnitude is greater than 1 it is possible to *peel* the cell in order to remove additional entries. This is made possible by the fact (discussed above) that every item is inserted into a cell from every cell group. Thus, if any of the cells from `D` associated with a given item are pure, then that item can be identified and erased from `D`. This process itself can leave more pure cells, allowing it to be repeated until an unpeelable *core* remains in `D`.

## Data Structures

There are two new data structures associated with an IBLT: `CIblt` and `HashTableEntry`. Both data structures use the standard Bitcoin serialization format: they use little-endian for integers; vector encoding is Bitcoin standard (compact int length, vector values); and other specific use of the Bitcoin standard "compact int" encoding is noted in the tables below. The data structures are comprised of relatively simple C++ constructs, which we detail below. 

### CIblt

|**Field Name**|**Type**|**Size**|**Encoding Details**|**Purpose**|
|:------------:|:------:|:------:|:------------------:|:---------:|
|`version`|`uint64_t`|8 bytes|Compact size|Version bits|
|`mapHashIdxSeeds`|`map<uint8_t, uint32_t>`|variable|Standard|Mapping from hash function number (0 to `n_hash - 1`) to random seed for that hash function|
|`salt`|`uint32_t`|4 bytes|Standard|Entropy for generating random seeds in `mapHashIdxSeeds`|
|`n_hash`|`uint8_t`|1 byte|Standard|Number of hash functions used|
|`is_modified`|`bool`|1 byte|Standard|True if any items have been inserted into the IBLT|
|`hashTable`|`vector<HashTableEntry>`|variable|Standard|Data cells for IBLT|

The Graphene protocol requires that IBLT `R`, created by the receiver, be subtracted from IBLT `S`, originating from the sender. In order for this operation to succeed, it is critical that the IBLTs use the same quantity of hash functions, have the same number of cells, and that hash function `i` uses the same function and seed for both `S` and `R`. Although the sender may use [complex optimization techniques](https://github.com/umass-forensics/IBLT-optimization) to determine the number of cells and hash functions for `S`, which we discuss below, the receiver should simply copy those values provided that they are reasonably sized. The `i`th hash function is implemented by method `saltedHashValue` (defined below), which takes the [MurmurHash3](http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp) of the input using `mapHashIdxSeeds[i]` as its seed. Entries in `mapHashIdxSeeds` are extracted from entropy `salt` however the IBLT creator sees fit.

### HashTableEntry

`HashTableEntry` implements a single cell of the `CIblt` data structure.

|**Field Name**|**Type**|**Size**|**Encoding Details**|**Purpose**|
|:------------:|:------:|:------:|:------------------:|:---------:|
|`count`|`uint32_t`|4 bytes|Standard|Number of items|
|`keySum`|`uint64_t`|8 bytes|Standard|XOR of all keys|
|`keyCheck`|`uint32_t`|4 bytes|Standard|Error checking for keySum|
|`valueSum`|`vector<uint8_t>`|variable|Standard|XOR of all values|

## Critical Methods

In order to operate within the Graphene protocol, the `CIblt` and `HashTable` classes must implement the following methods: 

### CIblt Methods

#### `uint32_t keyChecksumCalc(uint64_t k)`

Return the [MurmurHash3](http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp) of key `k` using the integer `11` for the seed.

#### `_insert(int plusOrMinus, uint64_t k, vector<uint8_t> &v)`

Insert value `v` into all cells associated with the key `k`. Specifically, for each hash function index `i`, pass `i` and `k` to `saltedHashValue` to determine associated cells. For each associated cell, perform the following updates: `keySum ^= k`, `keyCheck ^= keyChecksumCalc(k)`, call `addValue(v)` on the cell, and `count += plusOrMinus`.

#### `void insert(uint64_t k, const vector<uint8_t> &v)`

Return `_insert(1, k, v)`.

#### `void erase(uint64_t k, const vector<uint8_t> &v)`

Return `_insert(-1, k, v)`.

#### `uint32_t saltedHashValue(size_t hashFuncIdx, unint64_t k)`

Return the [MurmurHash3](http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp) of key `k` using `mapHashIdxSeeds[hashFuncIdx]` as the seed.

#### `bool listEntries(set<pair<uint64_t, vector<uint8_t>>> &positive, set<pair<uint64_t, vector<uint8_t>>> &negative)`

Visit each cell; for any where `isPure() == true`, add the pair `<keySum, valueSum>` to set `positive` if `count == 1` or add it to `negative` if the `count == -1`. Next, *peel* (remove) the item associated with this key / value pair from every cell associated with the key `keySum`. If pure cells remain, then repeat the process. Otherwise, return `true` if all cells are empty or `false` if they are not.

#### `CIblt operator-(const CIblt &other)`

Ensure that `other` has the same number of cells and `mapHashIdxSeeds` vector as `this`. Create a third IBLT `result`, cloned from `this`. For each cell `cR` in `result` and corresponding cell `cO` in `other`, perform the following updates: `cR.keySum ^= cO.keySum`, `cR.keyCheck ^= cO.keyCheck`, call `cR.addValue(cO.valueSum)`, and `cR.count -= cO.count`.

### HashTable Methods

#### `bool isPure()`

Return `true` if the cell has `count == 1` or `count == -1` and `keyCheck == keyChecksumCalc(keySum)`. Return `false` otherwise.

#### `bool empty()`

Return `true` if `count == 0`, `keySum == 0`, and `keyCheck == 0`. Return `false` otherwise.

#### `void addValue(const vector<uint8_t> &v)`

For each `i`, `valueSum[i] ^= v[i]`. Resize `valueSum` to accommodate `v` if necessary.

## Selecting parameters for the and IBLT

The IBLT decode rate varies with the number of recovered items. In order to ensure a consistent decode rate (1 out of 240 blocks, for example), it is necessary to modify the number of hash functions and overhead used when constructing the IBLT. We have released [code](https://github.com/umass-forensics/IBLT-optimization) that can be used to determine such values. 


## Backward compatibility

CIblt implementations with higher version numbers are expected to maintain backward compatibility with those having lower version numbers.

## Implementation

https://github.com/BitcoinUnlimited/BitcoinUnlimited/tree/release

## Copyright

This document is placed in the public domain.
