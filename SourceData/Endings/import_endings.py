"""Extract the supplied ending manuscript into typed UE DataTable rows; never edit the source DOCX."""
from pathlib import Path
from zipfile import ZipFile
from xml.etree import ElementTree as ET
import hashlib
import json
import shutil

directory = Path(__file__).resolve().parent
source = Path("D:/qqdownloads/结局.docx")
shutil.copy2(source, directory / source.name)
ns = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
with ZipFile(source) as z:
    root = ET.fromstring(z.read("word/document.xml"))
paragraphs = ["".join(t.text or "" for t in p.findall(".//w:t", ns)) for p in root.findall(".//w:body//w:p", ns)]
paragraphs = [p for p in paragraphs if p.strip()]
definitions = [
    ("FAIL-1", "FailedRedemption", "FinalMoneyBelow", 3, "空书架"),
    ("FAIL-2", "PollutionReleased", "PollutionLimit", 1, "失控 · 被污染吞噬"),
    ("END-1", "Closed", "NegativeBalance", 2, "书店关门 · 出局"),
    ("END-2", "Returned", "FinalThresholds", 4, "归还 · 真结局"),
    ("END-3", "Redeemed", "FinalMoneyAtLeast", 5, "赎身离场"),
]
starts = [next(i for i, p in enumerate(paragraphs) if p.startswith(code)) for code, *_ in definitions]
rows = []
for index, (code, ending, condition, priority, title) in enumerate(definitions):
    end = starts[index + 1] if index + 1 < len(starts) else len(paragraphs)
    body = "\n\n".join(paragraphs[starts[index] + 2:end])
    body = body.replace("【金额数】", "{Money}").replace("三十五天", "{Days}天")
    rows.append(dict(Name=ending, Ending=ending, Condition=condition, Priority=priority,
                     NegativeDaysRequired=3, PollutionThreshold=100, MinEnlighten=60,
                     MaxPollutionExclusive=60, bRequireMoney=ending == "Returned", MinMoney=1500,
                     bRequiresPlayerChoice=ending == "Returned",
                     RedemptionCost=1500 if ending in ("Returned", "Redeemed") else 0,
                     Title=title, Text=body))
(directory / "ending_rows.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
(directory / "source_manifest.json").write_text(json.dumps({
    "source": source.name, "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "paragraphs": paragraphs, "substitutions": {"【金额数】": "{Money}", "三十五天": "{Days}天"},
    "priority": ["PollutionReleased", "Closed", "FailedRedemption", "Returned", "Redeemed"],
    "priority_override": "2026-10-07 user request: reaching pollution 100 ends immediately on any day, before other endings",
    "redemption_timing": "final settlement; eligible Returned requires explicit choice",
}, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"Extracted {len(rows)} ending rows from {source.name}")
