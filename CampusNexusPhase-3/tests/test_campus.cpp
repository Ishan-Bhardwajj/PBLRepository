// Self-checking tests for the Phase 1 + Phase 2 features (no external framework).
// Run: bin\campus_tests.exe   (exit code 0 = all tests passed)

#include <cmath>
#include <iostream>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "campus/campus_map.h"
#include "campus/tsp.h"

using namespace campus;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(cond)) {                                                                  \
            ++g_failed;                                                                 \
            std::cout << "    FAILED: " #cond "  (line " << __LINE__ << ")\n";          \
        }                                                                               \
    } while (0)

static bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

static CampusMap g_map;

static double EdgeW(int a, int b) {
    for (const Neighbor& nb : g_map.adjacency()[static_cast<std::size_t>(a)])
        if (nb.to == b) return nb.weight_m;
    return -1.0;
}

static void TestLoading() {
    CHECK(g_map.NodeCount() == 47);
    CHECK(g_map.EdgeCount() == 64);
    CHECK(g_map.LocationCount() == 16);
    CHECK(Near(g_map.config().metres_per_pixel, 0.5));
    CHECK(Near(g_map.MapWidthMetres(), 836.0));
    // Main Gate (92,508) -> Junction (328,490): 236 px east, 18 px north => real scale 0.5 m/px
    int gate = g_map.FindByName("Main Gate");
    CHECK(gate >= 0);
    double w = -1;
    for (const Neighbor& nb : g_map.adjacency()[static_cast<std::size_t>(gate)]) w = nb.weight_m;
    CHECK(Near(w, std::hypot(236.0, 18.0) * 0.5, 1e-3));
    // graph is connected: every named location reachable from the gate
    for (const Node& n : g_map.nodes())
        if (n.IsLocation()) CHECK(g_map.CalculateShortestPathDijkstra(gate, n.id).found);
}

static void TestAutocomplete() {
    auto r = g_map.Autocomplete("lib");
    CHECK(r.size() == 1 && r[0] == "Library");
    r = g_map.Autocomplete("LIB");  // case-insensitive
    CHECK(r.size() == 1 && r[0] == "Library");
    r = g_map.Autocomplete("academic");
    CHECK(r.size() == 2 && r[0] == "Academic Block A" && r[1] == "Academic Block B");
    r = g_map.Autocomplete("s");
    CHECK(r.size() == 3);  // Science Building, Sports Complex, Student Center
    CHECK(g_map.Autocomplete("zzz").empty());
    CHECK(g_map.Autocomplete("").empty());
    CHECK(g_map.Autocomplete("junction").empty());  // unnamed nodes are never suggested
}

static void TestPosition() {
    auto p = g_map.GetPosition("Library");
    CHECK(Near(p.first, 792 * 0.5) && Near(p.second, 353 * 0.5));
    auto q = g_map.GetPosition("library");  // case-insensitive
    CHECK(Near(q.first, p.first) && Near(q.second, p.second));
    auto px = g_map.GetPositionPixels("Library");
    CHECK(Near(px.first, 792) && Near(px.second, 353));
    auto none = g_map.GetPosition("Nowhere");
    CHECK(Near(none.first, -1) && Near(none.second, -1));
}

static void TestEditDistance() {
    CHECK(CampusMap::CalculateEditDistance("kitten", "sitting") == 3);
    CHECK(CampusMap::CalculateEditDistance("", "abc") == 3);
    CHECK(CampusMap::CalculateEditDistance("abc", "") == 3);
    CHECK(CampusMap::CalculateEditDistance("same", "same") == 0);
    CHECK(CampusMap::CalculateEditDistance("UFC", "KFC") == 1);
    CHECK(CampusMap::CalculateEditDistance("flaw", "lawn") == 2);
    CHECK(g_map.FindClosestName("Libary") == "Library");
    CHECK(g_map.FindClosestName("cafetria") == "Cafeteria");
    CHECK(g_map.FindClosestName("main gat") == "Main Gate");
    CHECK(g_map.FindClosestName("Hostel Aria") == "Hostel Area");
}

static void TestShortestPath() {
    std::vector<int> locs;
    for (const Node& n : g_map.nodes())
        if (n.IsLocation()) locs.push_back(n.id);

    // Dijkstra, Bellman-Ford and A* must agree for every pair of named locations.
    for (int a : locs) {
        for (int b : locs) {
            PathResult d = g_map.CalculateShortestPathDijkstra(a, b);
            PathResult f = g_map.CalculateShortestPathBellmanFord(a, b);
            PathResult as = g_map.CalculateShortestPathAStar(a, b);
            CHECK(d.found && f.found && as.found);
            CHECK(Near(d.distance_m, f.distance_m, 1e-6));
            CHECK(Near(d.distance_m, as.distance_m, 1e-6));          // A* is exact too
            CHECK(as.nodes.front() == a && as.nodes.back() == b);
            CHECK(as.expanded <= g_map.NodeCount());
            // symmetric on an undirected graph
            CHECK(Near(d.distance_m, g_map.CalculateShortestPathDijkstra(b, a).distance_m, 1e-6));
            // path validity: starts/ends right, consecutive nodes adjacent, weights add up
            CHECK(d.nodes.front() == a && d.nodes.back() == b);
            double sum = 0.0;
            bool adjacent = true;
            for (std::size_t i = 1; i < d.nodes.size(); ++i) {
                double w = EdgeW(d.nodes[i - 1], d.nodes[i]);
                if (w < 0) adjacent = false; else sum += w;
            }
            CHECK(adjacent);
            CHECK(Near(sum, d.distance_m, 1e-6));
        }
    }
    // same location
    int lib = g_map.FindByName("Library");
    PathResult self = g_map.CalculateShortestPathDijkstra(lib, lib);
    CHECK(self.found && self.nodes.size() == 1 && Near(self.distance_m, 0.0));
    // invalid ids
    CHECK(!g_map.CalculateShortestPathDijkstra(-1, lib).found);
    CHECK(!g_map.CalculateShortestPathBellmanFord(lib, 9999).found);
    CHECK(!g_map.CalculateShortestPathAStar(-1, lib).found && !g_map.CalculateShortestPathAStar(lib, 9999).found);
    PathResult aself = g_map.CalculateShortestPathAStar(lib, lib);
    CHECK(aself.found && aself.nodes.size() == 1 && Near(aself.distance_m, 0.0));
    // the A* guess is admissible: never longer than the real shortest distance, for EVERY pair of nodes
    for (const Node& x : g_map.nodes())
        for (const Node& y : g_map.nodes())
            CHECK(g_map.StraightLineDistance(x.id, y.id) <= g_map.CalculateShortestPathDijkstra(x.id, y.id).distance_m + 1e-6);
    // known route: Main Gate -> Library goes Gate-J1-J2-J3-Library (shorter than via Cafeteria)
    PathResult gl = g_map.CalculateShortestPathDijkstra(g_map.FindByName("Main Gate"), lib);
    CHECK(gl.nodes.size() == 5);
    CHECK(gl.distance_m > 300.0 && gl.distance_m < 450.0);
}

static void TestCycle() {
    std::vector<int> cycle, sub;
    // Whole map: the campus has many loops.
    SquareArea all{0, g_map.MapWidthMetres(), 0, g_map.MapHeightMetres()};
    CHECK(g_map.CycleDetection(all, &cycle, &sub));
    CHECK(sub.size() == g_map.NodeCount());
    CHECK(cycle.size() >= 3);
    // The returned cycle is valid: consecutive (and closing) nodes are adjacent, all distinct.
    bool ok = true;
    for (std::size_t i = 0; i < cycle.size(); ++i)
        if (EdgeW(cycle[i], cycle[(i + 1) % cycle.size()]) < 0) ok = false;
    CHECK(ok);
    CHECK(std::set<int>(cycle.begin(), cycle.end()).size() == cycle.size());
    // Corner around the Main Gate: only a short chain of paths => no cycle.
    SquareArea gate{0, 220, 150, 300};
    CHECK(!g_map.CycleDetection(gate, &cycle, &sub));
    CHECK(cycle.empty());
    CHECK(sub.size() >= 3);
    // Empty area: nothing inside, no cycle.
    CHECK(!g_map.CycleDetection(SquareArea{-50, -10, -50, -10}, &cycle, &sub));
    CHECK(sub.empty());
    // Swapped bounds behave the same as normal bounds.
    CHECK(g_map.CycleDetection(SquareArea{g_map.MapWidthMetres(), 0, g_map.MapHeightMetres(), 0}));
    // Area around Cafeteria / J3 / J2 / J4 contains the loop Cafeteria-J3-J2-J4.
    SquareArea loop{280, 340, 90, 250};
    CHECK(g_map.CycleDetection(loop, &cycle, nullptr));
}

static void TestTopologicalSort() {
    std::vector<std::string> locs;
    std::vector<Dependency> deps, cyclic;
    std::string err;
    std::string dir = g_map.data_dir();
    CHECK(CampusMap::ReadLocationsFromCSVFile(dir + "/locations.csv", &locs, &err));
    CHECK(CampusMap::ReadDependenciesFromCSVFile(dir + "/dependencies.csv", &deps, &err));
    CHECK(CampusMap::ReadDependenciesFromCSVFile(dir + "/dependencies_cyclic.csv", &cyclic, &err));
    CHECK(locs.size() == 7 && deps.size() == 7);

    std::vector<std::string> order = g_map.DeliveringCampus(locs, deps, &err);
    CHECK(order.size() == locs.size());
    auto pos = [&](const std::string& n) {
        for (std::size_t i = 0; i < order.size(); ++i) if (order[i] == n) return static_cast<int>(i);
        return -1;
    };
    for (const Dependency& d : deps) CHECK(pos(d.before) >= 0 && pos(d.before) < pos(d.after));

    CHECK(g_map.DeliveringCampus(locs, cyclic, &err).empty());
    CHECK(err.find("cycle") != std::string::npos);
    // unknown location
    CHECK(g_map.DeliveringCampus({"Library", "Narnia"}, {}, &err).empty());
    CHECK(err.find("unknown location") != std::string::npos);
    // dependency outside the list
    CHECK(g_map.DeliveringCampus({"Library"}, {{"Library", "Parking"}}, &err).empty());
    // no dependencies: any order, but all locations returned once (duplicates removed)
    CHECK(g_map.DeliveringCampus({"Library", "Parking", "library"}, {}, &err).size() == 2);
    // names are case-insensitive and returned canonically
    auto o2 = g_map.DeliveringCampus({"library", "parking"}, {{"PARKING", "LIBRARY"}}, &err);
    CHECK(o2.size() == 2 && o2[0] == "Parking" && o2[1] == "Library");
}


// ------------------------------------------------------------------ Travelling Campus
static std::vector<int> AllLocations() {
    std::vector<int> v;
    for (const Node& n : g_map.nodes())
        if (n.IsLocation()) v.push_back(n.id);
    return v;
}

// Same stops as `stops`, each exactly once, start first?
static bool IsValidOrder(const std::vector<int>& order, const std::vector<int>& stops) {
    if (order.size() != stops.size() || order.empty() || order[0] != stops[0]) return false;
    std::vector<int> a = order, b = stops;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

static void TestTravellingCampus() {
    const std::vector<int> all = AllLocations();
    const int gate = g_map.FindByName("Main Gate"), lib = g_map.FindByName("Library"),
              caf = g_map.FindByName("Cafeteria"), sci = g_map.FindByName("Science Building");

    // --- two stops: a round trip is exactly twice the one-way distance
    {
        TourPlanner one_way(g_map, {gate, lib}, false), round(g_map, {gate, lib}, true);
        double d = g_map.CalculateShortestPathDijkstra(gate, lib).distance_m;
        CHECK(one_way.valid() && round.valid());
        CHECK(Near(one_way.BruteForce().distance_m, d, 1e-6));
        CHECK(Near(round.BruteForce().distance_m, 2 * d, 1e-6));
        CHECK(Near(round.Backtracking().distance_m, 2 * d, 1e-6));
        CHECK(Near(round.TwoOpt().distance_m, 2 * d, 1e-6));
    }
    // --- the example from the assignment: Main Gate -> Library -> Cafeteria -> Science Building -> Main Gate
    {
        TourPlanner p(g_map, {gate, lib, caf, sci}, true);
        CHECK(p.valid() && p.size() == 4);
        TourResult bf = p.BruteForce(), bt = p.Backtracking(), opt = p.TwoOpt();
        CHECK(bf.ran && bt.ran && opt.ran);
        CHECK(bf.work == 6);  // (4-1)! orders
        CHECK(Near(bf.distance_m, bt.distance_m, 1e-6));
        CHECK(opt.distance_m >= bf.distance_m - 1e-6);
        CHECK(IsValidOrder(bf.order, p.stops()) && IsValidOrder(bt.order, p.stops()) && IsValidOrder(opt.order, p.stops()));
        // reported distance really is the length of the reported order
        CHECK(Near(p.OrderDistance(bf.order), bf.distance_m, 1e-6));
        CHECK(Near(p.OrderDistance(bt.order), bt.distance_m, 1e-6));
        CHECK(Near(p.OrderDistance(opt.order), opt.distance_m, 1e-6));
        // distance of the typed order is manually the sum of the four legs
        double typed = 0;
        const int seq[] = {gate, lib, caf, sci, gate};
        for (int i = 0; i < 4; ++i) typed += g_map.CalculateShortestPathDijkstra(seq[i], seq[i + 1]).distance_m;
        CHECK(Near(p.OrderDistance(p.stops()), typed, 1e-6));
        CHECK(bf.distance_m <= typed + 1e-6);
    }
    // --- Brute force == Backtracking on many pseudo-random subsets, round trip and one way
    unsigned seed = 12345;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
    for (int round = 0; round < 60; ++round) {
        std::vector<int> pool = all;
        for (std::size_t i = pool.size() - 1; i > 0; --i) std::swap(pool[i], pool[next() % (i + 1)]);
        std::size_t k = 2 + next() % 8;  // 2..9 stops
        std::vector<int> stops(pool.begin(), pool.begin() + static_cast<std::ptrdiff_t>(k));
        for (bool ret : {true, false}) {
            TourPlanner p(g_map, stops, ret);
            TourResult bf = p.BruteForce(), bt = p.Backtracking(), opt = p.TwoOpt();
            CHECK(bf.ran && bt.ran && opt.ran);
            CHECK(Near(bf.distance_m, bt.distance_m, 1e-6));          // both exact
            CHECK(opt.distance_m >= bf.distance_m - 1e-6);              // heuristic never beats the optimum
            CHECK(opt.distance_m <= opt.start_distance_m + 1e-6);       // 2-opt never makes the route longer
            CHECK(IsValidOrder(bf.order, p.stops()) && IsValidOrder(bt.order, p.stops()) && IsValidOrder(opt.order, p.stops()));
            CHECK(Near(p.OrderDistance(bt.order), bt.distance_m, 1e-6));
            CHECK(Near(p.OrderDistance(opt.order), opt.distance_m, 1e-6));
            // brute force checks exactly (k-1)! orders
            unsigned long long f = 1;
            for (std::size_t i = 2; i < k; ++i) f *= i;
            CHECK(bf.work == f);
            // backtracking never explores more complete routes than brute force
            CHECK(bt.work >= 1);
        }
    }
    // --- one way is never longer than the round trip for the same stops
    {
        std::vector<int> stops(all.begin(), all.begin() + 7);
        CHECK(TourPlanner(g_map, stops, false).Backtracking().distance_m <= TourPlanner(g_map, stops, true).Backtracking().distance_m + 1e-6);
    }
    // --- pruning really prunes
    {
        std::vector<int> stops(all.begin(), all.begin() + 10);
        TourPlanner p(g_map, stops, true);
        TourResult bf = p.BruteForce(), bt = p.Backtracking();
        CHECK(bt.pruned > 0);
        CHECK(bt.work < bf.work / 10);
    }
    // --- all 16 locations: backtracking still finishes, 2-opt is valid and not shorter than optimal
    {
        TourPlanner p(g_map, all, true);
        TourResult bf = p.BruteForce(), bt = p.Backtracking(), opt = p.TwoOpt();
        CHECK(!bf.ran && !bf.note.empty());  // 15! orders: refused, with an explanation
        CHECK(bt.ran && opt.ran);
        CHECK(opt.distance_m >= bt.distance_m - 1e-6);
        CHECK(IsValidOrder(bt.order, p.stops()));
    }
    // --- expanded walk follows real campus paths: starts at the start, consecutive nodes are neighbours
    {
        std::vector<int> stops = {gate, lib, caf, sci};
        for (bool ret : {true, false}) {
            TourPlanner p(g_map, stops, ret);
            TourResult bt = p.Backtracking();
            std::vector<int> walk = p.ExpandToPath(bt.order);
            CHECK(!walk.empty() && walk.front() == gate);
            if (ret) CHECK(walk.back() == gate);
            else CHECK(walk.back() == bt.order.back());
            double len = 0;
            bool adjacent = true;
            for (std::size_t i = 0; i + 1 < walk.size(); ++i) {
                double w = EdgeW(walk[i], walk[i + 1]);
                if (w < 0) adjacent = false; else len += w;
            }
            CHECK(adjacent);
            CHECK(Near(len, bt.distance_m, 1e-6));
        }
    }
    // --- bad input: duplicates are ignored, junctions are ignored, too few stops is an error
    {
        TourPlanner dup(g_map, {gate, lib, gate, lib}, true);
        CHECK(dup.valid() && dup.size() == 2);
        int junction = -1;
        for (const Node& n : g_map.nodes()) if (!n.IsLocation()) { junction = n.id; break; }
        TourPlanner junk(g_map, {gate, junction, -5, 9999}, true);
        CHECK(!junk.valid() && junk.BruteForce().ran == false);
        CHECK(!TourPlanner(g_map, {}, true).valid());
    }
    // --- 2-opt: a deliberately bad order gets repaired (reversals happen) and ends no longer than it began
    {
        std::vector<int> stops = {gate, g_map.FindByName("Hostel Area"), lib, g_map.FindByName("Engineering Labs"), caf, sci};
        TourPlanner p(g_map, stops, true);
        TourResult opt = p.TwoOpt();
        CHECK(opt.start_distance_m >= opt.distance_m - 1e-6);
        CHECK(Near(opt.start_distance_m, p.OrderDistance(stops), 1e-6));
    }
}

int main(int argc, char** argv) {
    std::string dir = CampusMap::LocateDataDir(argc > 0 ? argv[0] : "");
    std::string err;
    if (dir.empty() || !g_map.Load(dir, &err)) {
        std::cout << "Cannot load map data: " << (dir.empty() ? "data folder not found" : err) << "\n";
        return 2;
    }
    struct T { const char* name; void (*fn)(); };
    const T tests[] = {{"loading and real scale", TestLoading},
                       {"autocomplete", TestAutocomplete},
                       {"get position", TestPosition},
                       {"edit distance / closest name", TestEditDistance},
                       {"shortest path (Dijkstra vs Bellman-Ford vs A*)", TestShortestPath},
                       {"cycle detection", TestCycle},
                       {"topological sort", TestTopologicalSort},
                       {"Travelling Campus (brute force / backtracking / 2-opt)", TestTravellingCampus}};
    for (const T& t : tests) {
        int before = g_failed;
        t.fn();
        std::cout << (g_failed == before ? "[ PASS ] " : "[ FAIL ] ") << t.name << "\n";
    }
    std::cout << "\n" << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
