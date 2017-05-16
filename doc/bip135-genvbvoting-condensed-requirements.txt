==BIP-GENVBVOTING concise requirements specification==

This is a condensed form of the requirements from BIP-GENVBVOTING.
It is intended for checking completeness and test status.

REQ-* are requirements, OPT-REQ-* are optional requirements (not mandatory for compliance with BIP-GENVBVOTING).



===Backward compatibility===

REQ-FULL-COMPAT: This specification SHALL enable strict backward compatibility with existing BIP9-based deployments through suitable parameter configuration.
Rationale: To preserve compatibility with BIP9 through configurability.
Note only: Any part of the specification preventing full backward compatibility SHALL be considered
as erroneous and amended.

REQ-VERSIONBITS-PARAMETER: As before, a set of configuration parameters SHALL exist for the version bits for each chain supported by an implementation.
Rationale: This permits each bit to be configured independently for each chain (mainnet, testnet, etc.)



===Fork deployment parameters===

REQ-PARAM-BIT-RANGE: The 'bit' value SHALL be an integer in the range 0..28 .

REQ-PARAM-STARTTIME-RANGE: The 'starttime' value SHALL be an integer greater than or equal to 0.

REQ-PARAM-TIMEOUT-RANGE: The 'timeout' value SHALL be an integer greater than the 'starttime'

REQ-PARAM-WINDOWSIZE-RANGE: The 'windowsize' value SHALL be an integer equal to zero (0) or one (1) for unconfigured deployments, and greater than or equal to two (2) for configured deployments.

REQ-PARAM-THRESHOLD-RANGE: The 'threshold' value SHALL be an integer in the range of 1..'windowsize' .

REQ-PARAM-MINLOCKEDBLOCKS-RANGE: The 'minlockedblocks' value SHALL be an integer greater than or equal to zero (0).

REQ-PARAM-MINLOCKEDTIME-RANGE: The 'minlockedtime' value SHALL be an integer greater than or equal to zero (0).



===Signaling bits===

REQ-BITS-RANGE: The signaling bits SHALL comprise the 29 least significant bits of the
nVersion block header field. nVersion is a 32-bit field which is treated as
a little-endian integer.

REQ-BITS-ZERO-NUMBERED: Signaling bits SHALL be assigned numbers from 0..28 ranging from the least
significant (bit 0) to the most significant (bit 28) in the range.

REQ-TOP3BITS-ID: The top 3 bits of nVersion MUST be set to 001 , yielding a range of possible
nVersion values between [0x20000000...0x3FFFFFFF], inclusive.

REQ-TREAT-BITS-AS-ZERO-IF-NO-ID: If a block's nVersion does not have its top 3 bits set to 001, all its signaling
bits MUST be treated as if they are 0.



===States===

REQ-ANCESTORS-DETERMINE-BLOCK-STATE: A block's state SHALL never depend on its own nVersion, but only on that of its ancestors.

REQ-GENESIS-DEFINED : The genesis block for any chain SHALL by definition be in the DEFINED state for a configured deployment.



===State transitions===

REQ-STATE-TRANSITION-SYNC: State transitions from STARTED->LOCKED_IN and from LOCKED_IN->ACTIVE SHALL only be allowed when the block height is an even multiple of the deployment's 'windowsize'.

REQ-STATE-TRANSITION-DEFINED-TO-STARTED: A configured deployment SHALL remain in the DEFINED state until it either passes the starttime, when it becomes STARTED.

REQ-STATE-TRANSITION-FAILED: A configured deployment SHALL become FAILED if the MTP exceeds the timeout time and the deployment has not become ACTIVE.

REQ-STATE-TRANSITION-LOCKED_IN: If at least 'threshold' out of the last 'windowsize' STARTED blocks have the associated bit set in nVersion (as measured at next height that is evenly divisible by the windowsize), and the MTP of the last block is less than the 'timeout' value, then the fork becomes LOCKED_IN.

REQ-STATE-TRANSITION-ACTIVE: If at least 'minlockedblocks' have passed since the first LOCKED_IN block (inclusive), and the MTP of the previous block is greater or equal than the sum of the MTP of the first LOCKED_IN block plus the 'minlockedtime' (measured at next height that is evenly divisible by the windowsize), then the fork becomes ACTIVE.
Note 1: This describes the grace period conditions that need to be met before a fork can become ACTIVE.
Note 2: The default for both 'minlockedblocks' and 'minlockedtime' are zero, implying that the fork can become active at the next height evenly divisible by the configured window size. This should ensure backward compatibility with BIP9 under suitable configuration.



===Tallying===

REQ-SIGNAL-1-FOR-SUPPORT: A signaling bit value of '1' SHALL indicate support of a fork and SHALL count towards its tally on a chain.

REQ-SIGNAL-0-FOR-NO-SUPPORT: A signaling bit value of '0' SHALL indicate absence of support of a fork and SHALL NOT count towards its tally on a chain.

REQ-NO-ID-NO-TALLY: If a block's nVersion does not have its top 3 bits set to 001, all its signaling bits MUST be treated as if they are '0'.

REQ-TALLY-AFTER-STARTED: Once a deployment has STARTED, the signal for that deployment SHALL be tallied over the the past windowsize blocks whenever a new block is received on that chain.

REQ-TALLY-ON-HEAD-CHANGE: The signaling bits SHALL be tallied whenever the head of the active chain changes, including after reorganizations.



===New consensus rules===

REQ-NEW-CONSENSUS-ON-ACTIVE: New consensus rules deployed by a fork SHALL be enforced starting with the first block that has ACTIVE state.



===Optional operator notifications===

OPT-REQ-NOTIFY-ON-TRANSITIONS: An implementation SHOULD notify the operator when a deployment transitions to STARTED, LOCKED_IN, ACTIVE or FAILED states.

OPT-REQ-FINE-GRAINED-NOTIFICATIONS: It is RECOMMENDED that an implementation provide finer-grained notifications to the operator which allow him/her to track the measured support level for defined deployments.

OPT-REQ-WARN-SIGNAL-OVERRIDES: An implementation SHOULD warn the operator if the configured (emitted) nVersion has been overridden to contain bits set to '1' in contravention of the above non-signaling recommendations for DEFINED forks.

OPT-REQ-WARN-ABSENT-SIGNAL: It is RECOMMENDED that an implementation warn the operator if no signal has been received for a given deployment during a full windowsize period after the deployment has STARTED.
Rationale: This could indicate that something may be wrong with the operator's configuration that is causing them not to receive the signal correctly.

OPT-REQ-ALERT-UNDEFINED: For undefined signals, it is RECOMMENDED that implementation track these and alert their operators with supportive upgrade notifications, e.g.  
* "warning: signaling started on unknown feature on version bit X"
* "warning: signaling on unknown feature reached X% (over last N blocks)"
* "info: signaling ceased on unknown feature (over last M blocks)"

OPT-REQ-CONFIGURABLE-UNKNOWN-ALERTS: Since parameters of unconfigured deployments are unknown, it is RECOMMENDED that implementations allow the user to configure the emission of unknown signal notifications (e.g. suitable N and M parameters in the messages above, e.g. a best-guess window of 100 blocks).



==License==

This requirements specification is dual-licensed under the Creative Commons CC0 1.0 Universal and GNU All-Permissive licenses.
