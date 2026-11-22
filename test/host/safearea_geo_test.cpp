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
  // A 1000 m square, the pilot in it at (200, -100) from its centre; the edges are 300 / 700 east-west and 400 / 600 north-south away.
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

  // Inward normal of the east edge of the square points west.
  {
    const Near e = nearest(p, 4, 290, 0);  // close to the east edge (x = 300)
    const Pt n = inwardNormal(p, 4, e.edge, e.at);
    CHECK(near(n.e, -1.0f, 0.01f) && near(n.n, 0.0f, 0.01f), "inward normal (%.2f, %.2f), expected (-1, 0)", n.e, n.n);
  }

  // Escape heading: flying east, 150 m short of the east edge, with 300 m to keep clear: the smallest turn that stays inside that far is
  // 60 degrees (150 / cos 60 = 300), not a U-turn.
  {
    Pt q[4];
    for (int i = 0; i < 4; ++i) q[i] = {sq[i].e - 350, sq[i].n};  // pilot 150 m from the east edge, mid-height
    CHECK(contains(q, 4, 0, 0), "setup: inside");
    const Pt d = unit(90);
    int edge = -1;
    const float hit = rayHit(q, 4, 0, 0, d.e, d.n, 1e6f, &edge);
    CHECK(near(hit, 150.0f, 0.5f), "setup: hit %.2f", hit);
    (void)edge;
    const float h = escapeHeading(q, 4, 90.0f, 300.0f);
    const float turn = fabsf(wrap180(h - 90.0f));
    CHECK(turn >= 55 && turn <= 70, "escape heading %.1f is a turn of %.1f, expected about 60", h, turn);
  }

  // Near the north-east corner flying east: along the east edge to the north is blocked by the corner, so the way along the south side
  // or straight back in must be chosen, never a heading that leaves the area within the clear distance.
  {
    Pt q[4];
    for (int i = 0; i < 4; ++i) q[i] = {sq[i].e - 400, sq[i].n - 450};  // pilot at (400, 450): 100 m from the east and 50 m from the north edge
    CHECK(contains(q, 4, 0, 0), "setup 2: inside");
    const Pt d = unit(90);
    int edge = -1;
    const float hit = rayHit(q, 4, 0, 0, d.e, d.n, 1e6f, &edge);
    (void)hit;
    (void)edge;
    const float h = escapeHeading(q, 4, 90.0f, 200.0f);
    const Pt hd = unit(h);
    CHECK(rayHit(q, 4, 0, 0, hd.e, hd.n, 200.0f) < 0, "the escape heading %.1f leaves the area within 200 m", h);
  }

  // Area.
  CHECK(near(areaM2(sq, 4), 1000000.0f, 1.0f), "area %.1f", areaM2(sq, 4));

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
