// Report.cpp — HTML report assembly.

#include "Report.h"

#include <QDateTime>

namespace {

QString esc(const QString& s) { return s.toHtmlEscaped(); }

QString table(const QStringList& headers, const std::vector<QStringList>& rows)
{
    if (rows.empty()) return "<p class='muted'>— none —</p>";
    QString th;
    for (const auto& h : headers) th += "<th>" + esc(h) + "</th>";
    QString body;
    for (const auto& r : rows) {
        QString tds;
        for (const auto& c : r) tds += "<td>" + esc(c) + "</td>";
        body += "<tr>" + tds + "</tr>";
    }
    return "<table><thead><tr>" + th + "</tr></thead><tbody>" + body + "</tbody></table>";
}

} // namespace

QString Report::buildHtml(const QString& title,
                          const std::vector<std::pair<QString, QString>>& info,
                          const std::vector<std::pair<QString, QString>>& images,
                          const std::vector<QStringList>& statsRows,
                          const std::vector<QStringList>& suvRows,
                          const QStringList& measurements)
{
    const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    std::vector<QStringList> infoRows;
    for (const auto& kv : info) infoRows.push_back({kv.first, kv.second});

    QString imgHtml;
    for (const auto& cap : images)
        imgHtml += "<div class='shot'><img src='" + cap.second + "'/>"
                   "<div class='cap'>" + esc(cap.first) + "</div></div>";
    if (imgHtml.isEmpty()) imgHtml = "<p class='muted'>— no snapshots —</p>";

    QString measHtml;
    if (measurements.isEmpty()) {
        measHtml = "<p class='muted'>— none —</p>";
    } else {
        measHtml = "<ul>";
        for (const auto& m : measurements) measHtml += "<li>" + esc(m) + "</li>";
        measHtml += "</ul>";
    }

    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
        "body{font-family:sans-serif;color:#222;margin:24px;}"
        "h1{color:#1c3a55;border-bottom:2px solid #1c3a55;padding-bottom:4px;}"
        "h2{color:#24507a;margin-top:22px;}"
        ".muted{color:#888;}"
        "table{border-collapse:collapse;margin:6px 0;}"
        "th,td{border:1px solid #bbb;padding:3px 8px;font-size:12px;text-align:left;}"
        "th{background:#eef3f8;}"
        ".shots{} .shot{display:inline-block;text-align:center;margin:5px;}"
        ".shot img{max-width:300px;border:1px solid #ccc;background:#000;}"
        ".cap{font-size:11px;color:#555;margin-top:2px;}"
        ".ts{color:#888;font-size:12px;}"
        "</style></head><body>"
        "<h1>%1</h1><div class='ts'>Generated %2</div>"
        "<h2>Image</h2>%3"
        "<h2>Views</h2><div class='shots'>%4</div>"
        "<h2>ROI Statistics</h2>%5"
        "<h2>SUV Quantification</h2>%6"
        "<h2>Measurements</h2>%7"
        "<hr/><div class='ts'>ROISA — ROI Segmentation &amp; Analysis</div>"
        "</body></html>")
        .arg(esc(title), ts,
             table({"Property", "Value"}, infoRows),
             imgHtml,
             table({"Label", "Voxels", "Volume (mm³)", "Mean", "Std"}, statsRows),
             table({"Label", "Vol (mL)", "SUVmean", "SUVmax", "SUVpeak", "TLG"}, suvRows),
             measHtml);
}
