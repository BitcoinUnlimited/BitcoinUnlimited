# XVersionMessage: BCH node extended version and configuration fields

DRAFT specification
Version: 0.1.0

## Overview

Using the `version` message in the Bitcoin protocol (for more details,
see https://en.bitcoin.it/wiki/Protocol_documentation#version), peers
announce their capabilities and preferences to each other. The
`version` message is limited in scope to a fixed set of well-known
fields.

It is deemed desirable to be able to announce further configuration
and version information to peers. For example, this can then be used
to version advanced block transmission protocols like Graphene.

Notably, the `version` message can not be extended as there are
clients on the current BCH network that will consider an oversized
`version` message illegal (e.g. Bitcoin XT).

For this reason, an additional message type named `xversion` is
specified here in. The `xversion` message transports a generic
key-value map that is meant to hold the configuration and version
parameters.

As a general reminder, it should be noted that the values given in
this extended `xversion` version and configuration map are, except for
basic type checks and mappings, not in any way validated and a node
can very easily lie about them. Care should be taken that any
implementation depending for its configuration on the values in the
xmap will properly account for this fact and ban or otherwise penalize
nodes that do not conform with their advertised behavior.

## Encoding of the `xversion` message type

The `xversion` message contains a single compound field which is a
serialized key-value map henceforth named `xmap` that maps 64-bit
integer values to variable-sized byte vectors. The message itself is
encoded in the standard Bitcoin message frame that is documented elsewhere.

The `map` is encoded using Bitcoin's usual network de-/serialization
schemes while using `COMPACTSIZE` size encodings whenever possible.
Note that this is different from using the default encoding you would
get when serializing through the `std::map` serializer (which is
commonly found in `serialize.h` in most current C++ implementations based
on the original Satoshi code).

In particular the `xmap` payload of an `xversion` message looks like this
for `N` entries in the map:
```
[compact-size N]
[compact-size key0] [std::vector<uint8_t> default-encoding value0]
[compact-size key1] [std::vector<uint8_t> default-encoding value1]
...
[compact-size key<N-1>] [std::vector<uint8_t> default-encoding value<N-1>]
```

where `std::vector<uint8_t> default-encoding` is the default encoding
one gets when serializing a std::vector<uint8_t> of size `M` bytes through the
serialization templates usually found in `serialize.h`:

```
[compact-size M]
[uint8_t value0]
...
[uint8_t value<M-1>]
```

A node may serialize the `xmap` it wants to announce to its peers with
any order of the key-value pairs. When receiving an `xmap`, entries
with duplicate keys that are later in the message should override
values that are that are closer to the start of the message.

### Size limit

The *serialized* size of the `xversion` payload has to be of size
`100000` bytes (100KB, not 100KiB !) or less. Messages that are larger
than this are illegal.

### Later extensions

Implementations should allow extra bytes following the defined fields
in the `xversion` message to allow for further extensibility.


## Handling and sequencing of `xversion` messages

A node should expect an `xversion` message to arrive after the
`version` message.  Only a single `xversion` message should arrive
during the lifetime of a connection and receival of multiple such
messages should be considered misbehavior of the remote peer.

After receival of an `xversion` message, a node must answer with an
empty `verack` message to confirm recept.

An empty `xmap` for a peer has a defined reading (see below).
However, to simplify node implementations, it is deemed acceptable to
enable certain protocol features only after proper receipt of a
corresponding `xversion` message.

## Interpretation of the `xmap`

If a key cannot be found in the `xmap`, its value must be assumed to be an
empty byte vector. An entry that has unknown meaning is to be ignored.

If further structuring of a byte-vector value is desired, it should be
done using the standard Bitcoin de-/serialization format.

For obvious reasons, the enumeration of entries and their expected
types in the map should be agreed upon between implementations. For
this reason, a separate specification that lists configuration and
version keys is introduced, named the `xmap directory` (see below).

The format of `std::vector<uint8_t>` is unwieldy for configuration and
version parameters that could be expressed as simple integer to
integer mappings. For this reason, a specific interpretation for
certain value types is assumed. The list of these value types is
expected to grow over time and this document correspondingly
extended. For now, only a compact-size encoded unsigned 64-bit integer
value named `u64c` is specified.

### The `u64c` value type

Values in the `xmap` can be interpreted as compact-size encoded 64-bit
unsigned integers (named `u64c`), allowing to use it as a simple
unsigned to unsigned integer map.

A value of `type` is encoded in Bitcoin's message serialization as a
COMPACTSIZE integer *within* the generic byte vector value of the `xmap`.
For example, a value of `0x1000` (4096) is encoded like this as the `xmap`
value:
`FD 00 10`

Which gets further encoded in the `xmap` encoding itself as:
`03 FD 00 10`

A special exception is added for empty value vectors. Empty value
vectors are to be interpreted as value zero in the `u64c`
reading. Thus missing entries in the `xmap` are interpreted as `u64c`
zero values.

When interpreting these `uint64_t` values from the table and certain
bits of the value are unused for a given key, the implementer should
mask out the needed bits using an AND-mask to allow for future use of
the yet unused bits in a given value.

If decoding as a compact size integer fails, a value of zero for
the corresponding value should be assumed.

## The `xmap` directory

A directory that lists the currently defined key values comes along
with this specification and can be found in the file named
`xversionkeys.h`.

At the moment, and for more compact wire encoding, keys are assumed to
be just 32-bit in size and the current specification format reflects
this. They are assumed to consist of a 16-bit prefix in the more
significant bits of the 64-bit integer (bits 16..31) followed by a
16-bit suffix in the least significant bits (0..15).

Different prefixes are meant for different implementations so that
each implementation can extend the version map without needing
frequent synchronization with other implementations and while avoiding
double-assignments. Of course, regular integration into a common
directory should take place. The specifics of this and the exact,
detailed interpretation of the fields in the `xmap` will obviously
rely on external specifications that cannot ever be the scope of this
document.

## Implementation prefixes

The implementations listed here should use the following prefixes:

`ABC               0x0000`
`bcash             0x0001`
`BU                0x0002`
`XT                0x0003`
`bchd              0x0004`
`bitcore           0x0005`
`bitprim           0x0006`
`Parity Cash       0x0007`


An implementation not listed here, but wanting to extend the `xversion`
map can pick an unused prefix but is strongly suggested to communicate
the choice with the rest of the teams as early as possible.

Experimental or temporary features should use the `0x1000` prefix.

(FIXME: List all known implementations. Feedback welcome)

## Notes on Bitcoin Unlimited implementation details

In the Bitcoin Unlimited reference implementation, the `xversion`
message is handled using the `CXVersionMessage` class. The actual
`xmap` is serialized and deserialized using the
`CompactMapSerialization` adapter class.  To avoid attacks that could
cause a slow-down of key lookup in the `xmap` tables, the table is
internally using a salted siphash to map the keys.  The implementation
can be found in the files `src/xversionmessage.h` and
`src/xversionmessage.cpp` relative to the source root directory.
