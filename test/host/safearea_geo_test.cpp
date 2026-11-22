// Host test for the safe-area geometry (src/flight/SafeAreaGeo.h). Not part of the firmware build.
//   g++ -std=c++17 -I src/flight test/host/safearea_geo_test.cpp -o safearea_geo_test && ./safearea_geo_test
#include <cstdio>
#include <cstdlib>

#include "SafeAreaGeo.h"

using namespace safearea;

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      ++failures;                                 \
      printf("FAIL line %d: ", __LINE__);         \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)
static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

int main() {
  // A 1000 m square; the pilot is in it, 200 m east and 100 m south of its centre. The edges are 300 (east), 700 (west), 400 (south) and
  // 600 (north) m away.
  const Pt sq[4] = {{-500, -500}, {-500, 500}, {500, 500}, {500, -500}};  // clockwise
  // In the pilot's frame the pilot is the origin, so shift the square by (-200, +100).
  Pt p[4];
  for (int i = 0; i < 4; ++i) p[i] = {sq[i].e - 200, sq[i].n + 100};

  CHECK(contains(p, 4, 0, 0), "pilot should be inside");
  CHECK(!contains(p, 4, 1000, 0), "far east should be outside");
  CHECK(!contains(p, 4, 0, 700), "north of the square is outside");

  const Near nr = nearest(p, 4, 0, 0);
  CHECK(near(nr.dist, 300.0f, 0.01f), "nearest edge %.2f, expected 300 (the east edge)", nr.dist);

  // Flying east (090) the boundary is 300 m ahead; north (000) 600 m; west 700 m; south 400 m.
  struct { float brg, want; } rays[] = {{90, 300}, {0, 600}, {270, 700}, {180, 400}};
  for (auto& r : rays) {
    const Pt d = unit(r.brg);
    const float h = rayHit(p, 4, 0, 0, d.e, d.n, 1e6f);
    CHECK(near(h, r.want, 0.5f), "ray %.0f: %.2f, expected %.0f", r.brg, h, r.want);
  }
  // The maximum distance cuts a ray short.
  {
    const Pt d = unit(90);
    CHECK(rayHit(p, 4, 0, 0, d.e, d.n, 250) < 0, "a ray shorter than the edge distance must report none");
  }

  // Concave (an L shape): a point in the notch is outside.
  const Pt L[6] = {{0, 0}, {0, 1000}, {500, 1000}, {500, 500}, {1000, 500}, {1000, 0}};
  CHECK(contains(L, 6, 250, 750), "inside the tall arm");
  CHECK(contains(L, 6, 750, 250), "inside the wide arm");
  CHECK(!contains(L, 6, 750, 750), "the notch is outside");

  // Keeping out: a restricted square 100 m ahead (north), 1000 m wide. The line past its corner (500, 100) is atan(500/100) = 78.7 degrees
  // off; with 3 degrees to spare and whole clock positions (30 degrees) that is 90 (a tie turns right), never a U-turn.
  {
    const Pt r[4] = {{-500, 100}, {-500, 1100}, {500, 1100}, {500, 100}};
    CHECK(!contains(r, 4, 0, 0), "setup: outside the restricted square");
    const Pt d = unit(0);
    CHECK(near(rayHit(r, 4, 0, 0, d.e, d.n, 500.0f), 100.0f, 0.5f), "restricted square is 100 m ahead");
    const float h = avoidHeading(r, 4, 0.0f);
    CHECK(near(h, 90.0f, 0.01f), "avoid heading %.1f, expected 90", h);
    // A small square ahead needs a small turn: 100 m ahead and 100 m wide, the corner (50, 100) is 26.6 degrees off: 26.6 + 3 -> 30.
    const Pt n[4] = {{-50, 100}, {-50, 200}, {50, 200}, {50, 100}};
    const float h2 = avoidHeading(n, 4, 0.0f);
    CHECK(near(h2, 30.0f, 0.01f), "narrow square: avoid heading %.1f, expected 30", h2);
    // Off to one side: the square is to the right of the track, so turning left is the smaller turn.
    const Pt s[4] = {{20, 100}, {20, 200}, {120, 200}, {120, 100}};  // its near corner (20, 100) is 11.3 degrees to the right
    const float h3 = avoidHeading(s, 4, 0.0f);
    CHECK(near(h3, -30.0f, 0.01f), "square just right of the track: avoid heading %.1f, expected 30 to the left", h3);
    // Whatever heading it gives, a line along it must not enter the square.
    const Pt dh = unit(h), dh2 = unit(h2), dh3 = unit(h3);
    CHECK(rayHit(r, 4, 0, 0, dh.e, dh.n, 1e5f) < 0, "line along the avoid heading enters the square");
    CHECK(rayHit(n, 4, 0, 0, dh2.e, dh2.n, 1e5f) < 0, "line along the avoid heading enters the narrow square");
    CHECK(rayHit(s, 4, 0, 0, dh3.e, dh3.n, 1e5f) < 0, "line along the avoid heading enters the right-hand square");
  }

  // Area.
  CHECK(near(areaM2(sq, 4), 1000000.0f, 1.0f), "area %.1f", areaM2(sq, 4));

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
