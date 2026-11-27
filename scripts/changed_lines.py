"""Read added lines from the merge base of two commits to the head commit."""

import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")


def git(*args):
  return subprocess.check_output(["git", *args], cwd=ROOT).decode("utf-8", "replace")


def commit_range(description):
  parser = argparse.ArgumentParser(description=description)
  parser.add_argument("base", nargs="?", default="origin/main")
  parser.add_argument("head", nargs="?", default="HEAD")
  args = parser.parse_args()
  return tuple(
      git("rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}").strip()
      for ref in (args.base, args.head))


def added_lines(base, head, *paths):
  """Yield (path, line number in head, text) without diff markers."""
  patch = git("-c", "core.quotePath=false", "diff", "--no-color", "--no-ext-diff",
              "--no-textconv", "--no-prefix", "--find-renames", "--unified=0",
              f"{base}...{head}", "--", *paths)
  path, number = "", None
  for line in patch.split("\n"):
    if line.startswith("diff --git "):
      path, number = "", None
    elif number is None and line.startswith("+++ "):
      path = line[4:].rstrip("\t").strip('"')
    elif match := HUNK.match(line):
      number = int(match[1])
    elif number is not None and line.startswith(("+", " ")):
      if line.startswith("+"):
        yield path, number, line[1:].rstrip("\r")
      number += 1
