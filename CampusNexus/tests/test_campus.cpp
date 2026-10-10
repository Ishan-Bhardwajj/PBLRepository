// Self-checking tests for the Phase 1 + Phase 2 features (no external framework).
// Run: bin\campus_tests.exe   (exit code 0 = all tests passed)

#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "campus/campus_map.h"

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

    // Dijkstra and Bellman-Ford must agree for every pair of named locations.
    for (int a : locs) {
        for (int b : locs) {
            PathResult d = g_map.CalculateShortestPathDijkstra(a, b);
            PathResult f = g_map.CalculateShortestPathBellmanFord(a, b);
            CHECK(d.found && f.found);
            CHECK(Near(d.distance_m, f.distance_m, 1e-6));
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
                       {"shortest path (Dijkstra vs Bellman-Ford)", TestShortestPath},
                       {"cycle detection", TestCycle},
                       {"topological sort", TestTopologicalSort}};
    for (const T& t : tests) {
        int before = g_failed;
        t.fn();
        std::cout << (g_failed == before ? "[ PASS ] " : "[ FAIL ] ") << t.name << "\n";
    }
    std::cout << "\n" << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
