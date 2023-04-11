#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import json
import os
import re
import sys

"""
TODO: Add this to build system. For now, manually run to update:
`./data/generate_chip_test_vectors.py --encode ./data/CHIPs/bch_vmb_tests_*chip_*.json > ./data/chip_test_vectors.json.h`
"""

def main():
    """
    Output structure
    [                             <chips_vector>
      { "name": chip_name         <chip>
        "tests" : [               <chip_tests>
          { "name": test_name,    <chip_test>
            "reasons": reasons,
            "tests": [
              test_vector1,
              test_vector2,
            ]
          }
        ]
      }
    ]
    """
    encode = False
    vector_files = sys.argv[1:]
    chips_vector = []
    for vec_file in vector_files:
        if vec_file == "--encode":
            encode = True
            continue
        assert os.path.isfile(vec_file)
        with open(vec_file, "rt", encoding="utf8") as f:
            contents = f.read()
            json_cont = json.loads(contents)
            m = re.match(r'.*bch_vmb_tests_([\w]*)chip_([a-z]*)_([\w]*)\.json', vec_file)
            assert m
            active = m[1] != 'before_'
            chip_name = m[2]
            standardness = m[3].split("_")[0]
            reasons = m[3].endswith("_reasons")
            test_name = standardness
            if not active:
                test_name = "preactivation_" + test_name

            # Get/set chip_tests vector within chips_vector
            exists = False
            for chip in chips_vector:
                if chip["name"] == chip_name:
                    exists = True
            if not exists:
                chips_vector.append({"name": chip_name, "tests": []})
            for chip in chips_vector:
                if chip["name"] == chip_name:
                    chip_tests = chip["tests"]

            # Update or create the named test within chip_test, and set the "reasons" or "tests" content
            exists = False
            for chip_test in chip_tests:
                if chip_test["name"] == test_name:
                    exists = True
                    chip_test["reasons" if reasons else "tests"] = json_cont
            if not exists:
                chip_tests.append({ "name": test_name,
                                    "reasons": json_cont if reasons else [],
                                    "tests": json_cont if not reasons else [] })

    if encode:
        json_bytes = json.dumps(chips_vector).encode(encoding="utf8")
        print("#include <cstdint>\n")
        print("namespace json_tests {")
        print("static const uint8_t chip_test_vectors[] = {")

        ctr = 0

        def formatter(bb: bytes) -> str:
            nonlocal ctr
            ret = f"0x{bb:02x}"
            # Allow for up to 20 items per line
            if ctr >= 20:
                ctr = 0
                ret = "\n" + ret
            ctr += 1
            return ret

        print(", ".join(map(formatter, json_bytes)))
        print("};")
        print("} // namespace json_tests")
    else:
        print(json.dumps(chips_vector, indent=2))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: generate_chip_test_vectors.py [--encode] [chip_test_vector_file, ...]")
        sys.exit(1)

    main()
