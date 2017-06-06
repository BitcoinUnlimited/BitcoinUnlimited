#ifndef BUIP055FORK_H
#define BUIP055FORK_H

// Validate that the block's contents adhere to the BUIP055 hard fork requirements.
// the requirement that the fork block is >= 1MB is not checked because we do not
// know whether this is the fork block.
extern bool ValidateBUIP055Block(const CBlock &block, CValidationState &state);

// Validate that a transaction adheres to the BUIP055 hard fork requirements.
extern bool ValidateBUIP055Tx(const CTransaction& tx);
#endif
