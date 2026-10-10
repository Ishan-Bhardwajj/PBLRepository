// Campus Nexus - console front end.
//
//   campus_nexus.exe                         interactive menu
//   campus_nexus.exe --path "Main Gate" Library
//   campus_nexus.exe --help                  all command-line options
//
// Phase 1 + Phase 2 feature set of the RudraxDave CampusNavigation project, re-built
// for Windows with a dependency-free HTML/SVG map view instead of OpenCV + ncurses.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "campus/campus_map.h"
#include "campus/visualizer.h"

namespace fs = std::filesystem;
using namespace campus;

namespace {

double g_sink = 0.0;  // keeps benchmarked calls from being optimised away

struct App {
    CampusMap map;
    std::string out_dir;
    bool auto_open = true;
    bool interactive = true;
};

std::string F1(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << v;
    return os.str();
}

std::string F2(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << v;
    return os.str();
}

bool ReadInput(std::string* s) {
    if (!std::getline(std::cin, *s)) return false;
    while (!s->empty() && (s->back() == '\r' || s->back() == '\n')) s->pop_back();
    return true;
}

template <typename F>
double BenchMicroseconds(F fn, int repeats) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) g_sink += fn();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / repeats;
}

double EdgeWeight(const CampusMap& map, int a, int b) {
    for (const Neighbor& nb : map.adjacency()[static_cast<std::size_t>(a)])
        if (nb.to == b) return nb.weight_m;
    return 0.0;
}

std::string JoinNames(const CampusMap& map, const std::vector<int>& ids, const std::string& sep) {
    std::string s;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) s += sep;
        s += map.DisplayName(ids[i]);
    }
    return s;
}

void Finish(App& app, const MapScene& scene, const std::string& file, const std::string& title) {
    std::string path = (fs::path(app.out_dir) / file).string();
    std::string err;
    if (!scene.Save(path, title, &err)) {
        std::cout << "  (could not write map view: " << err << ")\n";
        return;
    }
    std::cout << "  Map view saved to: " << path << "\n";
    if (app.auto_open) {
        if (!OpenInBrowser(path)) std::cout << "  Open that file in a browser to see the map.\n";
    }
}

// ---- turns whatever the user typed into a location id (or -1)
int ResolveLocation(App& app, const std::string& input) {
    const CampusMap& map = app.map;
    std::string text = Trim(input);
    if (text.empty()) return -1;
    int id = map.FindByName(text);
    if (id >= 0) return id;

    std::vector<std::string> matches = map.Autocomplete(text);
    if (matches.size() == 1) {
        std::cout << "  Using '" << matches[0] << "'.\n";
        return map.FindByName(matches[0]);
    }
    if (matches.size() > 1) {
        std::cout << "  '" << text << "' matches several locations:\n";
        for (const std::string& m : matches) std::cout << "    - " << m << "\n";
        return -1;
    }
    std::string close = map.FindClosestName(text);
    if (close.empty()) {
        std::cout << "  No location named '" << text << "'.\n";
        return -1;
    }
    std::cout << "  No location named '" << text << "'. Did you mean '" << close << "'?";
    if (!app.interactive) {
        std::cout << "\n";
        return -1;
    }
    std::cout << " (y/n): ";
    std::string a;
    if (ReadInput(&a) && !a.empty() && (a[0] == 'y' || a[0] == 'Y')) return map.FindByName(close);
    return -1;
}

int AskLocation(App& app, const std::string& prompt) {
    for (;;) {
        std::cout << prompt;
        std::string s;
        if (!ReadInput(&s) || Trim(s).empty()) return -1;
        int id = ResolveLocation(app, s);
        if (id >= 0) return id;
        std::cout << "  (press Enter on an empty line to cancel)\n";
    }
}

double AskNumber(const std::string& prompt, double fallback) {
    for (;;) {
        std::cout << prompt << " [" << F1(fallback) << "]: ";
        std::string s;
        if (!ReadInput(&s)) return fallback;
        s = Trim(s);
        if (s.empty()) return fallback;
        try {
            std::size_t used = 0;
            double v = std::stod(s, &used);
            if (used == s.size()) return v;
        } catch (...) {
        }
        std::cout << "  Please type a number.\n";
    }
}

// =================================================================== commands
int CmdAutocomplete(App& app, const std::string& prefix) {
    std::vector<std::string> r = app.map.Autocomplete(prefix);
    std::cout << "\nAutocomplete for '" << prefix << "':\n";
    if (r.empty()) {
        std::cout << "  (no location starts with that text)\n";
        return 1;
    }
    for (const std::string& n : r) std::cout << "  - " << n << "\n";
    return 0;
}

int CmdPosition(App& app, const std::string& input) {
    int id = ResolveLocation(app, input);
    if (id < 0) {
        std::pair<double, double> none = app.map.GetPosition(input);  // (-1,-1) for unknown
        std::cout << "  Position: (" << F1(none.first) << ", " << F1(none.second) << ")  -> location not found\n";
        return 1;
    }
    const Node& n = app.map.GetNode(id);
    std::pair<double, double> m = app.map.GetPosition(n.name);
    std::pair<double, double> px = app.map.GetPositionPixels(n.name);
    std::cout << "\n" << n.name << "\n"
              << "  Real-world position : x = " << F1(m.first) << " m east, y = " << F1(m.second)
              << " m south of the map's top-left corner\n"
              << "  Image position      : (" << F1(px.first) << ", " << F1(px.second) << ") px\n"
              << "  Node id             : " << n.id << "\n"
              << "  Direct neighbours   : " << app.map.adjacency()[static_cast<std::size_t>(id)].size()
              << " path segments\n";
    MapScene scene(app.map);
    scene.ShowGraphByDefault(false);
    scene.AddHighlight(id, "#facc15");
    scene.AddMarker(id, "P", "#2563eb");
    scene.AddInfo("Location", n.name);
    scene.AddInfo("Position (m)", "x = " + F1(m.first) + ", y = " + F1(m.second));
    scene.AddInfo("Position (px)", "(" + F1(px.first) + ", " + F1(px.second) + ")");
    scene.AddLegend("#2563eb", "Selected location");
    Finish(app, scene, "position.html", "Position of " + n.name);
    return 0;
}

int CmdEdit(App& app, const std::string& a, const std::string& b) {
    (void)app;
    int d = CampusMap::CalculateEditDistance(a, b);
    int dl = CampusMap::CalculateEditDistance(ToLower(a), ToLower(b));
    std::cout << "\nEdit distance between '" << a << "' and '" << b << "': " << d
              << "  (ignoring letter case: " << dl << ")\n";
    return 0;
}

int CmdPath(App& app, int src, int dst) {
    const CampusMap& map = app.map;
    const std::string from = map.DisplayName(src), to = map.DisplayName(dst);

    PathResult dij = map.CalculateShortestPathDijkstra(src, dst);
    PathResult bf = map.CalculateShortestPathBellmanFord(src, dst);
    const int reps = 2000;  // average over many runs: one run is far below clock resolution
    double t_dij = BenchMicroseconds([&] { return map.CalculateShortestPathDijkstra(src, dst).distance_m; }, reps);
    double t_bf = BenchMicroseconds([&] { return map.CalculateShortestPathBellmanFord(src, dst).distance_m; }, reps);

    std::cout << "\nShortest path: " << from << " -> " << to << "\n";
    if (!dij.found || !bf.found) {
        std::cout << "  No route exists between these locations.\n";
        return 1;
    }
    const double speed = map.config().walking_speed_mps;
    auto line = [&](const char* name, const PathResult& r, double t) {
        std::cout << "  " << std::left << std::setw(13) << name << std::right << F1(r.distance_m) << " m, walk about "
                  << FormatDuration(r.distance_m / speed) << ", " << r.nodes.size() << " nodes, " << F2(t)
                  << " us per run, " << r.relaxations << " edge relaxations\n";
    };
    line("Dijkstra", dij, t_dij);
    line("Bellman-Ford", bf, t_bf);
    const bool agree = std::abs(dij.distance_m - bf.distance_m) < 1e-6;
    std::cout << "  " << (agree ? "Both algorithms found the same shortest distance." : "WARNING: the two distances differ!")
              << "\n";
    if (t_dij > 0.0) {
        double ratio = t_bf / t_dij;
        if (ratio >= 1.05) std::cout << "  Dijkstra was about " << F1(ratio) << "x faster than Bellman-Ford on this query.\n";
        else if (ratio <= 0.95) std::cout << "  Bellman-Ford was faster on this query (" << F1(1.0 / ratio) << "x).\n";
        else std::cout << "  Both algorithms took about the same time on this query.\n";
    }
    std::cout << "  Route: " << JoinNames(map, dij.nodes, " > ") << "\n";

    // Leg distances between consecutive *named* stops on the route.
    std::vector<int> stops;
    std::vector<double> legs;
    double acc = 0.0;
    for (std::size_t i = 0; i < dij.nodes.size(); ++i) {
        if (i > 0) acc += EdgeWeight(map, dij.nodes[i - 1], dij.nodes[i]);
        if (map.GetNode(dij.nodes[i]).IsLocation()) {
            if (!stops.empty()) legs.push_back(acc);
            stops.push_back(dij.nodes[i]);
            acc = 0.0;
        }
    }
    if (stops.size() > 1) {
        std::cout << "  Legs:\n";
        for (std::size_t i = 0; i + 1 < stops.size(); ++i)
            std::cout << "    " << map.DisplayName(stops[i]) << " -> " << map.DisplayName(stops[i + 1]) << " : "
                      << F1(legs[i]) << " m\n";
    }

    MapScene scene(map);
    scene.AddRoute(dij.nodes, "#ff3b30", 7, false, true);
    scene.AddRoute(bf.nodes, "#00e5ff", 3, true, false);
    for (std::size_t i = 1; i + 1 < stops.size(); ++i) scene.AddHighlight(stops[i], "#facc15");
    scene.AddMarker(src, "S", "#16a34a");
    if (dst != src) scene.AddMarker(dst, "D", "#dc2626");
    scene.AddInfo("From", from);
    scene.AddInfo("To", to);
    scene.AddInfo("Distance", F1(dij.distance_m) + " m");
    scene.AddInfo("Walking time", FormatDuration(dij.distance_m / speed) + " at " + F1(speed) + " m/s");
    scene.AddInfo("Dijkstra", F2(t_dij) + " us/run, " + std::to_string(dij.relaxations) + " relaxations");
    scene.AddInfo("Bellman-Ford", F2(t_bf) + " us/run, " + std::to_string(bf.relaxations) + " relaxations");
    scene.AddInfo("Distances agree", agree ? "yes" : "NO");
    scene.AddLegend("#ff3b30", "Dijkstra route");
    scene.AddLegend("#00e5ff", "Bellman-Ford route (dashed, drawn on top)", true);
    scene.AddLegend("#facc15", "Named stops passed on the way");
    if (dij.nodes != bf.nodes)
        scene.AddNote("The two algorithms returned different routes of equal length (a tie).");
    else
        scene.AddNote("Both algorithms return the same route, so the dashed cyan line follows the red one.");
    Finish(app, scene, "shortest_path.html", "Shortest path: " + from + " to " + to);
    return 0;
}

int CmdCycle(App& app, const SquareArea& area) {
    const CampusMap& map = app.map;
    std::vector<int> cycle, sub;
    bool has = map.CycleDetection(area, &cycle, &sub);
    double us = BenchMicroseconds([&] { return map.CycleDetection(area) ? 1.0 : 0.0; }, 2000);

    std::size_t inside_edges = 0;
    std::vector<char> in(map.NodeCount(), 0);
    for (int id : sub) in[static_cast<std::size_t>(id)] = 1;
    for (const Edge& e : map.edges())
        if (in[static_cast<std::size_t>(e.a)] && in[static_cast<std::size_t>(e.b)]) ++inside_edges;

    std::cout << "\nCycle detection in area x " << F1(area.left_m) << ".." << F1(area.right_m) << " m, y "
              << F1(area.top_m) << ".." << F1(area.bottom_m) << " m\n"
              << "  Nodes inside: " << sub.size() << ", paths inside: " << inside_edges << "\n";
    if (has) {
        std::vector<int> closed = cycle;
        closed.push_back(cycle.front());
        std::cout << "  Result: a cycle EXISTS (" << cycle.size() << " nodes):\n  " << JoinNames(map, closed, " > ") << "\n";
    } else {
        std::cout << "  Result: NO cycle in this area (the paths inside form a forest/tree).\n";
    }
    std::cout << "  Time: " << F2(us) << " us per run\n";

    MapScene scene(map);
    scene.AddArea(area, "#f59e0b", "Selected area");
    scene.AddSubgraph(sub, "#38bdf8");
    if (has) scene.AddPolygon(cycle, "#ef4444", 7);
    scene.AddInfo("Area (metres)", "x " + F1(area.left_m) + " to " + F1(area.right_m) + ", y " + F1(area.top_m) +
                                       " to " + F1(area.bottom_m));
    scene.AddInfo("Nodes / paths inside", std::to_string(sub.size()) + " / " + std::to_string(inside_edges));
    scene.AddInfo("Cycle found", has ? "yes (" + std::to_string(cycle.size()) + " nodes)" : "no");
    scene.AddLegend("#f59e0b", "Selected area", true);
    scene.AddLegend("#38bdf8", "Graph inside the area");
    if (has) scene.AddLegend("#ef4444", "Cycle");
    Finish(app, scene, "cycle_detection.html", has ? "Cycle detected" : "No cycle in this area");
    return 0;
}

std::string ResolveDataFile(const App& app, const std::string& given) {
    if (fs::exists(given)) return given;
    fs::path p = fs::path(app.map.data_dir()) / given;
    if (fs::exists(p)) return p.string();
    return given;
}

int CmdTopo(App& app, const std::string& loc_file_in, const std::string& dep_file_in) {
    const CampusMap& map = app.map;
    std::string loc_file = ResolveDataFile(app, loc_file_in);
    std::string dep_file = ResolveDataFile(app, dep_file_in);
    std::vector<std::string> locations;
    std::vector<Dependency> deps;
    std::string err;
    if (!CampusMap::ReadLocationsFromCSVFile(loc_file, &locations, &err) ||
        !CampusMap::ReadDependenciesFromCSVFile(dep_file, &deps, &err)) {
        std::cout << "  Could not read input: " << err << "\n";
        return 1;
    }
    std::cout << "\nTopological sort of " << locations.size() << " locations with " << deps.size()
              << " dependencies\n  (" << loc_file << ", " << dep_file << ")\n";
    std::vector<std::string> order = map.DeliveringCampus(locations, deps, &err);
    if (order.empty()) {
        std::cout << "  Result: " << err << "\n";
        return 1;
    }
    std::cout << "  Valid visiting order:\n";
    for (std::size_t i = 0; i < order.size(); ++i) std::cout << "    " << (i + 1) << ". " << order[i] << "\n";

    // Walk the order on the real campus paths (shortest route between consecutive stops).
    MapScene scene(map);
    double total = 0.0;
    std::vector<int> ids;
    for (const std::string& n : order) ids.push_back(map.FindByName(n));
    for (std::size_t i = 0; i + 1 < ids.size(); ++i) {
        PathResult leg = map.CalculateShortestPathDijkstra(ids[i], ids[i + 1]);
        if (leg.found) {
            total += leg.distance_m;
            scene.AddRoute(leg.nodes, "#a855f7", 5, false, false);
        }
    }
    for (std::size_t i = 0; i < ids.size(); ++i) scene.AddMarker(ids[i], std::to_string(i + 1), "#7c3aed");
    std::cout << "  Walking all stops in this order: " << F1(total) << " m, about "
              << FormatDuration(total / map.config().walking_speed_mps) << "\n";
    scene.AddInfo("Locations", std::to_string(ids.size()));
    scene.AddInfo("Dependencies", std::to_string(deps.size()));
    scene.AddInfo("Walking distance", F1(total) + " m (shortest path between consecutive stops)");
    scene.AddInfo("Walking time", FormatDuration(total / map.config().walking_speed_mps));
    scene.AddLegend("#7c3aed", "Numbered stops in topological order");
    scene.AddLegend("#a855f7", "Walkable route between stops");
    scene.AddNote("Every 'before' location appears earlier in the order than its 'after' location.");
    Finish(app, scene, "topological_sort.html", "Topological sort - valid visiting order");
    return 0;
}

int CmdGraph(App& app) {
    const CampusMap& map = app.map;
    std::cout << "\nCampus graph: " << map.NodeCount() << " nodes (" << map.LocationCount() << " named locations, "
              << (map.NodeCount() - map.LocationCount()) << " junctions), " << map.EdgeCount() << " paths\n"
              << "Map size: " << F1(map.MapWidthMetres()) << " m x " << F1(map.MapHeightMetres()) << " m at "
              << F2(map.config().metres_per_pixel) << " m per pixel\n\nLocations:\n";
    for (const Node& n : map.nodes()) {
        if (!n.IsLocation()) continue;
        std::cout << "  " << std::left << std::setw(18) << n.name << std::right << "x=" << std::setw(6) << F1(n.x_m)
                  << " m  y=" << std::setw(6) << F1(n.y_m) << " m  paths: "
                  << map.adjacency()[static_cast<std::size_t>(n.id)].size() << "\n";
    }
    MapScene scene(map);
    scene.ShowGraphByDefault(true);
    scene.AddInfo("Nodes", std::to_string(map.NodeCount()) + " (" + std::to_string(map.LocationCount()) + " locations)");
    scene.AddInfo("Paths (edges)", std::to_string(map.EdgeCount()));
    scene.AddInfo("Scale", F2(map.config().metres_per_pixel) + " m per pixel");
    scene.AddLegend("#2563eb", "Named location");
    scene.AddLegend("#ff5cf0", "Junction / path bend");
    scene.AddLegend("#ffe066", "Walkable path (hover for length)");
    Finish(app, scene, "campus_graph.html", "Campus graph overview");
    return 0;
}

// =================================================================== menus
void PrintUsage() {
    std::cout <<
        "Campus Nexus - graph-based campus navigation (C++17)\n\n"
        "Usage: campus_nexus [options] [command]\n\n"
        "Commands (omit for the interactive menu):\n"
        "  --autocomplete <prefix>            locations starting with <prefix>\n"
        "  --position <name>                  position of a location (and mark it on the map)\n"
        "  --edit <a> <b>                     edit distance between two strings\n"
        "  --path <from> <to>                 shortest path: Dijkstra vs Bellman-Ford\n"
        "  --cycle <left> <right> <top> <bottom>   cycle detection inside an area (metres)\n"
        "  --topo [locations.csv dependencies.csv] topological sort (defaults: data/ samples)\n"
        "  --graph                            list the graph and draw it on the map\n\n"
        "Options:\n"
        "  --data <folder>   data folder (default: auto-detected)\n"
        "  --no-open         do not open the generated map in the browser\n"
        "  --help            this text\n";
}

void PrintMenu(const App& app) {
    std::cout << "\n==============================================\n"
              << "  CAMPUS NEXUS - Intelligent Pathfinding\n"
              << "==============================================\n"
              << "  1. Autocomplete a location name\n"
              << "  2. Find the position of a location\n"
              << "  3. Shortest path (Dijkstra vs Bellman-Ford)\n"
              << "  4. Cycle detection in an area\n"
              << "  5. Topological sort (visiting order)\n"
              << "  6. Edit distance between two names\n"
              << "  7. Show the campus graph\n"
              << "  8. Open maps in browser automatically: " << (app.auto_open ? "ON" : "OFF") << "  (toggle)\n"
              << "  0. Exit\n"
              << "Choose: ";
}

void Menu(App& app) {
    for (;;) {
        PrintMenu(app);
        std::string choice;
        if (!ReadInput(&choice)) return;
        choice = Trim(choice);
        if (choice == "0" || ToLower(choice) == "q") return;
        if (choice == "1") {
            std::cout << "Type the first letters of a location: ";
            std::string p;
            if (ReadInput(&p)) CmdAutocomplete(app, p);
        } else if (choice == "2") {
            std::cout << "Location name: ";
            std::string p;
            if (ReadInput(&p) && !Trim(p).empty()) CmdPosition(app, p);
        } else if (choice == "3") {
            int a = AskLocation(app, "From (location, Enter to cancel): ");
            if (a < 0) continue;
            int b = AskLocation(app, "To   (location, Enter to cancel): ");
            if (b < 0) continue;
            CmdPath(app, a, b);
        } else if (choice == "4") {
            std::cout << "\nEnter the area in metres from the top-left corner of the map ("
                      << F1(app.map.MapWidthMetres()) << " m wide, " << F1(app.map.MapHeightMetres())
                      << " m tall). Press Enter to accept [default].\n";
            SquareArea area;
            area.left_m = AskNumber("Left   x", 0.0);
            area.right_m = AskNumber("Right  x", app.map.MapWidthMetres());
            area.top_m = AskNumber("Top    y", 0.0);
            area.bottom_m = AskNumber("Bottom y", app.map.MapHeightMetres());
            CmdCycle(app, area);
        } else if (choice == "5") {
            std::cout << "Locations file [locations.csv]: ";
            std::string l, d;
            if (!ReadInput(&l)) continue;
            if (Trim(l).empty()) l = "locations.csv";
            std::cout << "Dependencies file [dependencies.csv; type 'cyclic' for the cyclic sample]: ";
            if (!ReadInput(&d)) continue;
            d = Trim(d);
            if (d.empty()) d = "dependencies.csv";
            else if (ToLower(d) == "cyclic") d = "dependencies_cyclic.csv";
            CmdTopo(app, Trim(l), d);
        } else if (choice == "6") {
            std::cout << "First string : ";
            std::string a, b;
            if (!ReadInput(&a)) continue;
            std::cout << "Second string: ";
            if (!ReadInput(&b)) continue;
            CmdEdit(app, a, b);
        } else if (choice == "7") {
            CmdGraph(app);
        } else if (choice == "8") {
            app.auto_open = !app.auto_open;
            std::cout << "Automatic browser opening is now " << (app.auto_open ? "ON" : "OFF") << ".\n";
        } else {
            std::cout << "Please choose a number from the menu.\n";
        }
    }
}

bool ParseNumberArg(const std::string& s, double* v) {
    try {
        std::size_t used = 0;
        *v = std::stod(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    App app;
    std::string data_dir;
    std::vector<std::string> rest;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h" || args[i] == "/?") {
            PrintUsage();
            return 0;
        } else if (args[i] == "--no-open") {
            app.auto_open = false;
        } else if (args[i] == "--data" && i + 1 < args.size()) {
            data_dir = args[++i];
        } else {
            rest.push_back(args[i]);
        }
    }

    if (data_dir.empty()) data_dir = CampusMap::LocateDataDir(argc > 0 ? argv[0] : "");
    if (data_dir.empty()) {
        std::cerr << "Could not find the data folder (data/map.csv).\n"
                     "Run the program from the project folder or pass --data <folder>.\n";
        return 2;
    }
    std::string err;
    if (!app.map.Load(data_dir, &err)) {
        std::cerr << "Failed to load map data from '" << data_dir << "': " << err << "\n";
        return 2;
    }
    app.out_dir = (fs::path(data_dir).parent_path() / "output").string();

    if (rest.empty()) {
        std::cout << "Loaded campus graph: " << app.map.NodeCount() << " nodes, " << app.map.EdgeCount()
                  << " paths, " << app.map.LocationCount() << " named locations.\n";
        Menu(app);
        std::cout << "Goodbye!\n";
        return 0;
    }

    app.interactive = false;
    const std::string& cmd = rest[0];
    auto need = [&](std::size_t n) { return rest.size() >= n + 1; };
    if (cmd == "--autocomplete" && need(1)) return CmdAutocomplete(app, rest[1]);
    if (cmd == "--position" && need(1)) return CmdPosition(app, rest[1]);
    if (cmd == "--edit" && need(2)) return CmdEdit(app, rest[1], rest[2]);
    if (cmd == "--graph") return CmdGraph(app);
    if (cmd == "--path" && need(2)) {
        int a = ResolveLocation(app, rest[1]);
        int b = ResolveLocation(app, rest[2]);
        if (a < 0 || b < 0) return 1;
        return CmdPath(app, a, b);
    }
    if (cmd == "--cycle" && need(4)) {
        SquareArea area;
        if (!ParseNumberArg(rest[1], &area.left_m) || !ParseNumberArg(rest[2], &area.right_m) ||
            !ParseNumberArg(rest[3], &area.top_m) || !ParseNumberArg(rest[4], &area.bottom_m)) {
            std::cerr << "--cycle needs four numbers (metres): left right top bottom\n";
            return 1;
        }
        return CmdCycle(app, area);
    }
    if (cmd == "--topo") {
        std::string l = rest.size() > 1 ? rest[1] : "locations.csv";
        std::string d = rest.size() > 2 ? rest[2] : "dependencies.csv";
        return CmdTopo(app, l, d);
    }
    std::cerr << "Unknown or incomplete command: " << cmd << "\n\n";
    PrintUsage();
    return 1;
}
