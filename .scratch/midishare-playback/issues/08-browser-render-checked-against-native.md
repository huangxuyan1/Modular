# How is a browser render checked against a native one?

Type: grilling
Status: open
Blocked by: 02

## Question

Offline renders are Modular's primary verification path because they are comparable sample
for sample. A web build adds a second compiler and a second maths library, so the same graph
may not render the same bits in the browser as on Windows: `std::sin` and `std::pow` differ
between libraries, and floating-point contraction can differ between compilers.

Decide, using the probe's measured difference:

- Is the claim "a browser render equals the native render, sample for sample", or "equal
  within a stated tolerance"? The first is a stronger guarantee and may cost a portable maths
  routine in the nodes that need one.
- Where does the check run? An Emscripten build also runs under Node.js, so CTest could run the
  web build's render against a native reference without a browser.
- Is that check part of `CI gate`, and therefore part of the decision in
  [Where does the web build live, and how does a built module reach the site?](03-where-the-web-build-lives.md)?
