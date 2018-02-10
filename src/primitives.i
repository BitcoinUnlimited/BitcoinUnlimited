%module primitives
%include defaults.i

%ignore csBlockHashToIdx;
%ignore CBlockHeader::CURRENT_VERSION;

%include "uint256.h"

%include "primitives/transaction.h"
%include "primitives/block.h"

%{
    #include "uint256.h"
    #include "primitives/transaction.h"
    #include "primitives/block.h"
%}
