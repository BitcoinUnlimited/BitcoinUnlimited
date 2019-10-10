# Description and Network Format for the Fast Bloom Filter

## Summary

The fast filter is a Bloom filter implementation specific to the storage of Bitcoin transaction hashes that takes advantage of entropy in the hash values themselves to obviate the need to perform independent hashes when inserting or querying items in the filter. This feature provides a significant performance improvement over conventional Bloom filters because the majority of CPU time is spent in hashing items. 

## High Level Design and Operation

A [Bloom filter](https://en.wikipedia.org/wiki/Bloom_filter) is a classic probabilistic data structure that maintains set membership information for a collections of items. The filter is comprised of a large array of binary values and a set of `k` independent hash functions, each of which maps an item to a particular index in the array. Items are inserted into the filter by hashing them with the `k` hash functions and setting the bit at the associated index to 1. Queries follow a similar process, the item is hashed by all `k` hash functions and is considered *present* in the filter if all associated index locations are set to 1, otherwise the item is considered *not present*. By construction, if an item is deemed *not present*, then we can be certain that it is, in fact, not in the set. However, if an item is deemed *present* then we can only be sure that it is actually in the set up to a certain probability called the *false positive rate*. The array size and number of hash functions can be configured so as to achieve any desired false positive rate, with low rates requiring larger arrays. The principal innovation in a fast filter is that array indexes are determined directly from the item being inserted or queried, eliminating the need for `k` hashes to be applied. 

## Data Structures

In the BitcoinUnlimited codebase, there exist multiple fast filter implementations. Here we describe `CVariableFastFilter` which is used by version 2 of the [Graphene](https://github.com/BitcoinUnlimited/BUIP/blob/master/093.mediawiki) protocol. This data structure uses the standard Bitcoin serialization format: it uses little-endian for integers and vector encoding is Bitcoin standard (compact int length, vector values).

### CVariableFastFilter

|**Field Name**|**Type**|**Size**|**Purpose**|
|:------------:|:------:|:------:|:---------:|
|`vData`|`vector<unsigned char>`|variable|Bit array|
|`nHashFuncs`|`uint8_t`|1 byte|Number of hash functions to use|
|`nFilterBytes`|`uint32_t`|4 bytes|Size of `vData` in bytes|
|`nFilterBits`|`uint64_t`|8 bytes|Size of `vData` in bits|

Note that the allowable range for `nHashFuncs` is 1 to 32, inclusive. And only fields `vData`, `nHashFuncs`, and `nFilterItems` need be serialized or deserialized.

## Critical Methods

In order to operate within the Graphene protocol, the `CVariableFastFilter` class must implement the following methods: 

### `void insert(uint256 hash)`

Let `h` be the `uint256` transaction hash constituting the item being inserted, which we interpret as an array of unsigned 8 bit integers. We first assume that `nHashFuncs` <= 8. Beginning with the first index, 4 consecutive bytes are read from `h`, cast to an unsigned 32 bit integer, and the result modulus `nFilterItems-1` is assigned to variable `idx`. When `vData` is interpreted as a bit-array, `idx` gives the index of the bit in `vData` that should be set to 1. This process is repeated for each hash function by taking the next (non-overlapping) sequence of 4 bytes from `h`.

#### Hash rotation

If the process above is repeated, then we will reach the end of `h` after 8 hash functions have been applied. Accordingly, after any multiple of 8 hash functions have been applied, array `h` is rotated by shifting each byte in the array to the next higher index, with the last index being moved to the first. Since 4 rotations can be performed before duplicating an existing hash function, this technique enables a total of 32 hash functions to be used. 

### `bool checkAndSet(uint256 hash)`

Same as `insert` except that it returns `true` if any bits in `vData` are set and `false` otherwise.

### `bool contains(uint256 hash)`

Same as `insert` except instead of setting the bits in `vData` corresponding to each hash function, the existing values are collected into set `S`. If all bits in `S` are equal to 1, then the value `true` is returned, otherwise the value `false` is returned.

## Implementation

https://github.com/BitcoinUnlimited/BitcoinUnlimited/tree/release

## Copyright

This document is placed in the public domain.
