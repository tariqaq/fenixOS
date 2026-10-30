// Host test: parse the sample package built by crates/fenix-pkg, then make sure broken copies
// are rejected and that flipping any byte never crashes the reader. Run with run.ps1.
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "fenix_pkg.h"

static int fails;
static void expect(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL %s\n", what);
    fails++;
  }
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 2;
  std::vector<uint8_t> b;
  int c;
  while ((c = fgetc(f)) != EOF) b.push_back((uint8_t)c);
  fclose(f);

  fpkg::Package p;
  const char* err = p.parse(b.data(), b.size());
  expect(!err, err ? err : "");
  if (err) return 1;
  expect(p.id == "io.github.tariqaq.sample", "id");
  expect(p.version == "0.1.0", "version");
  expect(p.elems.size() == 15 && p.binds.size() == 6 && p.assets.size() == 2, "counts");
  expect(p.head.icon_small == 0 && p.head.icon_large == 1, "icons");

  // Assets are not read, only located: the offset points at the pixels in the package.
  const fpkg::Asset& last = p.assets.back();
  expect(last.offset + last.len == b.size(), "asset offset");

  // The same package read from the file in pieces (the board's path since P39).
  f = fopen(argv[1], "rb");
  fpkg::FileSource src(f, b.size());
  fpkg::Package q;
  const char* ferr = q.parse(src, b.size());
  fclose(f);
  expect(!ferr && q.id == p.id && q.elems.size() == p.elems.size() && q.strings == p.strings, "file source");
  fpkg::Package moved = std::move(q);
  expect(moved.name == p.name, "move keeps views");

  // Portrait override: element 1 is a row that becomes a column in portrait.
  int32_t flow = -1;
  expect(p.elems[1].prop(fpkg::key::FLOW, &flow) && flow == 1, "flow landscape");
  expect(p.elems[1].prop(fpkg::key::FLOW, &flow, true) && flow == 0, "flow portrait");

  std::string_view id, ver;
  expect(fpkg::peek(b.data(), 200, &id, &ver) && id == p.id && ver == "0.1.0", "peek");
  expect(!fpkg::peek(b.data(), 40, &id, &ver), "peek short");

  expect(p.parse(b.data(), b.size() - 1) != nullptr, "truncated");
  std::vector<uint8_t> t = b;
  t.push_back(0);
  expect(p.parse(t.data(), t.size()) != nullptr, "trailing byte");
  expect(p.elems.empty() && p.strings.empty() && p.id.empty(), "failed parse leaves it empty");
  t = b;
  t[0] = 'X';
  expect(p.parse(t.data(), t.size()) != nullptr, "magic");

  // Flip every byte in turn: parse must return (ok or error) without crashing.
  int accepted = 0;
  for (size_t i = 0; i < b.size(); i++) {
    t = b;
    t[i] ^= 0xFF;
    if (!p.parse(t.data(), t.size())) accepted++;
  }
  printf("fenix_pkg: sample ok, %zu flips, %d still parse (prop values, pixels)\n", b.size(), accepted);
  printf(fails ? "%d FAILED\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
