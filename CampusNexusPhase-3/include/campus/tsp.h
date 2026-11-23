// Campus Nexus - "Travelling Campus" feature.
//
// PROBLEM: the user picks several locations (for example Main Gate, Library,
// Cafeteria, Science Building). In which ORDER should they be visited so that the
// total walking distance is as small as possible? Optionally the walk ends back at
// the first location (a round trip). This is the classic Travelling Salesman
// Problem (TSP).
//
// HOW IT WORKS (in simple words):
//   1. For every pair of chosen locations we ask Dijkstra (already in the project)
//      for the real walking distance along the campus paths. These numbers go into a
//      small table called the DISTANCE MATRIX.
//   2. Three algorithms then look for the best visiting order using that table:
//        - Brute force  : try EVERY possible order, keep the best.      (always right, very slow)
//        - Backtracking : build the order step by step and give up on a
//                         half-built order as soon as it is already
//                         longer than the best one found.               (always right, much faster)
//        - 2-opt        : start with some order and keep reversing a
//                         piece of it whenever that makes it shorter.   (very fast, not always perfect)
//   3. Each algorithm reports distance, running time and the final order.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "campus/campus_map.h"

namespace campus {

// Everything one algorithm reports back.
struct TourResult {
    std::string algorithm;            // "Brute force", "Backtracking" or "2-opt"
    bool ran = false;                 // false => skipped (see note)
    std::string note;                 // why it was skipped, or a short remark
    std::vector<int> order;           // node ids of the stops, in visiting order (start first)
    double distance_m = 0.0;          // total walking distance (includes the way back for a round trip)
    double time_us = 0.0;             // average running time in microseconds
    unsigned long long work = 0;      // brute force: orders checked | backtracking: recursive calls | 2-opt: pairs tested
    unsigned long long pruned = 0;    // backtracking only: half-built orders abandoned early
    unsigned long long reversals = 0; // 2-opt only: how many sections were reversed
    double start_distance_m = 0.0;    // 2-opt only: distance of the order it started from
};

class TourPlanner {
public:
    // Largest number of stops each algorithm accepts (they get too slow after that).
    static constexpr std::size_t kMaxBruteForce = 11;   // 10! = 3,628,800 orders
    static constexpr std::size_t kMaxBacktracking = 16;

    // `stops`: node ids of the locations to visit; stops[0] is the starting point.
    // Duplicates and ids that are not named locations are ignored.
    // `return_to_start`: true => round trip, false => the walk simply ends at the last stop.
    TourPlanner(const CampusMap& map, const std::vector<int>& stops, bool return_to_start);

    // False when fewer than two usable stops are left or two stops are not connected.
    bool valid() const { return error_.empty(); }
    const std::string& error() const { return error_; }

    std::size_t size() const { return stops_.size(); }
    const std::vector<int>& stops() const { return stops_; }
    bool return_to_start() const { return return_to_start_; }

    // The three algorithms. Each one times itself.
    TourResult BruteForce() const;
    TourResult Backtracking() const;
    TourResult TwoOpt() const;

    // Total distance of a visiting order given as node ids (same stops as stops()).
    double OrderDistance(const std::vector<int>& order_ids) const;

    // The full walk on the real campus paths (every junction in between), ready to draw.
    std::vector<int> ExpandToPath(const std::vector<int>& order_ids) const;

    // Walking distance between two of the chosen stops (by position in stops()).
    double Leg(std::size_t i, std::size_t j) const { return dist_[i][j]; }

    // Plain-text complexity summary for `n` stops.
    static std::string ComplexityNote(std::size_t n, bool return_to_start);

private:
    // ---- the actual algorithms work on small indices 0..n-1 into stops_ ----
    double TourLength(const std::vector<int>& idx) const;
    void BruteForceRun(TourResult* r) const;
    void BacktrackingRun(TourResult* r) const;
    void TwoOptRun(const std::vector<int>& start, TourResult* r) const;
    std::vector<int> ToIds(const std::vector<int>& idx) const;
    int IndexOf(int node_id) const;

    const CampusMap& map_;
    std::vector<int> stops_;                    // node ids, stops_[0] = start
    bool return_to_start_ = true;
    std::vector<std::vector<double>> dist_;     // dist_[i][j] = walking distance between stop i and stop j
    std::vector<std::vector<std::vector<int>>> paths_;  // paths_[i][j] = node ids of that walk
    std::string error_;
};

}  // namespace campus
