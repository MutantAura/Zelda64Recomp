#!/usr/bin/env python3
# Prints a function's definition from a decomp source file, for use when writing patches.
# Usage: oot/tools/extract_func.py <source file> <function name>
import re
import sys

def extract(path, name):
    text = open(path, encoding="latin-1").read()
    match = re.search(r"^[^\n;]*\b" + re.escape(name) + r"\([^;{]*\)\s*\{", text, re.MULTILINE)
    if match is None:
        raise SystemExit(f"Function {name} not found in {path}")
    depth = 0
    for i in range(match.end() - 1, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():i + 1]
    raise SystemExit("Unbalanced braces")

if __name__ == "__main__":
    print(extract(sys.argv[1], sys.argv[2]))
