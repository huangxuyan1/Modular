"""Claude Code guardrail for direct Git commands, not a shell sandbox."""

import json
import os
import re
import shlex
import sys
from pathlib import Path

SEPARATORS = ";&|()\n"
BASH_WORD = r"""(?:'[^']*'|"(?:[^"\\]|\\.)*"|\\.|[^\s;&|()<>'"\\])+"""
PS_WORD = r"""(?:'(?:[^']|'')*'|"(?:[^"`]|`.|"")*"|`.|[^\s;&|()<>'"`])+"""
GIT_VALUE_OPTIONS = {
    "-c", "--config-env", "--git-dir", "--work-tree", "--namespace", "--attr-source",
}
FORCE_OPTIONS = {"--force", "--force-with-lease", "--force-if-includes"}


def shell_commands(command, powershell):
  # Keep quoted commands/messages as arguments; split only on unquoted separators.
  word = PS_WORD if powershell else BASH_WORD
  lexer = (rf"(?P<separator>[{SEPARATORS}])|#[^\n]*|"
           rf"(?P<redirect>\d*(?:>>?|<<?)&?)|(?P<word>{word})")
  args, redirect = [], False
  for token in re.finditer(lexer, command, re.DOTALL):
    if token["separator"]:
      if args:
        yield args
      args, redirect = [], False
    elif token["redirect"]:
      redirect = True
    elif redirect and token["word"]:
      redirect = False
    elif raw := token["word"]:
      if powershell:
        if raw.startswith("'") and raw.endswith("'"):
          value = raw[1:-1].replace("''", "'")
        else:
          value = re.sub(r"`(.)", r"\1", raw.strip('"'), flags=re.DOTALL)
      else:
        value = shlex.split(raw.replace("\\\n", ""))[0] if raw != "\\\n" else None
      if value is not None:
        args.append(value)
  if args:
    yield args


def executable(word):
  return word.replace("\\", "/").rsplit("/", 1)[-1].lower()


def force_push(args):
  options = True
  args = iter(args)
  for arg in args:
    if options and arg == "--":
      options = False
    elif options and arg in {"--push-option", "--receive-pack", "--exec", "--repo"}:
      next(args, None)
    elif options and arg.startswith("--"):
      if arg.partition("=")[0] in FORCE_OPTIONS:
        return True
    elif options and arg.startswith("-"):
      # -o consumes the rest of a short-option group or the next argument.
      flags, push_option, value = arg[1:].partition("o")
      if "f" in flags:
        return True
      if push_option and not value:
        next(args, None)
    elif arg.startswith("+"):
      return True
  return False


def native_path(path, powershell):
  if powershell:
    # PowerShell separates path parts with backslashes on every host.
    path = path.replace("\\", "/")
  elif os.name == "nt":
    # Git Bash converts /drive/... when passing arguments to native git.exe.
    path = re.sub(r"^/([a-zA-Z])(?:/|$)", r"\1:/", path)
  return Path(path)


def blocked_reason(args, cwd, root, powershell):
  args = iter(args)
  for arg in args:
    if executable(arg) in {"git", "git.exe"}:
      break
    if arg not in {"command", "env", "if", "then", "elif", "else", "do", "!"}:
      if not re.match(r"^[a-zA-Z_]\w*=", arg):
        return None
  else:
    return None

  for arg in args:
    if arg == "-C":
      path = next(args, "")
      if path:
        if root is None:
          return "Cannot check git -C without CLAUDE_PROJECT_DIR."
        try:
          cwd = (cwd / native_path(path, powershell)).resolve()
          cwd.relative_to(root)
        except (OSError, RuntimeError, ValueError):
          return "git -C must stay inside CLAUDE_PROJECT_DIR."
    elif arg in GIT_VALUE_OPTIONS:
      next(args, None)
    elif not arg.startswith("-"):
      if arg == "push" and force_push(args):
        return "Force-pushing with git push is blocked."
      break
  return None


def main():
  event = json.load(sys.stdin)
  command = event.get("tool_input", {}).get("command", "")
  powershell = event.get("tool_name") == "PowerShell"
  cwd = native_path(event.get("cwd") or str(Path.cwd()), powershell)
  project = os.environ.get("CLAUDE_PROJECT_DIR")
  root = native_path(project, powershell).resolve() if project else None
  for args in shell_commands(command, powershell):
    if reason := blocked_reason(args, cwd, root, powershell):
      print(reason, file=sys.stderr)
      return 2
  return 0


if __name__ == "__main__":
  sys.exit(main())
