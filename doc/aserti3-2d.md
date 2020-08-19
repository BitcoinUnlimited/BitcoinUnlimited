---
layout: specification
title: 2020-NOV-15 ASERT Difficulty Adjustment Algorithm (aserti3-2d)
date: 2020-08-12
category: spec
activation: 1605441600 (proposed)
version: 0.6
author: freetrader, Jonathan Toomim, Calin Culianu, Mark Lundeberg
---

# 2020-NOV-15 ASERT Difficulty Adjustment Algorithm (aserti3-2d)

freetrader, Jonathan Toomim, Calin Culianu, Mark Lundeberg

Version 0.6, 2020-08-12

----

## Summary

It is proposed that in the November 2020 upgrade, a new difficulty
adjustment algorithm referred to as 'aserti3-2d' (or 'ASERT' for short)
be activated on Bitcoin Cash. Activation will be based on MTP, with the last
pre-fork block used as the anchor block.


## Motivation

- To eliminate periodic oscillations in difficulty and hashrate

- To reduce the difference in profitability between steady miners and
  those who switch to mining other blockchains.

- To maintain average block intervals close to the 10 minute target.

- To bring the average transaction confirmation time close to target time.


## Technical background

The DAA introduced in November 2017 exhibited susceptibility to a daily
periodic difficulty oscillation stemming directly from the simple moving
average design of the algorithm. The periodic difficulty oscillations
incentivized switch mining and disincentivized steady hashrate mining.

The oscillations in difficulty and hashrate have resulted in a daily pattern
of long confirmation times followed by bursts of rapid blocks.
Average confirmation time of transactions are significantly increased as
few transactions are included in the rapidly mined blocks.

Research into the family of difficulty algorithms based on an exponential
moving average (EMA) yielded ASERT (Absolutely Scheduled Exponentially
Rising Targets) [1], developed by Mark Lundeberg in 2019 and thoroughly
described by him in 2020 although an equivalent formula was independently
discovered in 2018 by Jacob Eliosoff and in 2020 by Werner et. al [6].

ASERT does not exhibit the above-mentioned oscillations and has a range of
other attractive qualities such as robustness against singularities [15]
without a need for additional rules, and absence of accumulation of
rounding/approximation error.

In extensive simulation against a range of contending stable algorithms [2],
an ASERT algorithm performed best across criteria that included:

- average block times closest to ideal target time of 600 seconds
- average transaction confirmation time closest to target time
- reducing advantage of non-steady mining strategies, thereby maximizing
  relative profitability of steady mining


## Specification


### Terms and conventions

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT",
"SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" in this
document are to be interpreted as described in RFC 2119.

In mathematical formulas, operators like +, -, /, *, and ^ (denoting
exponentiation) are used conventially. In pseudo-code, the ^ symbol for
exponentiation only occurs in comments. Integer exponentiation is denoted
in the pseudo-code by the `pow(x, y)` function.

The 'pre-fork block' is customarily considered to be the parent of the
first block mined according to the new consensus rules. The first block
mined according to new consensus rules is referred to as the 'fork block'.


### Requirements

#### REQ-ASERT-TARGET-COMPUTATION (target computation)

The next block's 'target' bits SHALL be calculated by an implementation
of the algorithm below.

The 'evaluation block' height SHALL be greater than or equal to the
anchor block height.

The `aserti3-2d` algorithm seeks to implement the following computation:

```
next_target = old_target * 2^((time_delta - ideal_block_time * (height_delta + 1)) / halflife)
```

where the meaning of the parameters / variables is:

- `old_target` is the unsigned 256 bit integer equivalent of the nBits value in
  the header of the anchor block
- `time_delta` is the difference, in signed integer seconds, between the
  timestamp in the header of the evaluation block and the timestamp in the
  parent of the anchor block
- `ideal_block_time` is a constant: 600 (seconds) representing the targeted
  average time between blocks
- `height_delta` is the difference in block height between the evaluation
  block and the anchor block
- `halflife` is a constant parameter sometimes referred to as
  'tau', with a value of 172800 (seconds) on mainnet
- `next_target` is the integer value of the target computed for the next block
  after the evaluation block)

The algorithm below implements the above using fixed-point integer
arithmetic and a cubic polynomial approximation to the 2^x term.

The 'target' values used as input and output are the compact representations
of actual 256-bit integer targets as specified for the 'nBits' field in the
block header.


Pseudo-code:

```
ALGORITHM aserti3-2d is
    INPUT:  anchor block height h_ref,       ; a block height (0, the genesis block height, is not permitted)
            anchor block parent time t_ref,  ; timestamp (nTime) of parent of anchor block
            anchor block bits b_ref,         ; 'nBits' value of anchor block
            evaluation block height h_eval,  ; height of block at which next target is to be evaluated
            evaluation block time t_eval     ; timestamp of block at which next target is to be evaluated
    OUTPUT: next block bits b_next           ; the 'target' nBits of the next block
    PRECONDITION:  (h_eval >= h_ref > 0) AND (0 < bits_to_target(b_ref) <= max_target)
    POSTCONDITION: (0 < bits_to_target(OUTPUT) <= max_target)
    CONSTANTS: ideal_block_time = 600  ; seconds
               halflife = 172800       ; 2 days (in seconds) on mainnet
               radix = 65536           ; pow(2, 16) , 16 bits for decimal part of fixed-point integer arithmetic
               max_bits = 486604799    ; maximum target in bits representation (0x1d00ffff)
               max_target = bits_to_target( max_bits )  ; maximum target as integer

    target_ref ← bits_to_target( b_ref )  ; convert anchor block nBits to integer
    time_delta ← t_eval - t_ref  ; can be negative
    height_delta ← h_eval - h_ref
    ; Use truncating division - see note 3 below
    exponent ← trunc_div(((time_delta - ideal_block_time * (height_delta + 1)) * radix), halflife)

    ; Compute equivalent of `num_shifts ← floor(exponent / 2^16)`
    num_shifts ← shift_right(exponent, 16)  ; must be arithmetic shift [4]
    exponent ← exponent - num_shifts * radix
    factor ← shift_right(   195766423245049 * exponent
                          + 971821376 * pow(exponent, 2)
                          + 5127 * pow(exponent, 3)
                          + pow(2, 47), 48) + 65536

    next_target ← target_ref * factor

    ; The following if-else construct is equivalent to `next_target ← floor(next_target * 2^factor)`
    IF num_shifts < 0 THEN
        next_target ← shift_right(next_target, -num_shifts)
    ELSE
        ; Implementations should be careful of overflow here (see note 6 below).
        next_target ← shift_left(next_target, num_shifts)
    END IF
    next_target ← shift_right(next_target, 16)

    IF next_target == 0 THEN
        RETURN target_to_bits(1)   ; hardest valid target
    END IF
    IF next_target > max_target THEN
        RETURN max_bits            ; limit on easiest target
    END IF
    RETURN target_to_bits(next_target)
```

Note 1: The reference implementations make use of signed integer arithmetic.
        Alternative implementations may use strictly unsigned integer
        arithmetic.

Note 2: All implementations should strictly avoid use of floating point
        arithmetic in the computation of the exponent.

Note 3: In the calculation of the exponent, truncating integer division [7, 10]
        must be used, as indicated by the `trunc_div` division operator, the
        result of which shall be a signed integer value. Languages such as
        Python, which default to floor division, may need to use an idiom like
        `int(a / b)` instead of `(a // b)`.

Note 4: Integer exponentiation of `x` to the power of `y` is indicated in the
        pseudo-code by the `pow(x, y)` function.

Note 5: The convenience functions `bits_to_target()` and `target_to_bits()`
        are assumed to be available for conversion between compact 'nBits'
        and unsigned 256-bit integer representations of targets.
        Examples of such functions are available in the C++ and Python3
        reference implementations.

Note 6: If a limited-width integer type is used for `next_target`, then the `shift_left`
        may cause an overflow exception or silent discarding of most-significant bits.
        Implementations must detect and handle such cases to correctly emulate
        the behaviour of an unlimited-width calculation. Note that if the result
        at this point would exceed `radix * max_target` then `max_bits` may be returned
        immediately.

Note 7: The polynomial approximation that computes `factor` must be performed
        with 64 bit unsigned integer arithmetic or better.  It *will*
        overflow a signed 64 bit integer.  Since exponent is signed, it may be
        necessary to cast it to unsigned 64 bit integer. In languages like
        Java where long is always signed, an unsigned shift `>>> 48` must be
        used to divide by 2^48.


#### REQ-ASERT-ACTIVATION (activation method)

The ASERT algorithm SHALL be activated using the standard median-time-past
(MTP) [3] network upgrade mechanism.


#### REQ-ASERT-ANCHOR-BLOCK (anchor block)

ASERT requires the choice of an anchor block to schedule future target
computations.

The first block with an MTP that is greater/equal to the upgrade activation time
SHALL be used as the anchor block for subsequent ASERT calculations.

This corresponds to the last block mined under the pre-ASERT DAA rules.

Note 1: The anchor block is the block whose height and target
        (nBits) are used as the 'absolute' basis for ASERT's
        scheduled target. The timestamp (nTime) of the anchor block's
        *parent* is used.

Note 2: The height, timestamp, and nBits of this block are not known ahead of
        the upgrade. Implementations MUST dynamically determine it across the
        upgrade. Once the network upgrade has been consolidated by
        sufficient chain work or a checkpoint, implementations can simply
        hard-code the known height, nBits and associated (parent) timestamp
        this anchor block. Implementations MAY also hard-code other equivalent
        representations, such as an nBits value and a time offset from the
        genesis block.


#### REQ-ASERT-TESTNET-DIFF-RESET (testnet difficulty reset)

On testnet, an additional rule SHALL be included: Any block with a timestamp
that is more than 1200 seconds after its parent's timestamp MUST use an
nBits value of `max_bits` (`0x1d00ffff`).

#### REQ-ASERT-TESTNET-HALF-LIFE

On testnet, a ``halflife` value of 3600 (seconds) SHALL be used.

#### REQ-ASERT-TESTNET-ACTIVATION

Activation parameters for testnet SHALL be the same as for mainnet.


## Rationale and commentary on requirements / design decisions

### 1. Choice of activation method

   Activation of new consensus rules based on MTP has been the established
   method for past Bitcoin Cash upgrades and all implementations can be
   expected to have facilities to comply with such activation.

   Additionally, the upgrade timestamp for November has already been set,
   so mandating a different activation criterion for the DAA change would
   result in the possibility of additional chain forks beyond the usual
   historic chain resulting from the planned upgrade.
   Avoiding additional forks is desirable.

### 2. Choice of anchor block determination

   Choosing an anchor block that is far enough in the past would result
   in slightly simpler coding requirements but would create the possibility
   of a significant difficulty adjustment at the upgrade.

   The last block mined according to the old DAA was chosen since this block is
   the most proximal anchor and allows for the smoothest transition to the new
   algorithm.

### 3. Avoidance of floating point calculations

   Compliance with IEEE-754 floating point arithmetic is not generally
   guaranteed by programming languages on which a new DAA needs to be
   implemented. This could result in floating point calculations yielding
   different results depending on compilers, interpreters or hardware.

   It is therefore highly advised to perform all calculations purely using
   integers and highly specific operators to ensure identical difficulty
   targets are enforced across all implementations.

### 4. Choice of half-life

   A half-life of 2 days (`halflife = 2 * 24 * 3600`), equivalent to an e^x-based
   time constant of `2 * 144 * ln(2)` or aserti3-415.5, was chosen because it reaches
   near-optimal performance in simulations by balancing the need to buffer
   against statistical noise and the need to respond rapidly to swings in price
   or hashrate, while also being easy for humans to understand: For every 2 days
   ahead of schedule a block's timestamp becomes, the difficulty doubles.

### 5. Choice of approximation polynomial

   The DAA is part of a control system feedback loop that regulates hashrate,
   and the exponential function and its integer approximation comprise its
   transfer function. As such, standard guidelines for ensuring control system
   stability apply. Control systems tend to be far more sensitive to
   differential nonlinearity (DNL) than integral nonlinearity (INL) in their
   transfer functions. Our requirements were to have a transfer function that
   was (a) monotonic, (b) contained no abrupt changes, (c) had precision and
   differential nonlinearity that was better than our multi-block statistical
   noise floor, (d) was simple to implement, and (e) had integral nonlinearity
   that was no worse than our single-block statistical noise floor.

   A simple, fast to compute cubic approximation of 2^x for 0 <= x < 1 was
   found to satisfy all of these requirements. It maintains an absolute error
   margin below 0.013% over this range [8]. In order to address the full
   (-infinity, +infinity) domain of the exponential function, we found the
   `2^(x + n) = 2^n * 2^x` identity to be of use. Our cubic approximation gives
   the exactly correct values `f(0) == 1` and `f(1) == 2`, which allows us to
   use this identity without concern for discontinuities at the edges of the
   approximation's domain.

   First, there is the issue of DNL. Our goal was to ensure that our algorithm
   added no more than 25% as much noise as is inherent in our dataset. Our
   algorithm is effectively trying to estimate the characteristic hashrate over
   the recent past, using a 2-day (~288-block) half-life. Our expected
   exponential distribution of block intervals has a standard deviation (stddev)
   of 600 seconds. Over a 2-day half-life, our noise floor in our estimated
   hashrate should be about `sqrt(1 / 288) * 600` seconds, or 35.3 seconds. Our
   chosen approximation method is able to achieve precision of 3 seconds in most
   circumstances, limited in two places by 16-bit operations:
       `172800 sec / 65536 = 2.6367 sec`
   Our worst-case precision is 8 seconds, and is limited by the worst-case
   15-bit precision of the nBits value. This 8 second worst-case is not within
   the scope of this work to address, as it would require a change to the block
   header. Our worst-case step size is 0.00305%,[11] due to the worst-case
   15-bit nBits mantissa issue. Outside the 15-bit nBits mantissa range, our
   approximation has a worst-case precision of 0.0021%. Overall, we considered
   this to be satisfactory DNL performance.

   Second, there is the issue of INL. Simulation testing showed that difficulty
   and hashrate regulation performance was remarkably insensitive to
   integral non-linearity. We found that even the use of `f(x) = 1 + x` as an
   approximation of `2^x` in the `aserti1` algorithm was satisfactory when
   coupled with the `2^(x + n) = 2^n * 2^x` identity, despite having 6%
   worst-case INL.[12][13] An approximation with poor INL will still show good
   hashrate regulation ability, but will have a different amount of drift for a
   given change in hashrate depending on where in the [0, 1) domain our exponent
   (modulo 1) lies. With INL of +/- 1%, for any given difficulty (or target), a
   block's timestamp might end up being 1% of 172800 seconds ahead of or behind
   schedule. However, out of an abundance of caution, and because achieving
   higher precision was easy, we chose to aim for INL that would be comparable
   to or less than the typical drift that can be caused by one block. Out of
   a 2-day half-life window, one block's variance comprises:
       `600 / 172800 = 0.347%`
   Our cubic approximation's INL performance is better than 0.013%,[14] which
   exceeds that requirement by a comfortable margin.

### 6. Conversion of difficulty bits (nBits) to 256-bit target representations

   As there are few calculations in ASERT which involve 256-bit integers
   and the algorithm is executed infrequently, it was considered unnecessary
   to require more complex operations such as doing arithmetic directly on
   the compact target representations (nBits) that are the inputs/output of
   the difficulty algorithm.

   Furthermore, 256-bit (or even bignum) arithmetic is available in existing
   implementation and used within the previous DAA. Performance impacts are
   negligible.

### 7. Choice of 16-bits of precision for fixed-point math

   The nBits format is comprised of 8 bits of base_256 exponent, followed by a
   24-bit mantissa. The mantissa must have a value of at least 0x008000, which
   means that the worst-case scenario gives the mantissa only 15 bits of
   precision. The choice of 16-bit precision in our fixed point math ensures
   that overall precision is limited by this 15-bit nBits limit.

### 8. Choice of name

   The specific algorithm name 'aserti3-2d' was chosen based on:

   - the 'i' refers to the integer-only arithmetic
   - the '3' refers to the cubic approximation of the exponential
   - the '2d' refers to the 2-day (172800 second) halflife


## Implementation advice

Implementations MUST NOT make any rounding errors during their calculations.
Rounding must be done exactly as specified in the algorithm. In practice,
to guarantee that, you likely need to use integer arithmetic exclusively.

Implementations which use signed integers and use bit-shifting MUST ensure
that the bit-shifting is arithmetic.

Note 1: In C++ compilers, right shifting negative signed integers
        is formally unspecified behavior until C++20 when it
        will become standard [5]. In practice, C/C++ compilers
        commonly implement arithmetic bit shifting for signed
        numbers. Implementers are advised to verify good behavior
        through compile-time assertions or unit tests.


## Reference implementations

- C++ code for aserti3-2d (see pow.cpp): <https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/merge_requests/692>
- Python3 code (see contrib/testgen/validate_nbits_aserti3_2d.py): <https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node/-/merge_requests/692>
- Java code: <https://github.com/pokkst/asert-java>


## Test vectors

Test vectors suitable for validating further implementations of the aserti3-2d
algorithm are available at:

  <https://gitlab.com/bitcoin-cash-node/bchn-sw/qa-assets/-/tree/master/test_vectors/aserti3-2d>

and alternatively at:

  <https://download.bitcoincashnode.org/misc/data/asert/test_vectors>


## Acknowledgements

Thanks to Mark Lundeberg for granting permission to publish the ASERT paper [1],
Jonathan Toomim for developing the initial Python and C++ implementations,
upgrading the simulation framework [9] and evaluating the various difficulty
algorithms.

Thanks to Jacob Eliosoff, Tom Harding and Scott Roberts for evaluation work
on the families of EMA and other algorithms considered as replacements for
the Bitcoin Cash DAA, and thanks to the following for review and their
valuable suggestions for improvement:

- Andrea Suisani (sickpig)
- BigBlockIfTrue
- Fernando Pellicioni
- imaginary_username
- mtrycz
- Jochen Hoenicke
- John Nieri (emergent_reasons)
- Tom Zander


## References

[1] "[Static difficulty adjustments, with absolutely scheduled exponentially rising targets (DA-ASERT) -- v2](http://toom.im/files/da-asert.pdf)", Mark B. Lundeberg, July 31, 2020

[2] "[BCH upgrade proposal: Use ASERT as the new DAA](https://read.cash/@jtoomim/bch-upgrade-proposal-use-asert-as-the-new-daa-1d875696)", Jonathan Toomim, 8 July 2020

[3] Median Time Past is described in [bitcoin.it wiki](https://en.bitcoin.it/wiki/Block_timestamp).

[4] <https://en.wikipedia.org/wiki/Arithmetic_shift>

[5] <https://en.cppreference.com/w/cpp/language/operator_arithmetic>

[6] "[Unstable Throughput: When the Difficulty Algorithm Breaks](https://arxiv.org/pdf/2006.03044.pdf)", Sam M. Werner, Dragos I. Ilie, Iain Stewart, William J. Knottenbelt, June 2020

[7] "[Different kinds of integer division](https://harry.garrood.me/blog/integer-division)", Harry Garrood, blog, 2018

[8] [Error in a cubic approximation of 2^x for 0 <= x < 1](https://twitter.com/MarkLundeberg/status/1191831127306031104)

[9] Jonathan Toomim adaptation of kyuupichan's difficulty algorithm simulator: <https://github.com/jtoomim/difficulty/tree/comparator>

[10] "[The Euclidean definition of the functions div and mod](dl.acm.org/doi/10.1145/128861.128862)", Raymond T. Boute, 1992, ACM Transactions on Programming Languages and Systems (TOPLAS). 14. 127-144. 10.1145/128861.128862

[11] <http://toom.im/bch/aserti3_step_size.html>

[12] [f(x) = (1 + x)/2^x for 0<x<1](https://www.wolframalpha.com/input/?i=f%28x%29+%3D+%281+%2B+x%29%2F2%5Ex+for+0%3Cx%3C1), WolframAlpha.

[13] <https://github.com/zawy12/difficulty-algorithms/issues/62#issuecomment-647060200>

[14] <http://toom.im/bch/aserti3_approx_error.html>

[15] <https://github.com/zawy12/difficulty-algorithms/issues/62#issuecomment-646187957>


## License

This specification is dual-licensed under the Creative Commons CC0 1.0 Universal and
GNU All-Permissive licenses.
