#!/usr/bin/env python3

import numpy as np
import time
from os import system
from sys import stdout

ntx_values = np.array(10.**np.linspace(0, 6, 20), int)

for test in range(4):
    for ntx in ntx_values:
        a = time.time()
        system("./bitcoin-tx %d %d < ~/blocks-400000-409999  > /dev/null" % (ntx, test))
        b = time.time()

        print("%3d %10d %8f" % (test, ntx, (b-a)/100.))
        stdout.flush()
