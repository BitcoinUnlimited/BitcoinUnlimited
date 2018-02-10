%module coreio
%include defaults.i

%include "core_io.h"

%{
    #include "uint256.h"
    #include "script/script.h"
    #include "core_io.h"
%}
