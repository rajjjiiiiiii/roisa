#pragma once
// Report.h — Build a self-contained HTML report string (Qt strings only).

#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace Report {

/// Assemble the report HTML.  `images` pairs a caption with an <img src>
/// value — a data: URI (standalone HTML) or a resource name (PDF via
/// QTextDocument addResource).
QString buildHtml(const QString& title,
                  const std::vector<std::pair<QString, QString>>& info,
                  const std::vector<std::pair<QString, QString>>& images,
                  const std::vector<QStringList>& statsRows,
                  const std::vector<QStringList>& suvRows,
                  const QStringList& measurements);

} // namespace Report
