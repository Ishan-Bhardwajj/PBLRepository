#include "campus/tsp.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace campus {
namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kEps = 1e-9;  // distances are decimals, so "equal" means "closer than this"

using Matrix = std::vector<std::vector<double>>;

// Runs `run` once, then repeats it so that the total measured time is long enough for
// the clock to be accurate (one tiny run is below the clock's resolution).
// Returns the average time of one run in microseconds.
template <typename F>
double AverageMicroseconds(F run) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    run();
    double first = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
    if (first >= 5000.0) return first;  // already slow enough to measure directly
    int reps = static_cast<int>(std::min(2000.0, 5000.0 / std::max(first, 0.1))) + 1;
    t0 = Clock::now();
    for (int i = 0; i < reps; ++i) run();
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / reps;
}

// Length of one complete visiting order (indices into the distance table).
double LengthOf(const Matrix& d, const std::vector<int>& order, bool back_to_start) {
    double len = 0.0;
    for (std::size_t i = 0; i + 1 < order.size(); ++i)
        len += d[static_cast<std::size_t>(order[i])][static_cast<std::size_t>(order[i + 1])];
    if (back_to_start && order.size() > 1)
        len += d[static_cast<std::size_t>(order.back())][static_cast<std::size_t>(order.front())];
    return len;
}

// ======================================================================
//  1. BRUTE FORCE - generate every permutation (every possible order).
// ======================================================================
// The start stop (index 0) stays in place, so only the other n-1 stops are shuffled:
// (n-1)! orders. `Permute(depth)` fixes the stop at position `depth` by trying each
// not-yet-placed stop there (swap in, recurse, swap back).
struct BruteState {
    const Matrix* d = nullptr;
    bool back_to_start = true;
    int n = 0;
    std::vector<int> order;
    std::vector<int> best_order;
    double best_len = kInf;
    unsigned long long checked = 0;
};

void Permute(BruteState& s, int depth) {
    if (depth == s.n) {  // a complete order: measure it, keep it if it is the best so far
        ++s.checked;
        double len = LengthOf(*s.d, s.order, s.back_to_start);
        if (len < s.best_len - kEps) {
            s.best_len = len;
            s.best_order = s.order;
        }
        return;
    }
    for (int i = depth; i < s.n; ++i) {
        std::swap(s.order[static_cast<std::size_t>(depth)], s.order[static_cast<std::size_t>(i)]);
        Permute(s, depth + 1);
        std::swap(s.order[static_cast<std::size_t>(depth)], s.order[static_cast<std::size_t>(i)]);
    }
}

// ======================================================================
//  2. BACKTRACKING - build the order one stop at a time; stop early when hopeless.
// ======================================================================
struct BtState {
    const Matrix* d = nullptr;
    bool back_to_start = true;
    int n = 0;
    std::vector<int> path;       // the partial order built so far
    std::vector<char> used;      // used[i] = stop i is already in `path`
    std::vector<double> min_in;  // min_in[i] = shortest leg that arrives at stop i from any other stop
    std::vector<int> best_order;
    double best_len = kInf;
    unsigned long long calls = 0;
    unsigned long long pruned = 0;
};

// `len_so_far`  : length of the partial route in s.path.
// `rest_bound`  : an OPTIMISTIC guess of what is still to come: every unvisited stop has to
//                 be reached by some leg, and that leg is never shorter than min_in[stop]
//                 (for a round trip the leg back to the start counts too).
//                 The real remaining distance can only be bigger, never smaller.
void Search(BtState& s, double len_so_far, double rest_bound) {
    ++s.calls;
    const std::size_t cur = static_cast<std::size_t>(s.path.back());
    if (static_cast<int>(s.path.size()) == s.n) {  // every stop visited: a complete route
        double total = len_so_far + (s.back_to_start ? (*s.d)[cur][0] : 0.0);
        if (total < s.best_len - kEps) {
            s.best_len = total;
            s.best_order = s.path;
        }
        return;
    }
    // Try the closest unvisited stops first: that finds a good route early, which
    // makes the pruning below cut off far more.
    std::pair<double, int> next[32];
    int count = 0;
    for (int j = 0; j < s.n; ++j)
        if (!s.used[static_cast<std::size_t>(j)]) next[count++] = {(*s.d)[cur][static_cast<std::size_t>(j)], j};
    std::sort(next, next + count);

    for (int k = 0; k < count; ++k) {
        const int j = next[k].second;
        const double new_len = len_so_far + next[k].first;
        const double new_rest = rest_bound - s.min_in[static_cast<std::size_t>(j)];
        // THE BACKTRACKING RULE: if the partial route plus the most optimistic estimate of
        // the rest is already not shorter than the best complete route found so far, this
        // branch can never win. Abandon it without exploring any of its continuations.
        if (new_len + new_rest >= s.best_len - kEps) {
            ++s.pruned;
            continue;
        }
        s.used[static_cast<std::size_t>(j)] = 1;  // choose
        s.path.push_back(j);
        Search(s, new_len, new_rest);             // explore
        s.path.pop_back();                        // un-choose (this is the "back" in backtracking)
        s.used[static_cast<std::size_t>(j)] = 0;
    }
}

std::string WithCommas(unsigned long long v) {
    std::string s = std::to_string(v), out;
    int count = 0;
    for (std::size_t i = s.size(); i-- > 0;) {
        out.insert(out.begin(), s[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return out;
}

// (n-1)!  or 0 when it does not fit in 64 bits.
unsigned long long FactorialOrZero(std::size_t n) {
    unsigned long long f = 1;
    for (std::size_t i = 2; i <= n; ++i) {
        if (f > std::numeric_limits<unsigned long long>::max() / i) return 0;
        f *= i;
    }
    return f;
}

}  // namespace

// ---------------------------------------------------------------------------- setup
TourPlanner::TourPlanner(const CampusMap& map, const std::vector<int>& stops, bool return_to_start)
    : map_(map), return_to_start_(return_to_start) {
    for (int id : stops) {
        if (id < 0 || static_cast<std::size_t>(id) >= map.NodeCount()) continue;
        if (!map.GetNode(id).IsLocation()) continue;
        if (std::find(stops_.begin(), stops_.end(), id) != stops_.end()) continue;
        stops_.push_back(id);
    }
    const std::size_t n = stops_.size();
    if (n < 2) {
        error_ = "Pick at least two different named locations.";
        return;
    }
    // Distance table: one Dijkstra run per pair (the table is symmetric because the
    // campus paths can be walked in both directions).
    dist_.assign(n, std::vector<double>(n, 0.0));
    paths_.assign(n, std::vector<std::vector<int>>(n));
    for (std::size_t i = 0; i < n; ++i) {
        paths_[i][i] = {stops_[i]};
        for (std::size_t j = i + 1; j < n; ++j) {
            PathResult r = map.CalculateShortestPathDijkstra(stops_[i], stops_[j]);
            if (!r.found) {
                error_ = "No walkable route between " + map.DisplayName(stops_[i]) + " and " +
                         map.DisplayName(stops_[j]) + ".";
                return;
            }
            dist_[i][j] = dist_[j][i] = r.distance_m;
            paths_[i][j] = r.nodes;
            paths_[j][i] = std::vector<int>(r.nodes.rbegin(), r.nodes.rend());
        }
    }
}

int TourPlanner::IndexOf(int node_id) const {
    for (std::size_t i = 0; i < stops_.size(); ++i)
        if (stops_[i] == node_id) return static_cast<int>(i);
    return -1;
}

double TourPlanner::TourLength(const std::vector<int>& idx) const { return LengthOf(dist_, idx, return_to_start_); }

std::vector<int> TourPlanner::ToIds(const std::vector<int>& idx) const {
    std::vector<int> ids;
    for (int i : idx) ids.push_back(stops_[static_cast<std::size_t>(i)]);
    return ids;
}

double TourPlanner::OrderDistance(const std::vector<int>& order_ids) const {
    std::vector<int> idx;
    for (int id : order_ids) {
        int i = IndexOf(id);
        if (i < 0) return -1.0;
        idx.push_back(i);
    }
    return TourLength(idx);
}

std::vector<int> TourPlanner::ExpandToPath(const std::vector<int>& order_ids) const {
    std::vector<int> idx;
    for (int id : order_ids) {
        int i = IndexOf(id);
        if (i >= 0) idx.push_back(i);
    }
    std::vector<int> walk;
    if (idx.empty()) return walk;
    walk.push_back(stops_[static_cast<std::size_t>(idx[0])]);
    const std::size_t legs = idx.size() - 1 + ((return_to_start_ && idx.size() > 1) ? 1 : 0);
    for (std::size_t k = 0; k < legs; ++k) {
        const std::size_t a = static_cast<std::size_t>(idx[k]);
        const std::size_t b = static_cast<std::size_t>(idx[(k + 1) % idx.size()]);
        const std::vector<int>& leg = paths_[a][b];
        walk.insert(walk.end(), leg.begin() + 1, leg.end());  // first node is already in `walk`
    }
    return walk;
}

// ------------------------------------------------------------------------ brute force
void TourPlanner::BruteForceRun(TourResult* r) const {
    BruteState s;
    s.d = &dist_;
    s.back_to_start = return_to_start_;
    s.n = static_cast<int>(stops_.size());
    s.order.resize(stops_.size());
    std::iota(s.order.begin(), s.order.end(), 0);
    Permute(s, 1);  // position 0 (the start) never moves
    r->order = ToIds(s.best_order);
    r->distance_m = s.best_len;
    r->work = s.checked;
}

TourResult TourPlanner::BruteForce() const {
    TourResult r;
    r.algorithm = "Brute force";
    if (!valid()) { r.note = error_; return r; }
    if (stops_.size() > kMaxBruteForce) {
        r.note = "skipped: " + std::to_string(stops_.size()) + " stops means " +
                 WithCommas(FactorialOrZero(stops_.size() - 1)) + " orders (limit is " +
                 std::to_string(kMaxBruteForce) + " stops)";
        return r;
    }
    r.time_us = AverageMicroseconds([&] { BruteForceRun(&r); });
    r.ran = true;
    return r;
}

// ------------------------------------------------------------------------ backtracking
void TourPlanner::BacktrackingRun(TourResult* r) const {
    BtState s;
    s.d = &dist_;
    s.back_to_start = return_to_start_;
    s.n = static_cast<int>(stops_.size());
    s.used.assign(stops_.size(), 0);
    s.used[0] = 1;
    s.path = {0};  // always start at the first stop
    s.min_in.assign(stops_.size(), kInf);
    for (std::size_t i = 0; i < stops_.size(); ++i)
        for (std::size_t j = 0; j < stops_.size(); ++j)
            if (i != j) s.min_in[i] = std::min(s.min_in[i], dist_[j][i]);
    double rest = 0.0;  // optimistic estimate for the whole job: all stops except the start...
    for (std::size_t i = 1; i < stops_.size(); ++i) rest += s.min_in[i];
    if (return_to_start_) rest += s.min_in[0];  // ...plus the leg back into the start
    Search(s, 0.0, rest);
    r->order = ToIds(s.best_order);
    r->distance_m = s.best_len;
    r->work = s.calls;
    r->pruned = s.pruned;
}

TourResult TourPlanner::Backtracking() const {
    TourResult r;
    r.algorithm = "Backtracking";
    if (!valid()) { r.note = error_; return r; }
    if (stops_.size() > kMaxBacktracking) {
        r.note = "skipped: limit is " + std::to_string(kMaxBacktracking) + " stops";
        return r;
    }
    r.time_us = AverageMicroseconds([&] { BacktrackingRun(&r); });
    r.ran = true;
    return r;
}

// ------------------------------------------------------------------------------ 2-opt
// Take the visiting order as it is. Pick two positions i < j and REVERSE the section
// between them. Only two "legs" change: (before i -> i) and (j -> after j) become
// (before i -> j) and (i -> after j). If the new pair is shorter, keep the reversal.
// Repeat until no reversal helps any more (a "2-opt local optimum").
void TourPlanner::TwoOptRun(const std::vector<int>& start, TourResult* r) const {
    const int n = static_cast<int>(start.size());
    std::vector<int> t = start;
    r->start_distance_m = TourLength(t);
    r->work = 0;
    r->reversals = 0;
    bool improved = true;
    while (improved) {
        improved = false;
        for (int i = 1; i < n; ++i) {          // position 0 (the start) never moves
            for (int j = i + 1; j < n; ++j) {
                ++r->work;
                const std::size_t a = static_cast<std::size_t>(t[static_cast<std::size_t>(i - 1)]);
                const std::size_t b = static_cast<std::size_t>(t[static_cast<std::size_t>(i)]);
                const std::size_t c = static_cast<std::size_t>(t[static_cast<std::size_t>(j)]);
                // Is there a leg after position j? (Yes, unless this is an open route and j is the last stop.)
                const bool has_after = (j + 1 < n) || return_to_start_;
                const std::size_t d = static_cast<std::size_t>(j + 1 < n ? t[static_cast<std::size_t>(j + 1)] : t[0]);
                const double old_len = dist_[a][b] + (has_after ? dist_[c][d] : 0.0);
                const double new_len = dist_[a][c] + (has_after ? dist_[b][d] : 0.0);
                if (new_len < old_len - kEps) {
                    std::reverse(t.begin() + i, t.begin() + j + 1);
                    ++r->reversals;
                    improved = true;
                }
            }
        }
    }
    r->order = ToIds(t);
    r->distance_m = TourLength(t);
}

TourResult TourPlanner::TwoOpt() const {
    TourResult r;
    r.algorithm = "2-opt";
    if (!valid()) { r.note = error_; return r; }
    std::vector<int> start(stops_.size());
    std::iota(start.begin(), start.end(), 0);  // starts from the order the user typed
    r.time_us = AverageMicroseconds([&] { TwoOptRun(start, &r); });
    r.ran = true;
    r.note = "improves the order you typed";
    return r;
}

// ------------------------------------------------------------------------ explanation
std::string TourPlanner::ComplexityNote(std::size_t n, bool return_to_start) {
    (void)return_to_start;
    std::ostringstream os;
    const unsigned long long f = FactorialOrZero(n > 0 ? n - 1 : 0);
    os << "Complexity for n = " << n << " stops:\n"
       << "  Brute force  : O(n!)   tries (n-1)! = " << (f ? WithCommas(f) : std::string("a huge number of"))
       << " orders, each measured in O(n).\n"
       << "  Backtracking : O(n!) in the worst case, but a half-built route is dropped as soon as it (plus an\n"
       << "                 optimistic guess for the rest) cannot beat the best complete route, so far fewer\n"
       << "                 orders are explored.\n"
       << "  2-opt        : O(n^2) pairs per pass, a few passes; fast for any n but only a heuristic\n"
       << "                 (it can stop at a route slightly longer than the best one).";
    return os.str();
}

}  // namespace campus
