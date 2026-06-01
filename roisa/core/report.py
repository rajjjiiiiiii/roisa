"""
report.py — Build a self-contained HTML report (Qt-independent).

The GUI grabs the view snapshots and gathers the stats/SUV/measurement
data, then calls build_report_html() to assemble a standalone HTML
document (images embedded as base64).  The same HTML can be rendered to
PDF by the caller via QTextDocument/QPrinter.
"""

from __future__ import annotations

import datetime
import html
from typing import List, Optional, Sequence, Tuple


def _table(headers: Sequence[str], rows: Sequence[Sequence[str]]) -> str:
    if not rows:
        return "<p class='muted'>— none —</p>"
    th = "".join(f"<th>{html.escape(str(h))}</th>" for h in headers)
    body = ""
    for r in rows:
        tds = "".join(f"<td>{html.escape(str(c))}</td>" for c in r)
        body += f"<tr>{tds}</tr>"
    return f"<table><thead><tr>{th}</tr></thead><tbody>{body}</tbody></table>"


def build_report_html(title: str,
                      info: dict,
                      images: Sequence[Tuple[str, str]],      # (caption, base64 png) OR (caption, resource-uri)
                      stats_rows: Sequence[Sequence[str]],
                      suv_rows: Sequence[Sequence[str]],
                      measurements: Sequence[str],
                      embed: bool = True) -> str:
    """Assemble the report HTML.

    When embed=True the image strings are base64 PNGs embedded via data: URIs
    (standalone file).  When embed=False they are treated as resource URIs
    (for QTextDocument addResource / PDF rendering).
    """
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    info_rows = [[html.escape(str(k)), html.escape(str(v))] for k, v in info.items()]

    img_html = ""
    for cap, data in images:
        src = f"data:image/png;base64,{data}" if embed else data
        img_html += (
            f"<div class='shot'><img src='{src}'/>"
            f"<div class='cap'>{html.escape(cap)}</div></div>")

    return f"""<!DOCTYPE html><html><head><meta charset='utf-8'>
<style>
  body {{ font-family: sans-serif; color:#222; margin:24px; }}
  h1 {{ color:#1c3a55; border-bottom:2px solid #1c3a55; padding-bottom:4px; }}
  h2 {{ color:#24507a; margin-top:22px; }}
  .muted {{ color:#888; }}
  table {{ border-collapse:collapse; margin:6px 0; }}
  th, td {{ border:1px solid #bbb; padding:3px 8px; font-size:12px; text-align:left; }}
  th {{ background:#eef3f8; }}
  .shots {{ display:flex; flex-wrap:wrap; gap:10px; }}
  .shot {{ text-align:center; }}
  .shot img {{ max-width:300px; border:1px solid #ccc; background:#000; }}
  .cap {{ font-size:11px; color:#555; margin-top:2px; }}
  .ts {{ color:#888; font-size:12px; }}
</style></head><body>
<h1>{html.escape(title)}</h1>
<div class='ts'>Generated {ts}</div>

<h2>Image</h2>
{_table(["Property", "Value"], info_rows)}

<h2>Views</h2>
<div class='shots'>{img_html or "<p class='muted'>— no snapshots —</p>"}</div>

<h2>ROI Statistics</h2>
{_table(["Label", "Voxels", "Volume (mm³)", "Mean", "Std"], stats_rows)}

<h2>SUV Quantification</h2>
{_table(["Label", "Vol (mL)", "SUVmean", "SUVmax", "SUVpeak", "TLG"], suv_rows)}

<h2>Measurements</h2>
{("<ul>" + "".join(f"<li>{html.escape(m)}</li>" for m in measurements) + "</ul>")
  if measurements else "<p class='muted'>— none —</p>"}

<hr/><div class='ts'>ROISA — ROI Segmentation &amp; Analysis</div>
</body></html>"""
