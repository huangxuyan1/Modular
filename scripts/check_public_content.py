"""Flag home paths and emails in added lines, and personal Git email metadata."""

import re
import sys

from changed_lines import added_lines, commit_range, git

HOME_PATH = re.compile(
    r"\b[a-z]:[\\/]+Users[\\/]+[^\\/\s<>\"'`]+"
    r"|(?<![\w./\\-])/(?:Users|home)/[^/\r\n\t<>\"'`]+/", re.IGNORECASE)
APP_DATA = re.compile(
    r"(?<![\w.-])AppData[\\/]"
    r"|[\\/]AppData(?=[\\/\s<>\"'`),;]|$)", re.IGNORECASE)
EMAIL = re.compile(
    r"[a-z0-9.!#$%&'*+/=?^_{|}~-]+@[a-z0-9-]+(?:\.[a-z0-9-]+)+", re.IGNORECASE)


def github_noreply(email):
  return EMAIL.fullmatch(email) and email.endswith("@users.noreply.github.com")


def content_problems(base, head):
  for path, number, line in added_lines(base, head):
    for pattern, label in ((HOME_PATH, "absolute home path"), (APP_DATA, "AppData path")):
      for _ in pattern.finditer(line):
        yield f"{path}:{number}: {label}"
    for match in EMAIL.finditer(line):
      email = match[0].lower()
      if not github_noreply(email) and email != "noreply@anthropic.com":
        yield f"{path}:{number}: email address"


def identity_problems(base, head):
  # Lowercase placeholders give the raw identities, not mailmap replacements.
  log = git("log", "--no-show-signature", "--no-notes", "--no-decorate",
            "--no-color", "--format=%H%x00%ae%x00%ce", f"{base}..{head}", "--")
  for record in log.splitlines():
    commit, author, committer = record.lower().split("\0")
    if not github_noreply(author):
      yield f"{commit}: author email is not GitHub noreply"
    # GitHub's service identity is the committer on GitHub-created merges.
    service = committer.partition("@") == ("noreply", "@", "github.com")
    if not github_noreply(committer) and not service:
      yield f"{commit}: committer email is not GitHub noreply"


def main():
  base, head = commit_range(__doc__)
  problems = [*content_problems(base, head), *identity_problems(base, head)]
  for problem in problems:
    print(problem)
  return 1 if problems else 0


if __name__ == "__main__":
  sys.exit(main())
