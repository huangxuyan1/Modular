"""Check the 90-column limit on added source and build-script lines."""

import sys

from changed_lines import added_lines, commit_range

PATHS = ("*.cpp", "*.h", "*.hpp", "CMakeLists.txt", "*.cmake", "*.py", "*.mjs", "*.js")
LIMIT = 90


def main():
  base, head = commit_range(__doc__)
  status = 0
  for path, number, line in added_lines(base, head, *PATHS):
    columns = len(line.expandtabs(8))
    if columns > LIMIT:
      print(f"{path}:{number}: {columns} columns (maximum {LIMIT})")
      status = 1
  return status


if __name__ == "__main__":
  sys.exit(main())
