"""离线统计的数值回归；合成时钟不作为真实长稳证据。"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class LongAnalysisTest(unittest.TestCase):
    def analyze(self, records):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "unit-timeline.jsonl"
            target = Path(directory) / "summary.json"
            source.write_text("".join(json.dumps(row) + "\n" for row in records))
            subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "tools/benchmark.py"),
                            "analyze-long", "--input", str(source), "--output", str(target)],
                           check=True, capture_output=True, text=True)
            return json.loads(target.read_text())

    @staticmethod
    def sample(minute, rss, phase="idle_end", models=1303):
        return {"phase": phase, "model_count": models, "elapsed_ms": minute * 60000,
                "rss_kib": rss * 1024, "pss_kib": (rss - 10) * 1024,
                "peak_rss_kib": 300 * 1024, "rusage_peak_rss_kib": 300 * 1024,
                "threads": 4, "file_descriptors": 20, "queue_tasks": 0,
                "cache_logical_bytes": 0, "cache_budget_bytes": 64 * 1024 * 1024}

    def test_bins_and_slope(self):
        rows = [{"event": "start", "seconds_requested": 3600}, self.sample(5, 280)]
        rows.extend(self.sample(minute, rss) for minute, rss in [(20, 200), (30, 210), (40, 220), (50, 230)])
        rows.extend([self.sample(60, 100, "final_cleared", 0), {"event": "finished", "elapsed_ms": 3600000}])
        result = self.analyze(rows)
        self.assertTrue(result["complete"])
        bins = [row for row in result["bins"] if row["phase"] == "idle_end"]
        self.assertEqual([row["samples"] for row in bins], [2, 2])
        self.assertEqual([row["rss_median_mib"] for row in bins], [205, 225])
        self.assertAlmostEqual(result["trends"][0]["rss_ols_mib_per_hour"], 60)
        self.assertEqual(result["final_sample"]["model_count"], 0)
        self.assertTrue(result["cache_budget_respected"])

    def test_incomplete_is_not_passed(self):
        rows = [{"event": "start", "seconds_requested": 7200}, self.sample(1, 100)]
        self.assertFalse(self.analyze(rows)["complete"])
        rows.append({"event": "finished", "elapsed_ms": 7199999})
        self.assertFalse(self.analyze(rows)["complete"])


if __name__ == "__main__":
    unittest.main()
