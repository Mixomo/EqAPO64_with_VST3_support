import json
import sqlite3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
db_path = root / "playground" / "ASH-Toolset" / "data" / "processed" / "hpcf_database_ash.db"
compilation_db_path = root / "playground" / "ASH-Toolset" / "data" / "processed" / "hpcf_database_compilation.db"
targets_db_path = root / "playground" / "ASH-Toolset" / "data" / "processed" / "headphone_targets.db"
out_path = root / "resources" / "HeadphoneCalibrations" / "ash_hpcf_catalog.json"

con = sqlite3.connect(db_path)
cur = con.cursor()
freqs = [row[0] for row in cur.execute("select frequency from frequency_axis order by idx")]
filters = []

for brand, model, sample, hp_type, mag_db in cur.execute(
	"select brand, headphone, sample, type, mag_db from hpcf_table order by brand, headphone, sample"
):
	if not brand or not model or not mag_db:
		continue
	try:
		gains = [round(float(value), 4) for value in json.loads(mag_db)]
	except (TypeError, ValueError, json.JSONDecodeError):
		continue
	if len(gains) != len(freqs):
		continue
	filters.append({
		"source": "ASH filters",
		"brand": brand,
		"model": model,
		"sample": sample or "Sample",
		"type": hp_type or "",
		"mag_db": gains,
	})

con.close()

targets = []
if targets_db_path.exists():
	con = sqlite3.connect(targets_db_path)
	cur = con.cursor()
	target_freqs = [row[0] for row in cur.execute("select frequency from frequency_axis order by idx")]
	if target_freqs == freqs:
		best_by_name = {}
		for name, avg_diff_db, n_curves in cur.execute(
			"select target_name, avg_diff_db, n_curves from target_difference_averages order by target_name"
		):
			if not name or not avg_diff_db:
				continue
			try:
				gains = [round(float(value), 4) for value in json.loads(avg_diff_db)]
			except (TypeError, ValueError, json.JSONDecodeError):
				continue
			if len(gains) != len(freqs):
				continue
			if name not in best_by_name or int(n_curves or 0) > best_by_name[name]["n_curves"]:
				best_by_name[name] = {
					"name": name,
					"category": "ASH target",
					"rig": "ASH",
					"n_curves": int(n_curves or 0),
					"mag_db": gains,
				}
		targets = [best_by_name[name] for name in sorted(best_by_name, key=str.casefold)]
	con.close()

if compilation_db_path.exists():
	con = sqlite3.connect(compilation_db_path)
	cur = con.cursor()
	comp_freqs = [row[0] for row in cur.execute("select frequency from frequency_axis order by idx")]
	if comp_freqs == freqs:
		for source, hp_type, rig, headphone_name, mag_db in cur.execute(
			"select source, type, rig, headphone_name, mag_db from hpcf_table order by source, headphone_name"
		):
			if not source or not headphone_name or not mag_db:
				continue
			try:
				gains = [round(float(value), 4) for value in json.loads(mag_db)]
			except (TypeError, ValueError, json.JSONDecodeError):
				continue
			if len(gains) != len(freqs):
				continue
			sample_parts = [part for part in (hp_type or "", rig or "") if part]
			filters.append({
				"source": "ASH compilation",
				"brand": source,
				"model": headphone_name,
				"sample": " / ".join(sample_parts) or "Sample",
				"type": hp_type or "",
				"rig": rig or "",
				"mag_db": gains,
			})
	con.close()

out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(
	json.dumps({
		"version": 1,
		"source": "ASH Toolset HpCF databases",
		"frequency_axis": freqs,
		"filters": filters,
		"targets": targets,
	}, ensure_ascii=False, separators=(",", ":")),
	encoding="utf-8",
)
print(f"Wrote {len(filters)} ASH filters and {len(targets)} ASH targets to {out_path}")
