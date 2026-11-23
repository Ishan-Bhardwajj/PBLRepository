#include "campus/campus_map.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace campus {

// =============================================================== small helpers
std::string ToLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::string Trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string FormatDuration(double seconds) {
    long total = std::lround(seconds);
    if (total < 0) total = 0;
    long m = total / 60;
    long s = total % 60;
    std::ostringstream os;
    if (m > 0) os << m << " min ";
    os << s << " s";
    return os.str();
}

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Strips a UTF-8 BOM and trailing CR/LF (Windows files).
std::string CleanLine(std::string line) {
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return line;
}

std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, ',')) out.push_back(Trim(cur));
    if (!line.empty() && line.back() == ',') out.push_back("");
    return out;
}

bool ReadAllLines(const std::string& path, std::vector<std::string>* lines, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open file: " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = CleanLine(line);
        if (!Trim(line).empty()) lines->push_back(line);
    }
    return true;
}

bool ParseDouble(const std::string& s, double* v) {
    try {
        std::size_t used = 0;
        *v = std::stod(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& s, int* v) {
    try {
        std::size_t used = 0;
        *v = std::stoi(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

PathResult BuildPath(const std::vector<double>& dist, const std::vector<int>& prev, int source,
                     int target, std::size_t relaxations) {
    PathResult r;
    r.relaxations = relaxations;
    if (dist[static_cast<std::size_t>(target)] == kInf) return r;
    for (int v = target; v != -1; v = prev[static_cast<std::size_t>(v)]) r.nodes.push_back(v);
    std::reverse(r.nodes.begin(), r.nodes.end());
    if (r.nodes.empty() || r.nodes.front() != source) {
        r.nodes.clear();
        return r;
    }
    r.found = true;
    r.distance_m = dist[static_cast<std::size_t>(target)];
    return r;
}

}  // namespace

// =============================================================== loading
bool CampusMap::Load(const std::string& data_dir, std::string* error) {
    data_dir_ = data_dir;
    nodes_.clear();
    edges_.clear();
    adjacency_.clear();
    name_index_.clear();
    fs::path dir(data_dir);
    if (!ReadConfig((dir / "config.txt").string(), error)) return false;
    if (!ReadNodes((dir / "map.csv").string(), error)) return false;
    if (!ReadEdges((dir / "edges.csv").string(), error)) return false;
    return true;
}

bool CampusMap::ReadConfig(const std::string& path, std::string* error) {
    std::vector<std::string> lines;
    if (!ReadAllLines(path, &lines, error)) return false;
    for (const std::string& raw : lines) {
        std::string line = Trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        double d = 0;
        int i = 0;
        if (key == "image_base") config_.image_base = val;
        else if (key == "image_reference") config_.image_reference = val;
        else if (key == "image_width" && ParseInt(val, &i)) config_.image_width = i;
        else if (key == "image_height" && ParseInt(val, &i)) config_.image_height = i;
        else if (key == "metres_per_pixel" && ParseDouble(val, &d)) config_.metres_per_pixel = d;
        else if (key == "walking_speed_mps" && ParseDouble(val, &d)) config_.walking_speed_mps = d;
    }
    if (config_.metres_per_pixel <= 0.0 || config_.walking_speed_mps <= 0.0) {
        if (error) *error = "config.txt: metres_per_pixel and walking_speed_mps must be positive";
        return false;
    }
    return true;
}

bool CampusMap::ReadNodes(const std::string& path, std::string* error) {
    std::vector<std::string> lines;
    if (!ReadAllLines(path, &lines, error)) return false;
    bool first = true;
    for (const std::string& line : lines) {
        std::vector<std::string> f = SplitCsv(line);
        if (first) {
            first = false;
            if (!f.empty() && ToLower(f[0]) == "id") continue;  // header row
        }
        if (f.size() < 4) {
            if (error) *error = "map.csv: bad row: " + line;
            return false;
        }
        Node n;
        double x = 0, y = 0;
        if (!ParseInt(f[0], &n.id) || !ParseDouble(f[2], &x) || !ParseDouble(f[3], &y)) {
            if (error) *error = "map.csv: bad number in row: " + line;
            return false;
        }
        if (n.id != static_cast<int>(nodes_.size())) {
            if (error) *error = "map.csv: ids must be 0,1,2,... in order (row: " + line + ")";
            return false;
        }
        n.name = f[1];
        n.x_px = x;
        n.y_px = y;
        n.x_m = x * config_.metres_per_pixel;
        n.y_m = y * config_.metres_per_pixel;
        if (n.IsLocation()) {
            std::string key = ToLower(n.name);
            if (name_index_.count(key)) {
                if (error) *error = "map.csv: duplicate location name: " + n.name;
                return false;
            }
            name_index_[key] = n.id;
        }
        nodes_.push_back(n);
    }
    adjacency_.assign(nodes_.size(), {});
    if (nodes_.empty()) {
        if (error) *error = "map.csv contains no nodes";
        return false;
    }
    return true;
}

bool CampusMap::ReadEdges(const std::string& path, std::string* error) {
    std::vector<std::string> lines;
    if (!ReadAllLines(path, &lines, error)) return false;
    bool first = true;
    for (const std::string& line : lines) {
        std::vector<std::string> f = SplitCsv(line);
        if (first) {
            first = false;
            if (!f.empty() && ToLower(f[0]) == "from_id") continue;
        }
        int a = 0, b = 0;
        if (f.size() < 2 || !ParseInt(f[0], &a) || !ParseInt(f[1], &b)) {
            if (error) *error = "edges.csv: bad row: " + line;
            return false;
        }
        if (a < 0 || b < 0 || a >= static_cast<int>(nodes_.size()) ||
            b >= static_cast<int>(nodes_.size()) || a == b) {
            if (error) *error = "edges.csv: invalid node id in row: " + line;
            return false;
        }
        const Node& na = nodes_[static_cast<std::size_t>(a)];
        const Node& nb = nodes_[static_cast<std::size_t>(b)];
        double w = std::hypot(na.x_m - nb.x_m, na.y_m - nb.y_m);  // metres
        edges_.push_back({a, b, w});
        adjacency_[static_cast<std::size_t>(a)].push_back({b, w});
        adjacency_[static_cast<std::size_t>(b)].push_back({a, w});
    }
    return true;
}

std::string CampusMap::LocateDataDir(const std::string& argv0) {
    std::vector<fs::path> starts;
    std::error_code ec;
    starts.push_back(fs::current_path(ec));
    if (!argv0.empty()) {
        fs::path exe = fs::absolute(fs::path(argv0), ec);
        if (!ec) starts.push_back(exe.parent_path());
    }
    for (const fs::path& s : starts) {
        fs::path p = s;
        for (int up = 0; up < 6 && !p.empty(); ++up) {
            if (fs::exists(p / "data" / "map.csv", ec)) return (p / "data").string();
            fs::path parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    }
    return "";
}

std::size_t CampusMap::LocationCount() const {
    std::size_t c = 0;
    for (const Node& n : nodes_) {
        if (n.IsLocation()) ++c;
    }
    return c;
}

std::string CampusMap::DisplayName(int id) const {
    if (id < 0 || id >= static_cast<int>(nodes_.size())) return "?";
    const Node& n = nodes_[static_cast<std::size_t>(id)];
    return n.IsLocation() ? n.name : "Junction #" + std::to_string(n.id);
}

// =============================================================== Phase 1
int CampusMap::FindByName(const std::string& name) const {
    auto it = name_index_.find(ToLower(Trim(name)));
    return it == name_index_.end() ? -1 : it->second;
}

std::vector<std::string> CampusMap::Autocomplete(const std::string& prefix) const {
    std::vector<std::string> out;
    std::string p = ToLower(Trim(prefix));
    if (p.empty()) return out;
    for (const Node& n : nodes_) {
        if (!n.IsLocation()) continue;
        std::string lower = ToLower(n.name);
        if (lower.compare(0, p.size(), p) == 0) out.push_back(n.name);
    }
    std::sort(out.begin(), out.end(),
              [](const std::string& a, const std::string& b) { return ToLower(a) < ToLower(b); });
    return out;
}

std::pair<double, double> CampusMap::GetPosition(const std::string& name) const {
    int id = FindByName(name);
    if (id < 0) return {-1.0, -1.0};
    const Node& n = GetNode(id);
    return {n.x_m, n.y_m};
}

std::pair<double, double> CampusMap::GetPositionPixels(const std::string& name) const {
    int id = FindByName(name);
    if (id < 0) return {-1.0, -1.0};
    const Node& n = GetNode(id);
    return {n.x_px, n.y_px};
}

int CampusMap::CalculateEditDistance(const std::string& a, const std::string& b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= m; ++j) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

std::string CampusMap::FindClosestName(const std::string& name) const {
    std::string target = ToLower(Trim(name));
    std::string best;
    int best_d = std::numeric_limits<int>::max();
    for (const Node& n : nodes_) {
        if (!n.IsLocation()) continue;
        int d = CalculateEditDistance(target, ToLower(n.name));
        if (d < best_d) {
            best_d = d;
            best = n.name;
        }
    }
    return best;
}

// =============================================================== Phase 2: shortest path
PathResult CampusMap::CalculateShortestPathDijkstra(int source, int target) const {
    const int n = static_cast<int>(nodes_.size());
    if (source < 0 || target < 0 || source >= n || target >= n) return {};
    std::vector<double> dist(nodes_.size(), kInf);
    std::vector<int> prev(nodes_.size(), -1);
    std::size_t relaxations = 0, expanded = 0;

    using Item = std::pair<double, int>;  // (distance, node)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[static_cast<std::size_t>(source)] = 0.0;
    pq.push({0.0, source});
    while (!pq.empty()) {
        Item top = pq.top();
        pq.pop();
        int u = top.second;
        if (top.first > dist[static_cast<std::size_t>(u)]) continue;  // stale entry
        ++expanded;
        if (u == target) break;
        for (const Neighbor& nb : adjacency_[static_cast<std::size_t>(u)]) {
            ++relaxations;
            double nd = top.first + nb.weight_m;
            if (nd < dist[static_cast<std::size_t>(nb.to)]) {
                dist[static_cast<std::size_t>(nb.to)] = nd;
                prev[static_cast<std::size_t>(nb.to)] = u;
                pq.push({nd, nb.to});
            }
        }
    }
    PathResult result = BuildPath(dist, prev, source, target, relaxations);
    result.expanded = expanded;
    return result;
}

double CampusMap::StraightLineDistance(int a, int b) const {
    const Node& p = nodes_[static_cast<std::size_t>(a)];
    const Node& q = nodes_[static_cast<std::size_t>(b)];
    return std::hypot(p.x_m - q.x_m, p.y_m - q.y_m);
}

// A*: exactly like Dijkstra, but the priority queue is ordered by
//     f(node) = g(node) + h(node)
// where g = distance walked so far and h = straight-line distance from the node to the
// target. h never overestimates (a straight line is the shortest possible way), and it is
// "consistent" (every path weight on this map is a straight-line length), so the first time
// the target is taken out of the queue its distance is already the shortest one.
PathResult CampusMap::CalculateShortestPathAStar(int source, int target) const {
    const int n = static_cast<int>(nodes_.size());
    if (source < 0 || target < 0 || source >= n || target >= n) return {};
    std::vector<double> dist(nodes_.size(), kInf);  // g values
    std::vector<int> prev(nodes_.size(), -1);
    std::size_t relaxations = 0, expanded = 0;

    struct Item {
        double f, g;
        int node;
        bool operator>(const Item& o) const { return f > o.f; }
    };
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[static_cast<std::size_t>(source)] = 0.0;
    pq.push({StraightLineDistance(source, target), 0.0, source});
    while (!pq.empty()) {
        Item top = pq.top();
        pq.pop();
        const int u = top.node;
        if (top.g > dist[static_cast<std::size_t>(u)]) continue;  // stale entry
        ++expanded;
        if (u == target) break;
        for (const Neighbor& nb : adjacency_[static_cast<std::size_t>(u)]) {
            ++relaxations;
            const double ng = top.g + nb.weight_m;
            if (ng < dist[static_cast<std::size_t>(nb.to)]) {
                dist[static_cast<std::size_t>(nb.to)] = ng;
                prev[static_cast<std::size_t>(nb.to)] = u;
                pq.push({ng + StraightLineDistance(nb.to, target), ng, nb.to});
            }
        }
    }
    PathResult result = BuildPath(dist, prev, source, target, relaxations);
    result.expanded = expanded;
    return result;
}

PathResult CampusMap::CalculateShortestPathBellmanFord(int source, int target) const {
    const int n = static_cast<int>(nodes_.size());
    if (source < 0 || target < 0 || source >= n || target >= n) return {};
    std::vector<double> dist(nodes_.size(), kInf);
    std::vector<int> prev(nodes_.size(), -1);
    std::size_t relaxations = 0;
    dist[static_cast<std::size_t>(source)] = 0.0;

    for (int pass = 1; pass < n; ++pass) {
        bool changed = false;
        for (const Edge& e : edges_) {  // every undirected edge works in both directions
            const std::size_t a = static_cast<std::size_t>(e.a);
            const std::size_t b = static_cast<std::size_t>(e.b);
            ++relaxations;
            if (dist[a] != kInf && dist[a] + e.weight_m < dist[b]) {
                dist[b] = dist[a] + e.weight_m;
                prev[b] = e.a;
                changed = true;
            }
            ++relaxations;
            if (dist[b] != kInf && dist[b] + e.weight_m < dist[a]) {
                dist[a] = dist[b] + e.weight_m;
                prev[a] = e.b;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return BuildPath(dist, prev, source, target, relaxations);
}

// =============================================================== Phase 2: cycle detection
bool CampusMap::InsideArea(const Node& n, const SquareArea& a) const {
    const double l = std::min(a.left_m, a.right_m), r = std::max(a.left_m, a.right_m);
    const double t = std::min(a.top_m, a.bottom_m), b = std::max(a.top_m, a.bottom_m);
    return n.x_m >= l && n.x_m <= r && n.y_m >= t && n.y_m <= b;
}

bool CampusMap::CycleDetection(const SquareArea& area, std::vector<int>* cycle_out,
                               std::vector<int>* subgraph_out) const {
    if (cycle_out) cycle_out->clear();
    if (subgraph_out) subgraph_out->clear();
    const std::size_t n = nodes_.size();
    std::vector<char> inside(n, 0);
    for (const Node& node : nodes_) {
        if (InsideArea(node, area)) {
            inside[static_cast<std::size_t>(node.id)] = 1;
            if (subgraph_out) subgraph_out->push_back(node.id);
        }
    }

    std::vector<char> visited(n, 0);
    std::vector<int> parent(n, -1), depth(n, 0);
    for (std::size_t root = 0; root < n; ++root) {
        if (!inside[root] || visited[root]) continue;
        // Iterative DFS: stack of (node, index of next neighbour to examine).
        std::vector<std::pair<int, std::size_t>> stack;
        visited[root] = 1;
        stack.push_back({static_cast<int>(root), 0});
        while (!stack.empty()) {
            const int u = stack.back().first;
            const std::size_t idx = stack.back().second;
            const auto& adj = adjacency_[static_cast<std::size_t>(u)];
            if (idx >= adj.size()) {
                stack.pop_back();
                continue;
            }
            ++stack.back().second;
            const int v = adj[idx].to;
            if (!inside[static_cast<std::size_t>(v)] || v == parent[static_cast<std::size_t>(u)]) continue;
            if (!visited[static_cast<std::size_t>(v)]) {
                visited[static_cast<std::size_t>(v)] = 1;
                parent[static_cast<std::size_t>(v)] = u;
                depth[static_cast<std::size_t>(v)] = depth[static_cast<std::size_t>(u)] + 1;
                stack.push_back({v, 0});
            } else if (depth[static_cast<std::size_t>(v)] < depth[static_cast<std::size_t>(u)]) {
                // Back edge to an ancestor => cycle u -> ... -> v -> u.
                if (cycle_out) {
                    for (int x = u; x != v; x = parent[static_cast<std::size_t>(x)]) cycle_out->push_back(x);
                    cycle_out->push_back(v);
                }
                return true;
            }
        }
    }
    return false;
}

// =============================================================== Phase 2: topological sort
std::vector<std::string> CampusMap::DeliveringCampus(const std::vector<std::string>& locations,
                                                     const std::vector<Dependency>& dependencies,
                                                     std::string* error) const {
    // Resolve and de-duplicate the locations (keep input order).
    std::vector<int> ids;
    std::unordered_map<int, std::size_t> slot;  // node id -> index in ids
    for (const std::string& name : locations) {
        int id = FindByName(name);
        if (id < 0) {
            if (error) {
                std::string msg = "unknown location '" + name + "'";
                std::string close = FindClosestName(name);
                if (!close.empty()) msg += " (did you mean '" + close + "'?)";
                *error = msg;
            }
            return {};
        }
        if (!slot.count(id)) {
            slot[id] = ids.size();
            ids.push_back(id);
        }
    }

    // Directed graph: before -> after.
    std::vector<std::vector<std::size_t>> out(ids.size());
    for (const Dependency& d : dependencies) {
        int a = FindByName(d.before);
        int b = FindByName(d.after);
        if (a < 0 || b < 0 || !slot.count(a) || !slot.count(b)) {
            if (error) {
                *error = "dependency '" + d.before + "' -> '" + d.after +
                         "' refers to a location that is unknown or not in the locations list";
            }
            return {};
        }
        out[slot[a]].push_back(slot[b]);
    }

    enum Color { kWhite = 0, kGray = 1, kBlack = 2 };
    std::vector<int> color(ids.size(), kWhite);
    std::vector<std::size_t> post;   // post-order
    std::vector<std::size_t> path;   // current DFS path (for cycle report)
    bool cyclic = false;
    std::string cycle_text;

    // Recursive lambda via std::function-free helper struct.
    struct Dfs {
        const std::vector<std::vector<std::size_t>>& out;
        std::vector<int>& color;
        std::vector<std::size_t>& post;
        std::vector<std::size_t>& path;
        const std::vector<int>& ids;
        const CampusMap& map;
        bool& cyclic;
        std::string& cycle_text;
        void Visit(std::size_t u) {
            color[u] = kGray;
            path.push_back(u);
            for (std::size_t v : out[u]) {
                if (cyclic) return;
                if (color[v] == kGray) {
                    cyclic = true;
                    std::size_t start = 0;
                    while (path[start] != v) ++start;
                    for (std::size_t i = start; i < path.size(); ++i)
                        cycle_text += map.GetNode(ids[path[i]]).name + " -> ";
                    cycle_text += map.GetNode(ids[v]).name;
                    return;
                }
                if (color[v] == kWhite) Visit(v);
            }
            path.pop_back();
            color[u] = kBlack;
            post.push_back(u);
        }
    } dfs{out, color, post, path, ids, *this, cyclic, cycle_text};

    for (std::size_t i = 0; i < ids.size() && !cyclic; ++i)
        if (color[i] == kWhite) dfs.Visit(i);

    if (cyclic) {
        if (error) *error = "no valid order - the dependencies contain a cycle: " + cycle_text;
        return {};
    }
    std::vector<std::string> order;
    for (auto it = post.rbegin(); it != post.rend(); ++it) order.push_back(GetNode(ids[*it]).name);
    return order;
}

bool CampusMap::ReadLocationsFromCSVFile(const std::string& path, std::vector<std::string>* out,
                                         std::string* error) {
    std::vector<std::string> lines;
    if (!ReadAllLines(path, &lines, error)) return false;
    out->clear();
    bool first = true;
    for (const std::string& line : lines) {
        std::vector<std::string> f = SplitCsv(line);
        if (f.empty() || f[0].empty()) continue;
        if (first) {
            first = false;
            std::string h = ToLower(f[0]);
            if (h == "name" || h == "location" || h == "locations") continue;
        }
        out->push_back(f[0]);
    }
    return true;
}

bool CampusMap::ReadDependenciesFromCSVFile(const std::string& path, std::vector<Dependency>* out,
                                            std::string* error) {
    std::vector<std::string> lines;
    if (!ReadAllLines(path, &lines, error)) return false;
    out->clear();
    bool first = true;
    for (const std::string& line : lines) {
        std::vector<std::string> f = SplitCsv(line);
        if (first) {
            first = false;
            if (!f.empty() && ToLower(f[0]) == "source") continue;
        }
        if (f.size() < 2 || f[0].empty() || f[1].empty()) {
            if (error) *error = "bad dependency row: " + line;
            return false;
        }
        out->push_back({f[0], f[1]});
    }
    return true;
}

}  // namespace campus
