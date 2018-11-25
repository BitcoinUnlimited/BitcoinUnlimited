import pickle
from os.path import dirname, join

pickle_file = "xversion.pickle"

table = pickle.load(open(join(
    dirname(__file__), pickle_file),
                         "rb"))

def assert_in_table(s):
    if s not in table:
        raise RuntimeError("Xversion key '%s' is unknown." % s)

def key(s):
    assert_in_table(s)
    return ((table[s]['prefix']<<16) +
            table[s]['suffix'])

def valtype(s):
    assert_in_table(s)
    return table[s]['valtype']
