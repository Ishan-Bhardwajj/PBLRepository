// Campus Nexus - core map / graph library.
//
// The campus is modelled as an undirected weighted graph:
//   * vertices  = named locations (buildings, gate, ...) and unnamed path junctions
//   * edges     = walkable paths, weight = straight-line length in metres
//
// Covers the Phase 1 + Phase 2 feature set of the RudraxDave CampusNavigation
// (TrojanMap) project: Autocomplete, GetPosition, EditDistance / closest name,
// Dijkstra, Bellman-Ford, Cycle Detection and Topological Sort.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace campus {

// ---------------------------------------------------------------- small helpers
std::string ToLower(const std::string& s);
std::string Trim(const std::string& s);
// "4 min 26 s" style text for a duration in seconds.
std::string FormatDuration(double seconds);

// ---------------------------------------------------------------- data types
struct Node {
    int id = -1;
    std::string name;      // empty => unnamed junction / path waypoint
    double x_px = 0.0;     // position on the map image (pixels, origin top-left)
    double y_px = 0.0;
    double x_m = 0.0;      // real-world position in metres (origin top-left of image)
    double y_m = 0.0;
    bool IsLocation() const { return !name.empty(); }
};

struct Edge {
    int a = -1;
    int b = -1;
    double weight_m = 0.0;
};

struct Neighbor {
    int to = -1;
    double weight_m = 0.0;
};

struct Config {
    std::string image_base = "images/campus_base.png";
    std::string image_reference = "images/campus_reference.png";
    int image_width = 1672;
    int image_height = 941;
    double metres_per_pixel = 0.5;
    double walking_speed_mps = 1.4;
};

struct PathResult {
    bool found = false;
    std::vector<int> nodes;          // node ids from source to destination
    double distance_m = 0.0;
    std::size_t relaxations = 0;     // edge relaxations attempted (work counter)
};

// Rectangular region in real-world metres (origin = top-left of the map image).
struct SquareArea {
    double left_m = 0.0;
    double right_m = 0.0;
    double top_m = 0.0;
    double bottom_m = 0.0;
};

// "before" must be visited before "after".
struct Dependency {
    std::string before;
    std::string after;
};

// ---------------------------------------------------------------- the map
class CampusMap {
public:
    // Loads <data_dir>/config.txt, map.csv and edges.csv. Returns false and fills
    // *error on failure.
    bool Load(const std::string& data_dir, std::string* error = nullptr);

    // --- basic accessors
    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<Edge>& edges() const { return edges_; }
    const std::vector<std::vector<Neighbor>>& adjacency() const { return adjacency_; }
    const Config& config() const { return config_; }
    const Node& GetNode(int id) const { return nodes_[static_cast<std::size_t>(id)]; }
    std::size_t NodeCount() const { return nodes_.size(); }
    std::size_t EdgeCount() const { return edges_.size(); }
    std::size_t LocationCount() const;
    double MapWidthMetres() const { return config_.image_width * config_.metres_per_pixel; }
    double MapHeightMetres() const { return config_.image_height * config_.metres_per_pixel; }
    const std::string& data_dir() const { return data_dir_; }

    // Display name: the location name, or "Junction #id" for unnamed nodes.
    std::string DisplayName(int id) const;

    // --- Phase 1 --------------------------------------------------------------
    // Case-insensitive exact lookup of a location name. Returns node id or -1.
    int FindByName(const std::string& name) const;

    // All location names that start with `prefix` (case-insensitive), sorted A-Z.
    std::vector<std::string> Autocomplete(const std::string& prefix) const;

    // (x, y) in metres from the top-left corner of the map; (-1, -1) if unknown.
    std::pair<double, double> GetPosition(const std::string& name) const;
    // (x, y) in image pixels; (-1, -1) if unknown.
    std::pair<double, double> GetPositionPixels(const std::string& name) const;

    // Levenshtein distance between two strings (case-sensitive as given).
    static int CalculateEditDistance(const std::string& a, const std::string& b);

    // Location name with the smallest (case-insensitive) edit distance to `name`.
    // Returns "" if the map has no named locations.
    std::string FindClosestName(const std::string& name) const;

    // --- Phase 2 --------------------------------------------------------------
    // Dijkstra with a binary heap, O((V + E) log V).
    PathResult CalculateShortestPathDijkstra(int source, int target) const;
    // Bellman-Ford (V-1 relaxation passes, stops early when nothing changes), O(V*E).
    PathResult CalculateShortestPathBellmanFord(int source, int target) const;

    // Cycle detection (DFS) in the sub-graph induced by `area`. Both end points of
    // an edge must lie inside the area for the edge to count.
    //   cycle_out    - node ids of one cycle found (empty if none)
    //   subgraph_out - node ids that lie inside the area
    bool CycleDetection(const SquareArea& area, std::vector<int>* cycle_out = nullptr,
                        std::vector<int>* subgraph_out = nullptr) const;

    // Topological sort of `locations` under `dependencies` (DFS based).
    // Returns the canonical location names in a valid visiting order, or an empty
    // vector (and *error) when a name is unknown or the dependencies are cyclic.
    std::vector<std::string> DeliveringCampus(const std::vector<std::string>& locations,
                                              const std::vector<Dependency>& dependencies,
                                              std::string* error = nullptr) const;

    // --- CSV helpers for the topological-sort input ---------------------------
    static bool ReadLocationsFromCSVFile(const std::string& path,
                                         std::vector<std::string>* out, std::string* error);
    static bool ReadDependenciesFromCSVFile(const std::string& path,
                                            std::vector<Dependency>* out, std::string* error);

    // Walk upward from the working directory / executable directory looking for a
    // folder that contains data/map.csv. Returns the data directory or "".
    static std::string LocateDataDir(const std::string& argv0);

private:
    bool ReadConfig(const std::string& path, std::string* error);
    bool ReadNodes(const std::string& path, std::string* error);
    bool ReadEdges(const std::string& path, std::string* error);
    bool InsideArea(const Node& n, const SquareArea& area) const;

    std::string data_dir_;
    Config config_;
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    std::vector<std::vector<Neighbor>> adjacency_;
    std::unordered_map<std::string, int> name_index_;  // lower-case name -> id
};

}  // namespace campus
