#include "campus/visualizer.h"

#include <algorithm>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace campus {
namespace {

std::string HtmlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

std::string Num(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << v;
    return os.str();
}

std::string PointsOf(const CampusMap& map, const std::vector<int>& nodes) {
    std::string p;
    for (int id : nodes) {
        const Node& n = map.GetNode(id);
        p += Num(n.x_px) + "," + Num(n.y_px) + " ";
    }
    return p;
}

}  // namespace

MapScene::MapScene(const CampusMap& map) : map_(map) {}

void MapScene::AddRoute(const std::vector<int>& nodes, const std::string& color, double width,
                        bool dashed, bool animated) {
    if (nodes.size() < 2) return;
    std::string outline_w = Num(width + 4);
    // dark under-stroke keeps the route readable on busy imagery
    shapes_ += "<polyline points=\"" + PointsOf(map_, nodes) + "\" fill=\"none\" stroke=\"#000\" "
               "stroke-opacity=\"0.55\" stroke-width=\"" + outline_w +
               "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
    shapes_ += "<polyline points=\"" + PointsOf(map_, nodes) + "\" fill=\"none\" stroke=\"" + color +
               "\" stroke-width=\"" + Num(width) + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"";
    if (dashed) shapes_ += " stroke-dasharray=\"14 10\"";
    if (animated) shapes_ += " pathLength=\"1\" class=\"anim\"";
    shapes_ += "/>\n";
}

void MapScene::AddPolygon(const std::vector<int>& nodes, const std::string& color, double width) {
    if (nodes.size() < 3) return;
    shapes_ += "<polygon points=\"" + PointsOf(map_, nodes) + "\" fill=\"" + color +
               "\" fill-opacity=\"0.18\" stroke=\"#000\" stroke-opacity=\"0.55\" stroke-width=\"" +
               Num(width + 4) + "\" stroke-linejoin=\"round\"/>\n";
    shapes_ += "<polygon points=\"" + PointsOf(map_, nodes) + "\" fill=\"none\" stroke=\"" + color +
               "\" stroke-width=\"" + Num(width) + "\" stroke-linejoin=\"round\"/>\n";
}

void MapScene::AddSubgraph(const std::vector<int>& node_ids, const std::string& color) {
    std::vector<char> in(map_.NodeCount(), 0);
    for (int id : node_ids) in[static_cast<std::size_t>(id)] = 1;
    for (const Edge& e : map_.edges()) {
        if (!in[static_cast<std::size_t>(e.a)] || !in[static_cast<std::size_t>(e.b)]) continue;
        const Node& a = map_.GetNode(e.a);
        const Node& b = map_.GetNode(e.b);
        shapes_ += "<line x1=\"" + Num(a.x_px) + "\" y1=\"" + Num(a.y_px) + "\" x2=\"" + Num(b.x_px) +
                   "\" y2=\"" + Num(b.y_px) + "\" stroke=\"" + color +
                   "\" stroke-width=\"4\" stroke-linecap=\"round\"/>\n";
    }
    for (int id : node_ids) {
        const Node& n = map_.GetNode(id);
        shapes_ += "<circle cx=\"" + Num(n.x_px) + "\" cy=\"" + Num(n.y_px) + "\" r=\"" +
                   (n.IsLocation() ? "8" : "5") + "\" fill=\"" + color + "\" stroke=\"#fff\" stroke-width=\"2\"/>\n";
    }
}

void MapScene::AddMarker(int node, const std::string& text, const std::string& color) {
    const Node& n = map_.GetNode(node);
    markers_ += "<g><circle cx=\"" + Num(n.x_px) + "\" cy=\"" + Num(n.y_px) + "\" r=\"15\" fill=\"" + color +
                "\" stroke=\"#fff\" stroke-width=\"3\"/><text x=\"" + Num(n.x_px) + "\" y=\"" + Num(n.y_px) +
                "\" text-anchor=\"middle\" dominant-baseline=\"central\" font-size=\"16\" font-weight=\"700\" "
                "fill=\"#fff\">" + HtmlEscape(text) + "</text><title>" + HtmlEscape(map_.DisplayName(node)) +
                "</title></g>\n";
}

void MapScene::AddHighlight(int node, const std::string& color) {
    const Node& n = map_.GetNode(node);
    markers_ += "<circle cx=\"" + Num(n.x_px) + "\" cy=\"" + Num(n.y_px) + "\" r=\"20\" fill=\"none\" stroke=\"" +
                color + "\" stroke-width=\"4\"/>\n";
}

void MapScene::AddArea(const SquareArea& area, const std::string& color, const std::string& caption) {
    const double mpp = map_.config().metres_per_pixel;
    double l = std::min(area.left_m, area.right_m) / mpp, r = std::max(area.left_m, area.right_m) / mpp;
    double t = std::min(area.top_m, area.bottom_m) / mpp, b = std::max(area.top_m, area.bottom_m) / mpp;
    shapes_ += "<rect x=\"" + Num(l) + "\" y=\"" + Num(t) + "\" width=\"" + Num(r - l) + "\" height=\"" +
               Num(b - t) + "\" fill=\"" + color + "\" fill-opacity=\"0.10\" stroke=\"" + color +
               "\" stroke-width=\"4\" stroke-dasharray=\"18 10\"/>\n";
    if (!caption.empty()) {
        shapes_ += "<text x=\"" + Num(l + 8) + "\" y=\"" + Num(t + 22) + "\" font-size=\"18\" font-weight=\"700\" "
                   "fill=\"" + color + "\" stroke=\"#000\" stroke-width=\"3\" paint-order=\"stroke\">" +
                   HtmlEscape(caption) + "</text>\n";
    }
}

void MapScene::AddInfo(const std::string& label, const std::string& value) { info_.push_back({label, value}); }
void MapScene::AddNote(const std::string& text) { notes_.push_back(text); }
void MapScene::AddLegend(const std::string& color, const std::string& text, bool dashed) {
    legend_.push_back({color, text, dashed});
}

std::string MapScene::ImageHref(const std::string& html_file) const {
    fs::path root = fs::path(map_.data_dir()).parent_path();
    fs::path img = root / map_.config().image_base;
    std::error_code ec;
    fs::path rel = fs::relative(fs::absolute(img, ec), fs::absolute(fs::path(html_file), ec).parent_path(), ec);
    std::string href = ec || rel.empty() ? img.generic_string() : rel.generic_string();
    // percent-encode anything that is not safe inside a URL path
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    for (char ch : href) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                          c == '/' || c == '.' || c == '_' || c == '-' || c == '~' || c == ':';
        if (safe) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

bool MapScene::Save(const std::string& file_path, const std::string& title, std::string* error) const {
    std::error_code ec;
    fs::path p(file_path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(file_path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + file_path;
        return false;
    }
    const int W = map_.config().image_width;
    const int H = map_.config().image_height;
    const double mpp = map_.config().metres_per_pixel;

    // ---- walkable graph layer (toggle) + labels layer (toggle)
    std::string graph, labels;
    for (const Edge& e : map_.edges()) {
        const Node& a = map_.GetNode(e.a);
        const Node& b = map_.GetNode(e.b);
        graph += "<line x1=\"" + Num(a.x_px) + "\" y1=\"" + Num(a.y_px) + "\" x2=\"" + Num(b.x_px) + "\" y2=\"" +
                 Num(b.y_px) + "\" stroke=\"#ffe066\" stroke-width=\"3\" stroke-opacity=\"0.9\"><title>" +
                 Num(e.weight_m) + " m</title></line>\n";
    }
    for (const Node& n : map_.nodes()) {
        if (n.IsLocation()) {
            graph += "<circle cx=\"" + Num(n.x_px) + "\" cy=\"" + Num(n.y_px) +
                     "\" r=\"7\" fill=\"#2f7bff\" stroke=\"#fff\" stroke-width=\"2\"/>\n";
            labels += "<text x=\"" + Num(n.x_px) + "\" y=\"" + Num(n.y_px - 14) +
                      "\" text-anchor=\"middle\" font-size=\"15\" font-weight=\"600\" fill=\"#fff\" stroke=\"#000\" "
                      "stroke-width=\"4\" paint-order=\"stroke\">" + HtmlEscape(n.name) + "</text>\n";
        } else {
            graph += "<circle cx=\"" + Num(n.x_px) + "\" cy=\"" + Num(n.y_px) +
                     "\" r=\"3.5\" fill=\"#ff5cf0\" stroke=\"#fff\" stroke-width=\"1\"><title>Junction #" +
                     std::to_string(n.id) + "</title></circle>\n";
        }
    }

    // ---- scale bar (100 m)
    const double bar = 100.0 / mpp;
    std::string scale = "<g><rect x=\"20\" y=\"" + Num(H - 62) + "\" width=\"" + Num(bar + 40) +
                        "\" height=\"44\" rx=\"6\" fill=\"#000\" fill-opacity=\"0.6\"/><line x1=\"40\" y1=\"" +
                        Num(H - 34) + "\" x2=\"" + Num(40 + bar) + "\" y2=\"" + Num(H - 34) +
                        "\" stroke=\"#fff\" stroke-width=\"4\"/><text x=\"40\" y=\"" + Num(H - 42) +
                        "\" font-size=\"14\" fill=\"#fff\">100 m</text></g>\n";

    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>" << HtmlEscape(title) << " - Campus Nexus</title>\n<style>\n"
        << "body{margin:0;background:#12151c;color:#e8ebf2;font-family:Segoe UI,Roboto,Arial,sans-serif}\n"
        << "header{padding:14px 22px;background:#1b2030;border-bottom:1px solid #2c3550}\n"
        << "header h1{margin:0;font-size:20px} header small{color:#9aa6c4}\n"
        << ".panel{display:flex;flex-wrap:wrap;gap:18px;padding:14px 22px}\n"
        << ".card{background:#1b2030;border:1px solid #2c3550;border-radius:10px;padding:12px 16px;min-width:240px}\n"
        << ".card h2{margin:0 0 8px;font-size:14px;color:#9aa6c4;text-transform:uppercase;letter-spacing:.06em}\n"
        << ".card table{border-collapse:collapse} .card td{padding:2px 12px 2px 0;vertical-align:top}\n"
        << ".card td:first-child{color:#9aa6c4;white-space:nowrap}\n"
        << ".sw{display:inline-block;width:26px;height:0;border-top:5px solid;margin-right:8px;vertical-align:middle}\n"
        << ".sw.d{border-top-style:dashed} .note{margin:6px 0 0;color:#c7cfe4;max-width:520px}\n"
        << ".toolbar{padding:0 22px 10px} .toolbar label{margin-right:18px;cursor:pointer}\n"
        << ".map{position:relative;margin:0 22px 22px;max-width:" << W << "px;border:1px solid #2c3550}\n"
        << ".map img{display:block;width:100%;height:auto}\n"
        << ".map svg{position:absolute;left:0;top:0;width:100%;height:100%}\n"
        << ".anim{stroke-dasharray:1;stroke-dashoffset:1;animation:draw 2.5s ease forwards}\n"
        << "@keyframes draw{to{stroke-dashoffset:0}}\n"
        << "footer{padding:0 22px 22px;color:#7f8aa8;font-size:12px}\n</style>\n</head>\n<body>\n"
        << "<header><h1>" << HtmlEscape(title) << "</h1><small>Campus Nexus &middot; fictitious AI-generated "
        << "campus &middot; scale " << Num(mpp) << " m per pixel &middot; map " << Num(map_.MapWidthMetres())
        << " m &times; " << Num(map_.MapHeightMetres()) << " m</small></header>\n<div class=\"panel\">\n";

    if (!info_.empty() || !notes_.empty()) {
        out << "<div class=\"card\"><h2>Result</h2><table>\n";
        for (const Row& r : info_)
            out << "<tr><td>" << HtmlEscape(r.a) << "</td><td>" << HtmlEscape(r.b) << "</td></tr>\n";
        out << "</table>\n";
        for (const std::string& n : notes_) out << "<p class=\"note\">" << HtmlEscape(n) << "</p>\n";
        out << "</div>\n";
    }
    if (!legend_.empty()) {
        out << "<div class=\"card\"><h2>Legend</h2>\n";
        for (const LegendItem& l : legend_)
            out << "<div><span class=\"sw" << (l.dashed ? " d" : "") << "\" style=\"border-color:" << l.color
                << "\"></span>" << HtmlEscape(l.text) << "</div>\n";
        out << "</div>\n";
    }
    out << "</div>\n<div class=\"toolbar\">"
        << "<label><input type=\"checkbox\" id=\"tg\"" << (graph_default_on_ ? " checked" : "")
        << "> Walkable graph</label>"
        << "<label><input type=\"checkbox\" id=\"tl\" checked> Location names</label></div>\n"
        << "<div class=\"map\">\n<img src=\"" << ImageHref(file_path) << "\" alt=\"Campus map\">\n"
        << "<svg viewBox=\"0 0 " << W << " " << H << "\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        << "<g id=\"graph\" style=\"display:" << (graph_default_on_ ? "inline" : "none") << "\">\n" << graph << "</g>\n"
        << shapes_ << markers_
        << "<g id=\"labels\">\n" << labels << "</g>\n" << scale << "</svg>\n</div>\n"
        << "<footer>Generated by Campus Nexus. Hover an edge to see its length in metres.</footer>\n"
        << "<script>\n"
        << "document.getElementById('tg').onchange=function(){document.getElementById('graph').style.display=this.checked?'inline':'none';};\n"
        << "document.getElementById('tl').onchange=function(){document.getElementById('labels').style.display=this.checked?'inline':'none';};\n"
        << "</script>\n</body>\n</html>\n";
    return true;
}

bool OpenInBrowser(const std::string& file_path) {
#ifdef _WIN32
    std::error_code ec;
    std::string abs = fs::absolute(fs::path(file_path), ec).string();
    std::string cmd = "start \"\" \"" + abs + "\"";
    return std::system(cmd.c_str()) == 0;
#else
    (void)file_path;
    return false;
#endif
}

}  // namespace campus
