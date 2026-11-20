"""Documentation checks for the CI gate; also runnable locally.

Checks every tracked Markdown file for repository-internal links whose target
file or heading anchor does not exist, and that AGENTS.md and CLAUDE.md are
byte-identical. External (scheme) links are not fetched: that check would
depend on the network and other sites. Exits non-zero on any problem.

Usage, from anywhere in the checkout:  python scripts/check_docs.py
"""

import re
import subprocess
import sys
import urllib.parse
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
IDENTICAL_FILES = ("AGENTS.md", "CLAUDE.md")

FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
INLINE_CODE = re.compile(r"(`+)(?:(?!\1).)+?\1")
INLINE_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\[\]]*\])*\]\(\s*(<[^>]*>|[^\s()]*(?:\([^\s()]*\)[^\s()]*)*)")
REFERENCE_DEFINITION = re.compile(r"^ {0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)")
HEADING = re.compile(r"^ {0,3}#{1,6}\s+(.*?)\s*#*\s*$")
HTML_ANCHOR = re.compile(r"<a\s[^>]*\b(?:id|name)\s*=\s*[\"']([^\"']+)[\"']", re.IGNORECASE)
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")


def tracked_files():
  output = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True,
                          capture_output=True).stdout
  return [PurePosixPath(name) for name in output.decode("utf-8").split("\0") if name]


def prose_lines(text):
  """Yield (line number, line) for lines outside fenced code blocks."""
  fence = None
  for number, line in enumerate(text.splitlines(), 1):
    match = FENCE.match(line)
    if fence:
      if match and match.group(1)[0] == fence[0] and len(match.group(1)) >= len(fence):
        fence = None
      continue
    if match:
      fence = match.group(1)
      continue
    yield number, line


def slug(heading):
  """GitHub's heading anchor for rendered heading text."""
  text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading).replace("`", "")
  text = re.sub(r"<[^>]+>", "", text).strip().lower()
  text = "".join(ch for ch in text if ch.isalnum() or ch in " -_")
  return text.replace(" ", "-")


def anchors(text):
  found, counts = set(), {}
  for _, line in prose_lines(text):
    found.update(HTML_ANCHOR.findall(line))
    heading = HEADING.match(line)
    if heading:
      base = slug(heading.group(1))
      count = counts.get(base, 0)
      counts[base] = count + 1
      found.add(base if count == 0 else f"{base}-{count}")
  return found


def link_targets(text):
  for number, line in prose_lines(text):
    line = INLINE_CODE.sub("", line)
    for match in INLINE_LINK.finditer(line):
      yield number, match.group(1)
    definition = REFERENCE_DEFINITION.match(line)
    if definition:
      yield number, definition.group(1)


def check_links(files):
  file_set = set(files)
  directories = {parent for name in files for parent in name.parents}
  anchor_cache = {}

  def anchors_of(path):
    if path not in anchor_cache:
      anchor_cache[path] = anchors((ROOT / path).read_text(encoding="utf-8"))
    return anchor_cache[path]

  problems = []
  for source in (name for name in files if name.suffix.lower() == ".md"):
    text = (ROOT / source).read_text(encoding="utf-8")
    for number, target in link_targets(text):
      target = target.strip("<>")
      if not target or SCHEME.match(target):
        continue
      path_part, _, fragment = target.partition("#")
      path_part = urllib.parse.unquote(path_part)
      if path_part:
        base = PurePosixPath("/") if path_part.startswith("/") else PurePosixPath("/", source.parent)
        resolved = PurePosixPath(*_normalize(base / path_part).parts[1:])
      else:
        resolved = source
      if resolved not in file_set and resolved not in directories:
        problems.append(f"{source}:{number}: link target not in the repository: {target}")
      elif fragment and resolved.suffix.lower() == ".md" and resolved in file_set:
        if urllib.parse.unquote(fragment).lower() not in anchors_of(resolved):
          problems.append(f"{source}:{number}: no heading for anchor: {target}")
  return problems


def _normalize(path):
  parts = []
  for part in path.parts[1:]:
    if part == "..":
      if parts:
        parts.pop()
    elif part not in ("", "."):
      parts.append(part)
  return PurePosixPath("/", *parts)


def check_identical():
  first, second = (ROOT / name for name in IDENTICAL_FILES)
  if first.read_bytes() != second.read_bytes():
    return [f"{IDENTICAL_FILES[0]} and {IDENTICAL_FILES[1]} must be byte-identical"]
  return []


def main():
  files = tracked_files()
  problems = check_links(files) + check_identical()
  for problem in problems:
    print(problem)
  markdown = sum(1 for name in files if name.suffix.lower() == ".md")
  print(f"check_docs: {markdown} Markdown files, {len(problems)} problem(s)")
  return 1 if problems else 0


if __name__ == "__main__":
  sys.exit(main())
