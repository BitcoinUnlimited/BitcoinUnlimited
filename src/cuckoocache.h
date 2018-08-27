// Copyright (c) 2016 Jeremy Rubin
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOIN_CUCKOOCACHE_H_
#define _BITCOIN_CUCKOOCACHE_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>


/** namespace CuckooCache provides high performance cache primitives
 *
 * Summary:
 *
 * 1) CBitPackedAtomicFlags is bit-packed atomic flags for garbage collection
 *
 * 2) cache is a cache which is performant in memory usage and lookup speed. It
 * is lockfree for erase operations. Elements are lazily erased on the next
 * insert.
 */
namespace CuckooCache
{
/** CBitPackedAtomicFlags implements a container for garbage collection flags
 * that is only thread unsafe on calls to setup. This class bit-packs collection
 * flags for memory efficiency.
 *
 * All operations are std::memory_order_relaxed so external mechanisms must
 * ensure that writes and reads are properly synchronized.
 *
 * On setup(n), all bits up to n are marked as collected.
 *
 * Under the hood, because it is an 8-bit type, it makes sense to use a multiple
 * of 8 for setup, but it will be safe if that is not the case as well.
 *
 */
class CBitPackedAtomicFlags
{
    std::unique_ptr<std::atomic<uint8_t>[]> mem;

public:
    /** No default constructor as there must be some size */
    CBitPackedAtomicFlags() = delete;

    /**
     * CBitPackedAtomicFlags constructor creates memory to sufficiently
     * keep track of garbage collection information for size entries.
     *
     * @param size the number of elements to allocate space for
     *
     * @post bit_set, bit_unset, and bit_is_set function properly forall x. x <
     * size
     * @post All calls to bit_is_set (without subsequent bit_unset) will return
     * true.
     */
    CBitPackedAtomicFlags(uint32_t nSize)
    {
        // pad out the size if needed
        nSize = (nSize + 7) / 8;
        mem.reset(new std::atomic<uint8_t>[nSize]);
        for (uint32_t i = 0; i < nSize; ++i)
            mem[i].store(0xFF);
    };

    /** setup marks all entries and ensures that CBitPackedAtomicFlags can store
     * at least size entries
     *
     * @param b the number of elements to allocate space for
     * @post bit_set, bit_unset, and bit_is_set function properly forall x. x <
     * b
     * @post All calls to bit_is_set (without subsequent bit_unset) will return
     * true.
     */
    inline void setup(uint32_t b)
    {
        CBitPackedAtomicFlags d(b);
        std::swap(mem, d.mem);
    }

    /** bit_set sets an entry as discardable.
     *
     * @param s the index of the entry to bit_set.
     * @post immediately subsequent call (assuming proper external memory
     * ordering) to bit_is_set(s) == true.
     *
     */
    inline void bit_set(uint32_t s) { mem[s >> 3].fetch_or(1 << (s & 7), std::memory_order_relaxed); }
    /**  bit_unset marks an entry as something that should not be overwritten
     *
     * @param s the index of the entry to bit_unset.
     * @post immediately subsequent call (assuming proper external memory
     * ordering) to bit_is_set(s) == false.
     */
    inline void bit_unset(uint32_t s) { mem[s >> 3].fetch_and(~(1 << (s & 7)), std::memory_order_relaxed); }
    /** bit_is_set queries the table for discardability at s
     *
     * @param s the index of the entry to read.
     * @returns if the bit at index s was set.
     * */
    inline bool bit_is_set(uint32_t s) const { return (1 << (s & 7)) & mem[s >> 3].load(std::memory_order_relaxed); }
};

/** cache implements a cache with properties similar to a cuckoo-set
 *
 *  The cache is able to hold up to (~(uint32_t)0) - 1 elements.
 *
 *  Read Operations:
 *      - contains(*, false)
 *
 *  Read+Erase Operations:
 *      - contains(*, true)
 *
 *  Erase Operations:
 *      - allow_erase()
 *
 *  Write Operations:
 *      - setup()
 *      - setup_bytes()
 *      - insert()
 *      - please_keep()
 *
 *  Synchronization Free Operations:
 *      - invalid()
 *      - compute_hashes()
 *
 * User Must Guarantee:
 *
 * 1) Write Requires synchronized access (e.g., a lock)
 * 2) Read Requires no concurrent Write, synchronized with the last insert.
 * 3) Erase requires no concurrent Write, synchronized with last insert.
 * 4) An Erase caller must release all memory before allowing a new Writer.
 *
 *
 * Note on function names:
 *   - The name "allow_erase" is used because the real discard happens later.
 *   - The name "please_keep" is used because elements may be erased anyways on insert.
 *
 * @tparam Element should be a movable and copyable type
 * @tparam Hash should be a function/callable which takes a template parameter
 * hash_select and an Element and extracts a hash from it. Should return
 * high-entropy hashes for `Hash h; h<0>(e) ... h<7>(e)`.
 */
template <typename Element, typename Hash>
class cache
{
private:
    /** table stores all the elements */
    std::vector<Element> vTable;

    /** size stores the total available slots in the hash table */
    uint32_t nSize;

    /** The CBitPackedAtomicFlags array is marked mutable because we want
     * garbage collection to be allowed to occur from const methods */
    mutable CBitPackedAtomicFlags collection_flags;

    /** vEpochFlags tracks how recently an element was inserted into
     * the cache. true denotes recent, false denotes not-recent. See insert()
     * method for full semantics.
     */
    mutable std::vector<bool> vEpochFlags;

    /** nEpochHeuristicCounter is used to determine when a epoch might be aged
     * & an expensive scan should be done.  nEpochHeuristicCounter is
     * decremented on insert and reset to the new number of inserts which would
     * cause the epoch to reach nEpochSize when it reaches zero.
     */
    uint32_t nEpochHeuristicCounter;

    /** nEpochSize is set to be the number of elements supposed to be in a
     * epoch. When the number of non-erased elements in a epoch
     * exceeds nEpochSize, a new epoch should be started and all
     * current entries demoted. nEpochSize is set to be 45% of size because
     * we want to keep load around 90%, and we support 3 epochs at once --
     * one "dead" which has been erased, one "dying" which has been marked to be
     * erased next, and one "living" which new inserts add to.
     */
    uint32_t nEpochSize;

    /** hash_mask should be set to appropriately mask out a hash such that every
     * masked hash is [0,size), eg, if floor(log2(size)) == 20, then hash_mask
     * should be (1<<20)-1
     */
    uint32_t hash_mask;

    /** depth_limit determines how many elements insert should try to replace.
     * Should be set to log2(n)*/
    uint8_t depth_limit;

    /** hash_function is a const instance of the hash function. It cannot be
     * static or initialized at call time as it may have internal state (such as
     * a nonce).
     * */
    const Hash hash_function;

    /** compute_hashes is convenience for not having to write out this
     * expression everywhere we use the hash values of an Element.
     *
     * @param e the element whose hashes will be returned
     * @returns std::array<uint32_t, 8> of deterministic hashes derived from e
     */
    inline std::array<uint32_t, 8> compute_hashes(const Element &e) const
    {
        return {{(uint32_t)(((uint64_t)hash_function.template operator()<0>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<1>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<2>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<3>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<4>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<5>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<6>(e) * (uint64_t)nSize) >> 32),
            (uint32_t)(((uint64_t)hash_function.template operator()<7>(e) * (uint64_t)nSize) >> 32)}};
    }

    /* end
     * @returns a constexpr index that can never be inserted to */
    constexpr uint32_t invalid() const { return ~(uint32_t)0; }
    /** allow_erase marks the element at index n as discardable. Threadsafe
     * without any concurrent insert.
     * @param n the index to allow erasure of
     */
    inline void allow_erase(uint32_t n) const { collection_flags.bit_set(n); }
    /** please_keep marks the element at index n as an entry that should be kept.
     * Threadsafe without any concurrent insert.
     * @param n the index to prioritize keeping
     */
    inline void please_keep(uint32_t n) const { collection_flags.bit_unset(n); }
    /** epoch_check handles the changing of epochs for elements stored in the
     * cache. epoch_check should be run before every insert.
     *
     * First, epoch_check decrements and checks the cheap heuristic, and then does
     * a more expensive scan if the cheap heuristic runs out. If the expensive
     * scan suceeds, the epochs are aged and old elements are allow_erased. The
     * cheap heuristic is reset to retrigger after the worst case growth of the
     * current epoch's elements would exceed the nEpochSize.
     */
    void epoch_check()
    {
        if (nEpochHeuristicCounter != 0)
        {
            --nEpochHeuristicCounter;
            return;
        }

        // count the number of elements from the latest epoch which
        // have not been erased.
        uint32_t nEpochUnusedCount = 0;
        for (uint32_t i = 0; i < nSize; ++i)
            nEpochUnusedCount += vEpochFlags[i] && !collection_flags.bit_is_set(i);

        // If there are more non-deleted entries in the current epoch than the
        // epoch size, then allow_erase on all elements in the old epoch (marked
        // false) and move all elements in the current epoch to the old epoch
        // but do not call allow_erase on their indices.
        if (nEpochUnusedCount >= nEpochSize)
        {
            for (uint32_t i = 0; i < nSize; ++i)
            {
                if (vEpochFlags[i])
                    vEpochFlags[i] = false;
                else
                    allow_erase(i);
            }
            nEpochHeuristicCounter = nEpochSize;
        }
        else
        {
            // reset the nEpochHeuristicCounter to next do a scan when worst
            // case behavior (no intermittent erases) would exceed epoch size,
            // with a reasonable minimum scan size.
            // Ordinarily, we would have to sanity check std::min(nEpochSize,
            // nEpochUnusedCount), but we already know that `nEpochUnusedCount
            // < nEpochSize` in this branch
            nEpochHeuristicCounter = std::max(1u, std::max(nEpochSize / 16, nEpochSize - nEpochUnusedCount));
        }
    }

public:
    /** You must always construct a cache with some elements via a subsequent
     * call to setup or setup_bytes, otherwise operations may segfault.
     */
    cache()
        : vTable(), nSize(), collection_flags(0), vEpochFlags(), nEpochHeuristicCounter(), nEpochSize(), depth_limit(0),
          hash_function()
    {
    }

    /** setup initializes the container to store no more than new_size
     * elements. setup rounds down to a power of two size.
     *
     * setup should only be called once.
     *
     * @param new_size the desired number of elements to store
     * @returns the maximum number of elements storable
     **/
    uint32_t setup(uint32_t new_size)
    {
        // depth_limit must be at least one otherwise errors can occur.
        depth_limit = static_cast<uint8_t>(std::log2(static_cast<float>(std::max((uint32_t)2, new_size))));
        nSize = 1 << depth_limit;
        hash_mask = nSize - 1;
        vTable.resize(nSize);
        collection_flags.setup(nSize);
        vEpochFlags.resize(nSize);

        // Set to 45% as described above
        nEpochSize = std::max((uint32_t)1, (45 * nSize) / 100);

        // Initially set to wait for a whole epoch
        nEpochHeuristicCounter = nEpochSize;
        return nSize;
    }

    /** setup_bytes is a convenience function which accounts for internal memory
     * usage when deciding how many elements to store. It isn't perfect because
     * it doesn't account for any overhead (struct size, MallocUsage, collection
     * and epoch flags). This was done to simplify selecting a power of two
     * size. In the expected use case, an extra two bits per entry should be
     * negligible compared to the size of the elements.
     *
     * @param bytes the approximate number of bytes to use for this data
     * structure.
     * @returns the maximum number of elements storable (see setup()
     * documentation for more detail)
     */
    uint32_t setup_bytes(size_t bytes) { return setup(bytes / sizeof(Element)); }
    /** insert loops at most depth_limit times trying to insert a hash
     * at various locations in the table via a variant of the Cuckoo Algorithm
     * with eight hash locations.
     *
     * It drops the last tried element if it runs out of depth before
     * encountering an open slot.
     *
     * Thus
     *
     * insert(x);
     * return contains(x, false);
     *
     * is not guaranteed to return true.
     *
     * @param e the element to insert
     * @post one of the following: All previously inserted elements and e are
     * now in the table, one previously inserted element is evicted from the
     * table, the entry attempted to be inserted is evicted.
     *
     */
    inline void insert(Element e)
    {
        epoch_check();
        uint32_t last_loc = invalid();
        bool last_epoch = true;
        std::array<uint32_t, 8> locs = compute_hashes(e);
        // Make sure we have not already inserted this element
        // If we have, make sure that it does not get deleted
        for (uint32_t loc : locs)
        {
            if (vTable[loc] == e)
            {
                please_keep(loc);
                vEpochFlags[loc] = last_epoch;
                return;
            }
        }

        for (uint8_t depth = 0; depth < depth_limit; ++depth)
        {
            // First try to insert to an empty slot, if one exists
            for (uint32_t loc : locs)
            {
                if (!collection_flags.bit_is_set(loc))
                    continue;
                vTable[loc] = std::move(e);
                please_keep(loc);
                vEpochFlags[loc] = last_epoch;
                return;
            }

            /** Swap with the element at the location that was
            * not the last one looked at. Example:
            *
            * 1) On first iteration, last_loc == invalid(), find returns last, so
            *    last_loc defaults to locs[0].
            * 2) On further iterations, where last_loc == locs[k], last_loc will
            *    go to locs[k+1 % 8], i.e., next of the 8 indicies wrapping around
            *    to 0 if needed.
            *
            * This prevents moving the element we just put in.
            *
            * The swap is not a move -- we must switch onto the evicted element
            * for the next iteration.
            */
            last_loc = locs[(1 + (std::find(locs.begin(), locs.end(), last_loc) - locs.begin())) & 7];
            std::swap(vTable[last_loc], e);
            // Can't std::swap a std::vector<bool>::reference and a bool&.
            bool epoch = last_epoch;
            last_epoch = vEpochFlags[last_loc];
            vEpochFlags[last_loc] = epoch;

            // Recompute the locs -- unfortunately happens one too many times!
            locs = compute_hashes(e);
        }
    }

    /* contains iterates through the hash locations for a given element
     * and checks to see if it is present.
     *
     * contains does not check garbage collected state (in other words,
     * garbage is only collected when the space is needed), so:
     *
     * insert(x);
     * if (contains(x, true))
     *     return contains(x, false);
     * else
     *     return true;
     *
     * executed on a single thread will always return true!
     *
     * This is a great property for re-org performance for example.
     *
     * contains returns a bool set true if the element was found.
     *
     * @param e the element to check
     * @param erase
     *
     * @post if erase is true and the element is found, then the garbage collect
     * flag is set
     * @returns true if the element is found, false otherwise
     */
    inline bool contains(const Element &e, const bool erase) const
    {
        std::array<uint32_t, 8> locs = compute_hashes(e);
        for (uint32_t loc : locs)
        {
            if (vTable[loc] == e)
            {
                if (erase)
                    allow_erase(loc);
                return true;
            }
        }
        return false;
    }
};
} // namespace CuckooCache

#endif
