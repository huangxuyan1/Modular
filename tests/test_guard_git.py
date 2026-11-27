"""Exercise the Git guard's stdin JSON and exit-code contract without running Git."""

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOOK = ROOT / "scripts/hooks/guard_git.py"


class GuardGitTest(unittest.TestCase):
  def test_commands(self):
    cases = (
        ("git push origin main", 0),
        ("git push --force", 2),
        ("git push -f origin main", 2),
        ("git push origin +main", 2),
        ("git push --force-with-lease", 2),
        ("git push --force-with-lease=main", 2),
        ("git push --force-if-includes", 2),
        ("git push -vf origin main", 2),
        ("git push -o +value origin main", 0),
        ("git -C ../other status", 2),
        ("git -C .claude/worktrees/x status", 0),
        ('git -C "../other project" status', 2),
        ('git -C "./inside project" status', 0),
        ("git -C .claude -C ../.. status", 2),
        ("git -c color.ui=never push --force", 2),
        ("git status", 0),
        ("git status && git push '-f'", 2),
        ("git push --force>/dev/null", 2),
        ("git push >/dev/null --force", 2),
        ("git status\ngit push --force", 2),
        ('git commit -m "mention git push --force; do not run it"', 0),
        ("git status # git push --force", 0),
    )
    for command, expected in cases:
      self.check_command("Bash", command, expected)

  def test_powershell(self):
    self.check_command("PowerShell", r"Get-Date; git.exe -C ..\other status", 2)
    self.check_command("PowerShell", r"git -C .claude\worktrees\x status", 0)
    self.check_command("PowerShell", "& 'git.exe' push --force", 2)

  def check_command(self, tool, command, expected):
    with self.subTest(tool=tool, command=command):
      event = {"tool_name": tool, "cwd": str(ROOT), "tool_input": {"command": command}}
      result = subprocess.run([sys.executable, str(HOOK)], cwd=ROOT, text=True,
                              input=json.dumps(event), capture_output=True,
                              env={**os.environ, "CLAUDE_PROJECT_DIR": str(ROOT)})
      self.assertEqual(result.returncode, expected, result.stderr)
      self.assertEqual(result.stdout, "")
      if expected:
        self.assertEqual(len(result.stderr.splitlines()), 1)
      else:
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
  unittest.main()
