// Campus Nexus - map visualiser.
//
// Instead of depending on OpenCV, results are drawn as an SVG overlay on top of the
// campus image inside a small self-contained HTML page. Any browser on Windows 11
// (Edge, Chrome, Firefox) can open it - no extra libraries or installs required.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "campus/campus_map.h"

namespace campus {

class MapScene {
public:
    explicit MapScene(const CampusMap& map);

    // Whether the walkable-graph layer is visible when the page opens.
    void ShowGraphByDefault(bool on) { graph_default_on_ = on; }

    // A polyline through the given node ids.
    void AddRoute(const std::vector<int>& nodes, const std::string& color, double width,
                  bool dashed = false, bool animated = false);
    // A closed polygon through the given node ids (used to draw a cycle).
    void AddPolygon(const std::vector<int>& nodes, const std::string& color, double width);
    // Draws every edge whose two end points are in `node_ids`, plus the node dots.
    void AddSubgraph(const std::vector<int>& node_ids, const std::string& color);
    // Large round marker with a short text (letter or number) inside.
    void AddMarker(int node, const std::string& text, const std::string& color);
    // Ring around a node.
    void AddHighlight(int node, const std::string& color);
    // Dashed rectangle (metres) with a caption.
    void AddArea(const SquareArea& area, const std::string& color, const std::string& caption);

    // Side panel content.
    void AddInfo(const std::string& label, const std::string& value);
    void AddNote(const std::string& text);
    void AddLegend(const std::string& color, const std::string& text, bool dashed = false);

    // Writes the HTML file (creating the folder if needed). Returns false on failure.
    bool Save(const std::string& file_path, const std::string& title, std::string* error) const;

private:
    struct Row {
        std::string a, b;
    };
    struct LegendItem {
        std::string color, text;
        bool dashed;
    };

    std::string ImageHref(const std::string& html_file) const;

    const CampusMap& map_;
    bool graph_default_on_ = false;
    std::string shapes_;   // SVG fragments drawn under markers
    std::string markers_;  // SVG fragments drawn on top
    std::vector<Row> info_;
    std::vector<std::string> notes_;
    std::vector<LegendItem> legend_;
};

// Opens a file with the system default application (browser for .html).
// Only does something on Windows; elsewhere it returns false.
bool OpenInBrowser(const std::string& file_path);

}  // namespace campus
