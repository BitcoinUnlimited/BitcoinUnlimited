%module script
%include defaults.i

// FIXME: get SWIG to accept these properly
%ignore CScript(const_iterator, const_iterator);
%ignore CScript::IsPushOnly;

%template(byte_vector) std::vector<unsigned char>;

%include "prevector.h"
%include "script/script.h"

%{
    #include "prevector.h"
    #include "script/script.h"
    typedef CScript::const_iterator const_iterator;
    typedef CScript::iterator iterator;
%}
