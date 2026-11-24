# Where does the web build live, and how does a built module reach the site?

Type: grilling
Status: open
Blocked by: 02

## Question

midishare deploys by running `vite build`, which cannot run `emcc`, so the site can only ship
a module built somewhere else. Decide where, and what Modular takes on by it.

- **Is the web a supported Modular platform?** Supported means an Emscripten configure in
  CMake, a CI job that builds it on every PR, and a `docs/` page beside `docs/windows.md`.
  The alternative is a recipe a client follows at its own risk. Supported catches a change that
  breaks the web build when it is made; it also makes every later kernel change pay for a
  second toolchain.
- **Where does the glue live?** The exported functions and their JavaScript loader are client
  code. In this repository they would be an `apps/` target, and `AGENTS.md` puts client policy
  in `apps/`. But which instrument and how many voices is midishare's policy, so a glue layer
  that fixes them in C++ puts midishare's policy in Modular. How far that matters depends on
  [What does the page hand Modular, and what does it get back?](04-page-to-modular-contract.md).
- **How does a module reach the site?** Candidates: built here and committed into the midishare
  repository with the Modular commit recorded beside it; or built by Modular's CI and published
  as a versioned artifact (a release asset or a package) that midishare pins. Either way the
  site states which Modular commit it plays.
- Does any of this need cross-origin isolation, or change midishare's headers? With offline
  render and no threads, it should not; confirm from the probe.

The probe supplies the facts this rests on: which libraries build, what had to change, and
how large the module is.
